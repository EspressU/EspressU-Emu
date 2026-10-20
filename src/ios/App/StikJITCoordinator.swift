// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit
import Darwin
import Network

enum StikJITSetup {
    static let forceScriptKey = "espressu.stikjit.forceScript"
    static let txmKey = "espressu.stikjit.txm"
    static let pairingNameKey = "espressu.stikjit.pairingName"

    static var isInLiveContainer: Bool { getenv("LC_HOME_PATH") != nil }

    static var directory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("StikJIT", isDirectory: true)
    }

    static var pairingFileURL: URL { directory.appendingPathComponent("pairingFile.plist") }

    static func ensureDirectory() {
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    static var hasPairingFile: Bool {
        guard let size = (try? FileManager.default.attributesOfItem(atPath: pairingFileURL.path))?[.size] as? NSNumber
        else { return false }
        return size.intValue > 0
    }

    static var pairingFileName: String? {
        guard hasPairingFile else { return nil }
        return UserDefaults.standard.string(forKey: pairingNameKey) ?? pairingFileURL.lastPathComponent
    }

    static var forceScript: Bool {
        get { UserDefaults.standard.bool(forKey: forceScriptKey) }
        set { UserDefaults.standard.set(newValue, forKey: forceScriptKey) }
    }

    static var unavailableReason: String? {
        if isInLiveContainer {
            return "Built-in JIT doesn't work inside LiveContainer, because LiveContainer can't start EspressU's helper. Turn Use JIT off, or let LiveContainer switch JIT on."
        }
        guard #available(iOS 17.4, *) else {
            return "Built-in JIT needs iOS 17.4 or later."
        }
        if StikJITHelperLauncher.helperIdentifier() == nil {
            return "This build of EspressU doesn't contain the JIT helper."
        }
        return nil
    }

    static var isAvailable: Bool { unavailableReason == nil }

    static func importPairingFile(from url: URL) throws {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let data = try Data(contentsOf: url)
        guard !data.isEmpty else { throw SetupError("That file is empty.") }
        guard data.count < 4_000_000 else { throw SetupError("That file is far too big to be a pairing file.") }
        ensureDirectory()
        try data.write(to: pairingFileURL, options: .atomic)
        UserDefaults.standard.set(url.lastPathComponent, forKey: pairingNameKey)
    }

    static func removePairingFile() {
        try? FileManager.default.removeItem(at: pairingFileURL)
        UserDefaults.standard.removeObject(forKey: pairingNameKey)
        NotificationCenter.default.post(name: .espressuJITSettingChanged, object: nil)
    }

    static func savePairingFile(_ data: Data, name: String) throws {
        ensureDirectory()
        try data.write(to: pairingFileURL, options: .atomic)
        UserDefaults.standard.set(name, forKey: pairingNameKey)
    }

    struct SetupError: LocalizedError {
        let text: String
        init(_ text: String) { self.text = text }
        var errorDescription: String? { text }
    }

    static func preflight() -> Result<Data, StikJITCoordinator.Failure> {
        if let reason = unavailableReason {
            return .failure(.init(title: "Built-in JIT isn't available", message: reason))
        }
        guard StikJITHelperLauncher.hostHasGetTaskAllow() else {
            return .failure(.init(title: "EspressU can't get JIT",
                                  message: "This installation does not have get-task-allow. Reinstall EspressU using a signing method that keeps this entitlement."))
        }
        guard let data = try? Data(contentsOf: pairingFileURL), !data.isEmpty else {
            return .failure(.init(title: "Pairing file missing",
                                  message: "Pair this device or import a pairing file first: Settings › JIT."))
        }
        return .success(data)
    }
}

@MainActor
final class StikJITCoordinator: ObservableObject {
    static let shared = StikJITCoordinator()

    struct Failure: Identifiable, Equatable, Error {
        let id = UUID()
        let title: String
        let message: String
        var offersInterpreter = false
    }

    enum Readiness: Equatable {
        case notChecked, ready, unreachable(String), notReady(String)

        var title: String {
            switch self {
            case .notChecked: return "Not checked"
            case .ready: return "Ready"
            case .unreachable: return "Unreachable"
            case .notReady: return "Not ready"
            }
        }

        var reason: String? {
            switch self {
            case .unreachable(let reason), .notReady(let reason): return reason
            default: return nil
            }
        }
    }

    enum Job: Equatable { case enable, prepare, resetDDI, status }

    enum LaunchOutcome { case ready, interpreter, cancelled }

