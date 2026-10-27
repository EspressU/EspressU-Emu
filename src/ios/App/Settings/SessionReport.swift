// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

import UIKit

/// The front end's part of EspressU-Report.txt: every setting at game launch, and sharing the file.
enum SessionReport {
    /// Keys that hold accounts, pairing data or secrets stay out of a report that is meant to be sent around.
    private static let privateKeyParts = ["account", "token", "password", "secret", "mii", "friend", "bookmark"]

    static func settingsText(gameID: String?, title: String?) -> String {
        let defaults = UserDefaults.standard.dictionaryRepresentation()
        var lines: [String] = []
        lines.append("Mode: \(SettingsMode.current.rawValue)")
        for key in defaults.keys.sorted() where key.hasPrefix("espressu.") {
            let lower = key.lowercased()
            guard !privateKeyParts.contains(where: { lower.contains($0) }) else { continue }
            lines.append("\(key) = \(describe(defaults[key]))")
        }
        if let gameID {
            let overrides = PerGameSettingsStore.shared.activeOverrides(for: gameID)
            lines.append("Per-game overrides for this game: \(String(describing: overrides))")
        }
        return lines.joined(separator: "\n") + "\n"
    }

    private static func describe(_ value: Any?) -> String {
        guard let value else { return "nil" }
        if let data = value as? Data { return "<\(data.count) bytes>" }
        let text = String(describing: value).replacingOccurrences(of: "\n", with: " ")
        return text.count > 240 ? String(text.prefix(240)) + "…" : text
    }

    static func pushSettings(gameID: String?, title: String?) {
        settingsText(gameID: gameID, title: title).withCString { cemu_bridge_report_settings($0) }
    }

    /// The report of this session and of the one before it, whichever exist.
    static var files: [URL] {
        [String(cString: cemu_bridge_report_path()), String(cString: cemu_bridge_report_previous_path())]
            .filter { !$0.isEmpty && FileManager.default.fileExists(atPath: $0) }
            .map { URL(fileURLWithPath: $0) }
    }

    /// Removes anything that could name the user or their files: absolute paths and game file names.
    static func redact(_ text: String) -> String {
        let patterns = [
            #"file:///[^\s"'<>]*"#,
            #"/(?:var|private|Users|Library|tmp|Applications|System|usr|Volumes)/[^\s"'<>,;)\]]*"#,
            #"[^\s"'<>/\\]+\.(?:wua|rpx|wuhb|elf|wud|wux|iso|png|jpe?g)"#,
        ]
        var result = text
        for (index, pattern) in patterns.enumerated() {
            guard let expression = try? NSRegularExpression(pattern: pattern, options: [.caseInsensitive]) else { continue }
            let range = NSRange(result.startIndex..., in: result)
            result = expression.stringByReplacingMatches(in: result, options: [], range: range,
                                                         withTemplate: index == 2 ? "<file>" : "<path>")
        }
        return result
    }

    /// Temporary, redacted copies of the report files. The originals are never shared.
    static func redactedCopies() -> [URL] {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("EspressU-Report", isDirectory: true)
        try? FileManager.default.removeItem(at: folder)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return files.compactMap { url in
            guard let data = try? Data(contentsOf: url) else { return nil }
            let text = String(decoding: data.suffix(1_500_000), as: UTF8.self)
            let destination = folder.appendingPathComponent(url.lastPathComponent)
            do {
                try redact(text).write(to: destination, atomically: true, encoding: .utf8)
                return destination
            } catch {
                return nil
            }
        }
    }

    /// What the shared file contains, for the confirmation shown before it is shared.
    static let contentsDescription = "The report has your device model, iOS version, the EspressU version, your settings and an anonymous game ID, plus the app's log lines. File paths and game file names are removed. It is not sent anywhere: it only goes where you choose in the next step."

    @MainActor
    static func share() {
        let items = redactedCopies()
        guard !items.isEmpty,
              let scene = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene }).first,
              var top = scene.windows.first(where: { $0.isKeyWindow })?.rootViewController else { return }
        while let presented = top.presentedViewController { top = presented }
        let controller = UIActivityViewController(activityItems: items, applicationActivities: nil)
        controller.popoverPresentationController?.sourceView = top.view
        controller.popoverPresentationController?.sourceRect = CGRect(x: top.view.bounds.midX, y: top.view.bounds.midY, width: 1, height: 1)
        top.present(controller, animated: true)
    }
}
