// SPDX-License-Identifier: MPL-2.0

import UIKit

/// The front end's part of EspressU-Report.txt: every setting at game launch, and sharing the file.
enum SessionReport {
    /// Keys that hold accounts, pairing data or secrets stay out of a report that is meant to be sent around.
    private static let privateKeyParts = ["account", "pairing", "token", "password", "secret", "nnid", "pnid", "mii", "friend", "bookmark"]

    static func settingsText(gameID: String?, title: String?) -> String {
        let defaults = UserDefaults.standard.dictionaryRepresentation()
        var lines: [String] = []
        lines.append("Mode: \(SettingsMode.current.rawValue)")
        if let title { lines.append("Game: \(title)") }
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

    @MainActor
    static func share() {
        let items = files
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