    @Published private(set) var job: Job?
    @Published private(set) var stageText = ""
    @Published private(set) var stageFraction: Double?
    @Published private(set) var readiness = Readiness.notChecked
    @Published private(set) var txmText = StikJITCoordinator.txmTitle(UserDefaults.standard.string(forKey: StikJITSetup.txmKey))
    @Published var note: String?
    @Published var failure: Failure?
    @Published private(set) var showsLaunchProgress = false
    @Published private(set) var lastLog: [String] = []

    private var launchCompletion: ((LaunchOutcome) -> Void)?
    @Published private(set) var quiet = false
    private var lastAutoAttempt: Date?
    private var stageObserver: Any?
    private var watchdog: Timer?
    private var lastActivity = Date()

    private static let silenceLimit: TimeInterval = 300

    private static let stages = ["reachability", "checkingDDI", "mounting", "verifying", "prepared", "script"]
        + (0...10).map { "download.\($0)" }

    var launchNeedsJIT: Bool {
        JITSettings.enabled && !JITStatus.isUsable
    }

    /// Called by LocalDevVPNMonitor the moment the tunnel appears or goes away.
    func vpnChanged(_ connected: Bool) {
        guard job == nil else { return }
        if connected {
            if case .unreachable = readiness { readiness = .notChecked }
        } else {
            readiness = .unreachable("LocalDevVPN is off. Turn it on and JIT switches on by itself.")
        }
    }

    func acquireForLaunch(_ completion: @escaping (LaunchOutcome) -> Void) {
        if JITStatus.isUsable { completion(.ready); return }
        if job == .enable && quiet {
            quiet = false
            launchCompletion = completion
            showsLaunchProgress = true
            return
        }
        guard job == nil else { return }
        launchCompletion = completion
        startEnable()
    }

    func autoEnableIfPossible() {
        guard JITSettings.enabled, !JITStatus.isUsable, job == nil, launchCompletion == nil,
              StikJITSetup.isAvailable, StikJITSetup.hasPairingFile,
              StikJITHelperLauncher.hostHasGetTaskAllow(), !StikJITHelperLauncher.isBusy(),
              !DevicePairing.shared.isActive else { return }
        if let last = lastAutoAttempt, Date().timeIntervalSince(last) < 8 { return }
        lastAutoAttempt = Date()
        cemu_bridge_log_checkpoint("StikJIT: automatic start")
        quiet = true
        startEnable()
    }

    func retryLaunch() {
        failure = nil
        guard launchCompletion != nil else { return }
        startEnable()
    }

    func playWithoutJIT() {
        failure = nil
        finishLaunch(.interpreter)
    }

    func cancelLaunch() {
        failure = nil
        if job == .enable {
            StikJITHelperLauncher.cancel(withReason: "Cancelled.")
        }
        endJob()
        finishLaunch(.cancelled)
    }

    private func startEnable() {
        let pairing: Data
        switch StikJITSetup.preflight() {
        case .failure(var problem):
            cemu_bridge_log_checkpoint("StikJIT: preflight failed: \(problem.title). \(problem.message)")
            if quiet { quiet = false; return }
            problem.offersInterpreter = true
            failure = problem
            return
        case .success(let data):
            pairing = data
        }
        showsLaunchProgress = !quiet
        begin(.enable, request: [
            "action": "enable",
            "pid": NSNumber(value: getpid()),
            "pairing": pairing,
            "forceScript": StikJITSetup.forceScript,
        ], starting: "Starting the JIT helper…") { [weak self] reply, problem in
            self?.enableFinished(reply: reply, problem: problem)
        }
        guard job == .enable else { return }
        Task { @MainActor [weak self] in
            while let self, self.job == .enable {
                if JITStatus.isUsable {
                    self.stageText = "JIT is on."
                    self.readiness = .ready
                    self.quiet = false
                    self.endJob(keepHelperOpen: true)
                    self.finishLaunch(.ready)
                    return
                }
                try? await Task.sleep(nanoseconds: 150_000_000)
            }
        }
    }

    private func enableFinished(reply: [String: Any]?, problem: String?) {
        cemu_bridge_log_checkpoint("StikJIT: enable answered ok=\(reply?["ok"] as? Bool ?? false) problem=\(problem ?? "-") reason=\(reply?["reason"] as? String ?? "-") debugged=\(JITStatus.isActive)")
        storeTXM(reply)
        lastLog = reply?["log"] as? [String] ?? lastLog
        guard job == .enable else { return }
        endJob()
        if JITStatus.isUsable {
            readiness = .ready
            quiet = false
            finishLaunch(.ready)
            return
        }
        let reason = problem ?? (reply?["reason"] as? String)
            ?? "The JIT helper finished, but JIT still isn't on."
        if quiet {
            quiet = false
            readiness = .unreachable("Connect LocalDevVPN. JIT then switches on by itself.")
            return
        }
        readiness = .notReady(Self.explain(reason))
        failure = Failure(title: Self.launchTitle(reason), message: Self.explain(reason), offersInterpreter: true)
    }

