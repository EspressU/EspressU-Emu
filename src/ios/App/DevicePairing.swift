// SPDX-License-Identifier: MPL-2.0

import AVFoundation
import Foundation
import UserNotifications

@MainActor
final class DevicePairing: NSObject, ObservableObject {
    static let shared = DevicePairing()

    enum State: Equatable {
        case idle
        case waiting
        case pin(String)
        case paired(String?)
        case failed(String)
    }

    @Published private(set) var state: State = .idle

    var isActive: Bool {
        switch state {
        case .waiting, .pin: return true
        default: return false
        }
    }

    static let hostName = "EspressU"

    /// The built-in pairing (EspressU showing up in Developer Mode settings) needs this iOS version or newer.
    static let minimumIOSVersion = 27

    static var isSupported: Bool {
        ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= minimumIOSVersion
    }

    private var host: DevicePairingHost?
    private var service: NetService?
    private let keepAlive = SilentAudioKeepAlive()

    func start() {
        guard !isActive else { return }
        guard Self.isSupported else {
            state = .failed("Built-in pairing needs iOS \(Self.minimumIOSVersion) or newer. Import a pairing file instead.")
            return
        }
        StikJITSetup.ensureDirectory()
        var problem: NSString?
        guard let host = DevicePairingHost(name: Self.hostName, failure: &problem) else {
            state = .failed((problem as String?) ?? "Pairing couldn't start.")
            return
        }
        self.host = host
        state = .waiting
        log("started, listening on port \(host.port)")

        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { _, _ in }
        keepAlive.start()

        let service = NetService(domain: "local.", type: DevicePairingHost.serviceType + ".",
                                 name: host.serviceName, port: Int32(host.port))
        service.setTXTRecord(NetService.data(fromTXTRecord: host.txtRecord))
        service.delegate = self
        service.publish()
        self.service = service

        let hostID = ObjectIdentifier(host)
        host.run(pin: { [weak self] pin in
            Task { @MainActor [weak self] in
                guard let self, self.host.map(ObjectIdentifier.init) == hostID else { return }
                self.state = .pin(pin)
                self.log("device asked for the PIN")
                Self.notify(title: "EspressU pairing", body: "Enter this PIN in Settings: \(pin)")
            }
        }, completion: { [weak self] file, deviceName, failure, cancelled in
            Task { @MainActor [weak self] in
                guard let self, self.host.map(ObjectIdentifier.init) == hostID else { return }
                self.finish(file: file, deviceName: deviceName, failure: failure, cancelled: cancelled)
            }
        })
    }

    func cancel() {
        guard isActive else { return }
        host?.cancel()
    }

    func dismissResult() {
        if !isActive { state = .idle }
    }

    private func finish(file: Data?, deviceName: String?, failure: String?, cancelled: Bool) {
        stopAdvertising()
        keepAlive.stop()
        host = nil
        if let file {
            do {
                try StikJITSetup.savePairingFile(file, name: "Paired in EspressU")
                StikJITCoordinator.shared.pairingFileChanged()
                state = .paired(deviceName)
                log("paired")
                Self.notify(title: "EspressU is paired", body: "Go back to EspressU. JIT can start now.")
            } catch {
                state = .failed("Paired, but the pairing file couldn't be saved: \(error.localizedDescription)")
            }
        } else if cancelled {
            state = .idle
            log("stopped")
        } else {
            let text = failure ?? "Pairing failed."
            state = .failed(text)
            log("failed: \(text)")
            Self.notify(title: "EspressU pairing failed", body: "Go back to EspressU to try again.")
        }
    }

    private func stopAdvertising() {
        service?.stop()
        service = nil
    }

    private static func notify(title: String, body: String) {
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default
        let request = UNNotificationRequest(identifier: "espressu.pairing", content: content, trigger: nil)
        UNUserNotificationCenter.current().add(request)
    }

    private func log(_ text: String) {
        NSLog("StikJIT pairing: %@", text)
    }
}

extension DevicePairing: NetServiceDelegate {
    nonisolated func netService(_ sender: NetService, didNotPublish errorDict: [String: NSNumber]) {
        let code = errorDict[NetService.errorCode]?.intValue ?? 0
        Task { @MainActor in
            guard self.service === sender, self.isActive else { return }
            self.host?.cancel()
            self.stopAdvertising()
            self.state = .failed("EspressU couldn't show itself to Settings (error \(code)). Check that Local Network is on for EspressU in Settings › Privacy & Security › Local Network, then try again.")
        }
    }
}

private final class SilentAudioKeepAlive {
    private var player: AVAudioPlayer?

    func start() {
        guard player == nil else { return }
        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try? session.setActive(true)
        guard let player = try? AVAudioPlayer(data: Self.silence) else { return }
        player.numberOfLoops = -1
        player.play()
        self.player = player
    }

    func stop() {
        player?.stop()
        player = nil
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    private static let silence: Data = {
        let sampleRate: UInt32 = 8000
        let samples = Data(count: Int(sampleRate) * 2)
        var data = Data()
        func append<T: FixedWidthInteger>(_ value: T) { withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) } }
        data.append(contentsOf: Array("RIFF".utf8)); append(UInt32(36 + samples.count))
        data.append(contentsOf: Array("WAVE".utf8))
        data.append(contentsOf: Array("fmt ".utf8)); append(UInt32(16)); append(UInt16(1)); append(UInt16(1))
        append(sampleRate); append(sampleRate * 2); append(UInt16(2)); append(UInt16(16))
        data.append(contentsOf: Array("data".utf8)); append(UInt32(samples.count))
        data.append(samples)
        return data
    }()
}
