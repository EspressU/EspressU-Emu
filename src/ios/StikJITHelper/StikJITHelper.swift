// SPDX-License-Identifier: MPL-2.0

import Foundation
import StikJIT

@objc(StikJITHelperHandler)
final class StikJITHelperHandler: NSObject, NSExtensionRequestHandling {
    private static let queue = DispatchQueue(label: "dev.espressu.EspressU.StikJITHelper", qos: .userInitiated)

    func beginRequest(with context: NSExtensionContext) {
        let item = context.inputItems.first as? NSExtensionItem
        let request = (item?.userInfo as? [String: Any]) ?? [:]
        Self.queue.async {
            let reply = HelperWork(request: request).run()
            let out = NSExtensionItem()
            out.userInfo = reply
            context.completeRequest(returningItems: [out], completionHandler: nil)
        }
    }
}

private final class HelperWork {
    private static let script: StikJIT.Script = .universal

    private let request: [String: Any]
    private let notifyPrefix: String?
    private var log: [String] = []
    private var lastStage = ""

    init(request: [String: Any]) {
        self.request = request
        notifyPrefix = request["notify"] as? String
    }

    private var ddiPaths: DDIPaths {
        let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0]
        let directory = library.appendingPathComponent("StikJIT", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory.appendingPathComponent("DDI"),
                                                 withIntermediateDirectories: true)
        return DDIPaths.default(in: directory)
    }

    func run() -> [String: Any] {
        switch request["action"] as? String ?? "" {
        case "status":
            return reply(ok: true)
        case "resetDDI":
            do {
                try StikJIT.resetCachedDDI(at: ddiPaths)
                return reply(ok: true)
            } catch {
                return reply(ok: false, reason: error.localizedDescription)
            }
        case "prepare":
            return withPairingFile { prepare(pairingFile: $0) }
        case "enable":
            return withPairingFile { enable(pairingFile: $0) }
        default:
            return reply(ok: false, reason: "The helper got a request it doesn't know.")
        }
    }

    private func prepare(pairingFile: URL) -> [String: Any] {
        let readiness = StikJIT.prepareDevice(pairingFile: pairingFile, paths: ddiPaths) { [self] stage in
            report(stage)
        }
        switch readiness {
        case .ready(let state):
            return reply(ok: true, readiness: "ready", txm: state.isTXMPresent)
        case .unreachable(let reason):
            return reply(ok: false, readiness: "unreachable", reason: reason)
        case .preparationFailed(let reason):
            return reply(ok: false, readiness: "notReady", reason: reason)
        @unknown default:
            return reply(ok: false, readiness: "notReady", reason: "Unknown device readiness. Update EspressU and try again.")
        }
    }

    private func enable(pairingFile: URL) -> [String: Any] {
        guard let pid = (request["pid"] as? NSNumber)?.int32Value, pid > 0 else {
            return reply(ok: false, reason: "EspressU didn't say which process to enable JIT for.")
        }
        let forceScript = request["forceScript"] as? Bool ?? false
        do {
            try StikJIT.enableJIT(targetPID: pid,
                                  pairingFile: pairingFile,
                                  ddiPaths: ddiPaths,
                                  script: Self.script,
                                  forceScript: forceScript,
                                  preparationProgress: { [self] stage in report(stage) },
                                  progress: { [self] line in
                                      post("script")
                                      remember(line)
                                  })
            return reply(ok: true, readiness: "ready")
        } catch {
            return reply(ok: false, reason: error.localizedDescription)
        }
    }

    private func withPairingFile(_ body: (URL) -> [String: Any]) -> [String: Any] {
        guard let data = request["pairing"] as? Data, !data.isEmpty else {
            return reply(ok: false, reason: "EspressU didn't send a pairing file.")
        }
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("pairing-\(UUID().uuidString).plist")
        do {
            try data.write(to: url, options: [.atomic, .completeFileProtection])
        } catch {
            return reply(ok: false, reason: "The helper couldn't keep the pairing file for this attempt: \(error.localizedDescription)")
        }
        defer { try? FileManager.default.removeItem(at: url) }
        return body(url)
    }

    private func report(_ stage: StikJIT.PreparationStage) {
        switch stage {
        case .checkingReachability:
            post("reachability")
        case .checkingDDI:
            post("checkingDDI")
        case .downloadingDDI(let fraction, let status):
            post("download.\(Int((min(max(fraction, 0), 1) * 10).rounded()))")
            remember(status)
        case .mountingDDI:
            post("mounting")
        case .verifyingDDI:
            post("verifying")
        case .ready:
            post("prepared")
        @unknown default:
            break
        }
    }

    private func post(_ stage: String) {
        guard let notifyPrefix, stage != lastStage else { return }
        lastStage = stage
        CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),
                                             CFNotificationName("\(notifyPrefix).\(stage)" as CFString),
                                             nil, nil, true)
    }

    private func remember(_ line: String) {
        guard !line.isEmpty else { return }
        log.append(line)
        if log.count > 40 { log.removeFirst(log.count - 40) }
    }

    private func reply(ok: Bool, readiness: String? = nil, reason: String? = nil,
                       txm: Bool?? = .none) -> [String: Any] {
        let present: Bool? = txm ?? StikJIT.isTXMPresent
        var reply: [String: Any] = [
            "ok": ok,
            "txm": present.map { $0 ? "present" : "absent" } ?? "unknown",
            "log": log,
        ]
        if let readiness { reply["readiness"] = readiness }
        if let reason { reply["reason"] = reason }
        return reply
    }
}