    private func finishLaunch(_ outcome: LaunchOutcome) {
        cemu_bridge_log_checkpoint("StikJIT: launch \(outcome)")
        showsLaunchProgress = false
        let completion = launchCompletion
        launchCompletion = nil
        completion?(outcome)
    }

    func prepare() {
        guard job == nil else { return }
        switch StikJITSetup.preflight() {
        case .failure(let problem):
            failure = problem
            return
        case .success(let pairing):
            note = nil
            begin(.prepare, request: ["action": "prepare", "pairing": pairing], starting: "Starting the JIT helper…") { [weak self] reply, problem in
                guard let self else { return }
                self.storeTXM(reply)
                self.lastLog = reply?["log"] as? [String] ?? []
                self.endJob()
                if let problem {
                    self.readiness = .notReady(problem)
                } else {
                    let reason = reply?["reason"] as? String ?? "The JIT helper gave no reason."
                    switch reply?["readiness"] as? String {
                    case "ready": self.readiness = .ready
                    case "unreachable": self.readiness = .unreachable(Self.explain(reason))
                    default: self.readiness = .notReady(Self.explain(reason))
                    }
                }
            }
        }
    }

    func resetDDI(announce: Bool = true) {
        guard job == nil else { return }
        if let reason = StikJITSetup.unavailableReason {
            failure = Failure(title: "Built-in JIT isn't available", message: reason)
            return
        }
        note = nil
        begin(.resetDDI, request: ["action": "resetDDI"], starting: "Removing the cached Developer Disk Image…") { [weak self] reply, problem in
            guard let self else { return }
            self.storeTXM(reply)
            self.endJob()
            if !announce {
                self.readiness = .notChecked
                return
            }
            if let reason = problem ?? ((reply?["ok"] as? Bool) == true ? nil : reply?["reason"] as? String ?? "Unknown error.") {
                self.note = "Couldn't reset the Developer Disk Image: \(reason)"
            } else {
                self.readiness = .notChecked
                self.note = "The cached Developer Disk Image is gone. It's downloaded again the next time JIT switches on."
            }
        }
    }

    func refreshTXM() {
        guard job == nil, StikJITSetup.isAvailable, !StikJITHelperLauncher.isBusy() else { return }
        begin(.status, request: ["action": "status"], starting: "") { [weak self] reply, _ in
            self?.storeTXM(reply)
            self?.endJob()
        }
    }

    func settingsWereReset() {
        lastAutoAttempt = nil
        readiness = .notChecked
        note = nil
        lastLog = []
        txmText = Self.txmTitle(nil)
        guard job == nil, StikJITSetup.isAvailable else { return }
        resetDDI(announce: false)
    }

    func pairingFileChanged() {
        readiness = .notChecked
        note = nil
        lastAutoAttempt = nil
    }

