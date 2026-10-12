// SPDX-License-Identifier: MPL-2.0

import Foundation

enum EspressUInbox {
    static let folderName = "Updates & DLC"

    struct Report {
        var added: [String] = []
        var failed: [String] = []
        var isEmpty: Bool { added.isEmpty && failed.isEmpty }
    }

    static var folderURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent(folderName, isDirectory: true)
    }

    static func ensureFolderExists() {
        guard let url = folderURL else { return }
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    @MainActor private static var running = false
    @MainActor private static var failedThisLaunch: Set<String> = []

    @MainActor
    static func process(library: [GameMetadata]) async -> Report {
        ensureFolderExists()
        guard !running, let folder = folderURL else { return Report() }
        running = true
        defer { running = false }

        let fm = FileManager.default
        let items = ((try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { !$0.lastPathComponent.hasPrefix(".") }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        var report = Report()

        for item in items {
            let name = item.lastPathComponent
            let stamp = (try? fm.attributesOfItem(atPath: item.path)[.modificationDate] as? Date)?
                .timeIntervalSince1970 ?? 0
            let key = "\(name)|\(stamp)"
            guard !failedThisLaunch.contains(key) else { continue }
            cemu_bridge_log_checkpoint("Updates & DLC folder: installing \(name)")
            do {
                let result = try await installEither(item, library: library)
                let game = result.content.matchedGame?.title ?? "your game"
                report.added.append("\(result.kind == .update ? "Update" : "DLC") for \"\(game)\"")
                try? fm.removeItem(at: item)
            } catch {
                failedThisLaunch.insert(key)
                report.failed.append("\(name): \(error.localizedDescription)")
                cemu_bridge_log_checkpoint("Updates & DLC folder: \(name) failed: \(error.localizedDescription)")
            }
        }
        return report
    }

    private static func installEither(
        _ item: URL,
        library: [GameMetadata]
    ) async throws -> (kind: DlcUpdateImport.ContentKind, content: DlcUpdateImport.ImportedContent) {
        do {
            return (.update, try await DlcUpdateImport.import(from: item, kind: .update, library: library))
        } catch DlcUpdateImport.ImportError.wrongType {
            return (.dlc, try await DlcUpdateImport.import(from: item, kind: .dlc, library: library))
        }
    }

    static func summary(_ report: Report) -> String {
        var lines: [String] = []
        if !report.added.isEmpty { lines.append("Installed: " + report.added.joined(separator: ", ") + ".") }
        if !report.failed.isEmpty {
            lines.append("Not installed (still in the Updates & DLC folder):\n" + report.failed.joined(separator: "\n"))
        }
        return lines.joined(separator: "\n\n")
    }
}