    private func begin(_ newJob: Job, request: [String: Any], starting: String,
                       reply: @escaping ([String: Any]?, String?) -> Void) {
        let prefix = "\(Bundle.main.bundleIdentifier ?? "EspressU").stikjit.\(UUID().uuidString.prefix(8))"
        var payload = request
        payload["notify"] = prefix
        var problem: NSString?
        job = newJob
        stageText = starting
        stageFraction = nil
        lastActivity = Date()
        if let old = stageObserver { StikJITHelperLauncher.stopObserving(old) }
        stageObserver = StikJITHelperLauncher.observeStages(withPrefix: prefix, stages: Self.stages) { [weak self] stage in
            self?.stageChanged(stage)
        }
        let started = StikJITHelperLauncher.sendRequest(payload, reply: { answer, failureText in
            Task { @MainActor in reply(answer, failureText) }
        }, failure: &problem)
        cemu_bridge_log_checkpoint("StikJIT: \(newJob) \(started ? "started" : "not started: \((problem as String?) ?? "")")")
        guard started else {
            endJob()
            let text = (problem as String?) ?? "The JIT helper couldn't be started."
            if newJob == .enable && quiet {
                quiet = false
            } else if newJob == .enable {
                failure = Failure(title: "JIT couldn't be enabled", message: text, offersInterpreter: true)
            } else if newJob != .status {
                failure = Failure(title: "Built-in JIT", message: text)
            }
            return
        }
        guard newJob != .status else { return }
        watchdog?.invalidate()
        watchdog = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.checkSilence() }
        }
    }

    private func checkSilence() {
        guard job != nil, Date().timeIntervalSince(lastActivity) > Self.silenceLimit else { return }
        StikJITHelperLauncher.cancel(withReason: "The JIT helper stopped responding (nothing for \(Int(Self.silenceLimit)) seconds). Check LocalDevVPN and try again.")
    }

    private func endJob(keepHelperOpen: Bool = false) {
        job = nil
        stageFraction = nil
        watchdog?.invalidate()
        watchdog = nil
        if !keepHelperOpen, let stageObserver {
            StikJITHelperLauncher.stopObserving(stageObserver)
            self.stageObserver = nil
        }
    }

    private func stageChanged(_ stage: String) {
        lastActivity = Date()
        cemu_bridge_log_checkpoint("StikJIT: stage \(stage)")
        if stage.hasPrefix("download.") {
            let tenth = Double(stage.dropFirst("download.".count)) ?? 0
            stageFraction = tenth / 10
            stageText = "Downloading the Developer Disk Image…"
            return
        }
        stageFraction = nil
        switch stage {
        case "reachability": stageText = "Looking for the device through LocalDevVPN…"
        case "checkingDDI": stageText = "Checking the Developer Disk Image…"
        case "mounting": stageText = "Mounting the Developer Disk Image…"
        case "verifying": stageText = "Checking the mount…"
        case "prepared": stageText = job == .enable ? "Device ready. Attaching to EspressU…" : "Device ready."
        case "script": stageText = "Running the JIT script…"
        default: break
        }
    }

    private func storeTXM(_ reply: [String: Any]?) {
        guard let value = reply?["txm"] as? String else { return }
        UserDefaults.standard.set(value, forKey: StikJITSetup.txmKey)
        txmText = Self.txmTitle(value)
    }

    private static func txmTitle(_ value: String?) -> String {
        switch value {
        case "present": return "Present"
        case "absent": return "Not Present"
        default: return "Unknown"
        }
    }

    static func launchTitle(_ reason: String) -> String {
        let lower = reason.lowercased()
        if lower.contains("pairing") { return "The pairing file doesn't work" }
        if lower.contains("timed out") || lower.contains("unreachable") || lower.contains("connect")
            || lower.contains("refused") || lower.contains("network") || lower.contains("tunnel") {
            return "LocalDevVPN isn't connected"
        }
        return "JIT couldn't be enabled"
    }

    static func explain(_ reason: String) -> String {
        let lower = reason.lowercased()
        var hint: String?
        if lower.contains("pairing") {
            hint = "Pair this device again or import a fresh pairing file."
        } else if lower.contains("timed out") || lower.contains("unreachable") || lower.contains("connect")
                    || lower.contains("refused") || lower.contains("network") || lower.contains("tunnel") {
            hint = "Is LocalDevVPN connected? Use Wi-Fi if you can; without Wi-Fi, turn on mobile data, connect LocalDevVPN, then Airplane Mode."
        } else if lower.contains("download") {
            hint = "Downloading the Developer Disk Image needs internet. Try again, or reset it under Settings › JIT › Advanced."
        } else if lower.contains("mount") || lower.contains("developer disk image") || lower.contains("ddi") {
            hint = "Restart the device and try again. If it keeps failing, use Settings › JIT › Advanced › Reset Developer Disk Image."
        } else if lower.contains("txm") {
            hint = "Turn on Settings › JIT › Advanced › Force JIT script."
        }
        guard let hint else { return reason }
        return "\(reason)\n\n\(hint)"
    }
}

extension View {
    func stikJITOverlay() -> some View {
        modifier(StikJITOverlayModifier())
    }
}

private struct StikJITOverlayModifier: ViewModifier {
    @ObservedObject private var coordinator = StikJITCoordinator.shared

    func body(content: Content) -> some View {
        content
            .overlay {
                if coordinator.showsLaunchProgress && coordinator.failure == nil {
                    ZStack {
                        Color.black.opacity(0.35).ignoresSafeArea()
                        StikJITProgressCard(coordinator: coordinator)
                            .padding(24)
                    }
                    .transition(.opacity)
                }
            }
            .animation(.easeOut(duration: 0.2), value: coordinator.showsLaunchProgress)
            .alert(coordinator.failure?.title ?? "", isPresented: Binding(
                get: { coordinator.failure != nil },
                set: { if !$0 && coordinator.failure != nil { dismissFailure() } })) {
                if coordinator.failure?.offersInterpreter == true {
                    Button("Try again") { coordinator.retryLaunch() }
                    Button("Play without JIT") { coordinator.playWithoutJIT() }
                    Button("Cancel", role: .cancel) { coordinator.cancelLaunch() }
                } else {
                    Button("OK", role: .cancel) { coordinator.failure = nil }
                }
            } message: {
                Text(coordinator.failure?.message ?? "")
            }
    }

    private func dismissFailure() {
        if coordinator.failure?.offersInterpreter == true {
            coordinator.cancelLaunch()
        } else {
            coordinator.failure = nil
        }
    }
}

private struct StikJITProgressCard: View {
    @ObservedObject var coordinator: StikJITCoordinator

    var body: some View {
        VStack(spacing: 14) {
            HStack(spacing: 14) {
                Image(systemName: "bolt.fill")
                    .font(.system(size: 24, weight: .bold))
                    .foregroundColor(EspressUTheme.accentText)
                    .frame(width: 52, height: 52)
                    .background(EspressUTheme.accentText.opacity(0.15), in: RoundedRectangle(cornerRadius: 15, style: .continuous))
                VStack(alignment: .leading, spacing: 3) {
                    Text("Switching JIT on")
                        .font(.system(size: 19, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                    Text(coordinator.stageText.isEmpty ? "Starting the JIT helper…" : coordinator.stageText)
                        .font(.system(size: 13, weight: .medium, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
            }
            if let fraction = coordinator.stageFraction {
                ProgressView(value: fraction)
                    .tint(EspressUTheme.accentText)
            } else {
                ProgressView()
                    .tint(EspressUTheme.accentText)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            Text("The game starts as soon as JIT is on. LocalDevVPN has to be connected.")
                .font(.system(size: 12, weight: .medium, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
                .frame(maxWidth: .infinity, alignment: .leading)
                .fixedSize(horizontal: false, vertical: true)
            Button { coordinator.cancelLaunch() } label: {
                Text("Cancel")
                    .font(.system(size: 16, weight: .bold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .background(EspressUTheme.wrapper.opacity(0.7), in: Capsule())
            }
            .buttonStyle(.plain)
        }
        .padding(20)
        .frame(maxWidth: 420)
        .espressUSurface(RoundedRectangle(cornerRadius: 28, style: .continuous))
    }
}


/// Watches whether LocalDevVPN's tunnel is up, so the JIT status follows it within a couple of seconds
/// instead of only changing after a restart.
@MainActor
final class LocalDevVPNMonitor: ObservableObject {
    static let shared = LocalDevVPNMonitor()

    @Published private(set) var isConnected = LocalDevVPNMonitor.detect()

    private var timer: Timer?
    private var pathMonitor: NWPathMonitor?
    private var activeObserver: NSObjectProtocol?

    func start() {
        guard timer == nil else { return }
        let monitor = NWPathMonitor()
        monitor.pathUpdateHandler = { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
        monitor.start(queue: DispatchQueue(label: "com.am4rdev.EspressU.localdevvpn"))
        pathMonitor = monitor
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
        activeObserver = NotificationCenter.default.addObserver(forName: UIApplication.didBecomeActiveNotification,
                                                                object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
        if !isConnected { StikJITCoordinator.shared.vpnChanged(false) }
    }

    func refresh() {
        let now = Self.detect()
        guard now != isConnected else { return }
        isConnected = now
        cemu_bridge_log_checkpoint("LocalDevVPN: \(now ? "connected" : "disconnected")")
        StikJITCoordinator.shared.vpnChanged(now)
    }

    /// LocalDevVPN shows up as a utun interface with a private 10.x address.
    nonisolated static func detect() -> Bool {
        var list: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&list) == 0, let first = list else { return false }
        defer { freeifaddrs(list) }
        var cursor: UnsafeMutablePointer<ifaddrs>? = first
        while let entry = cursor {
            let item = entry.pointee
            cursor = item.ifa_next
            guard let address = item.ifa_addr,
                  address.pointee.sa_family == sa_family_t(AF_INET),
                  (item.ifa_flags & UInt32(IFF_UP)) != 0,
                  String(cString: item.ifa_name).hasPrefix("utun") else { continue }
            let ip = address.withMemoryRebound(to: sockaddr_in.self, capacity: 1) {
                UInt32(bigEndian: $0.pointee.sin_addr.s_addr)
            }
            if ip >> 24 == 10 { return true }
        }
        return false
    }
}
