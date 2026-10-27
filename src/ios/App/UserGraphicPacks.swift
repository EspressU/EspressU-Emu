// SPDX-License-Identifier: MPL-2.0

import Foundation

/// Mods ("graphic packs") the user brings along. EspressU ships none and downloads none:
/// a pack is a folder with a rules.txt that the user imports from Files.
enum UserGraphicPacks {
    struct Pack: Identifiable {
        let path: String
        let name: String
        let enabled: Bool
        let universal: Bool
        let titleIds: [String]
        var id: String { path }
    }

    enum ImportError: LocalizedError {
        case accessDenied
        case noRules
        case copyFailed(Error)

        var errorDescription: String? {
            switch self {
            case .accessDenied: return "Couldn't access that folder."
            case .noRules: return "That folder has no rules.txt, so it isn't a mod pack."
            case .copyFailed(let error): return "Couldn't copy the pack: \(error.localizedDescription)"
            }
        }
    }

    static var mlcPath: String? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.appendingPathComponent("mlc").path
    }

    static var packsFolder: URL? {
        guard let mlc = mlcPath else { return nil }
        return URL(fileURLWithPath: mlc).appendingPathComponent("graphicPacks", isDirectory: true)
    }

    static var isBusy: Bool { espressu_gp_title_running() }

    static func list() -> [Pack] {
        guard let mlc = mlcPath, espressu_gp_reload(mlc) else { return [] }
        let raw = String(cString: espressu_gp_list())
        guard !raw.isEmpty else { return [] }
        return raw.split(separator: "\u{1E}").compactMap { record in
            let f = record.split(separator: "\u{1F}", omittingEmptySubsequences: false)
            guard f.count >= 8 else { return nil }
            return Pack(path: String(f[0]), name: String(f[1]), enabled: f[3] == "1", universal: f[5] == "1",
                        titleIds: f[7].split(separator: ",").map(String.init))
        }
    }

    static func setEnabled(_ pack: Pack, _ enabled: Bool) {
        _ = pack.path.withCString { espressu_gp_set_enabled($0, enabled) }
    }

    /// Copies a pack folder (rules.txt inside, or in a subfolder) into the app's graphicPacks folder.
    static func importPack(from source: URL) throws {
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }
        let fm = FileManager.default
        guard let folder = packsFolder else { throw ImportError.accessDenied }
        var hasRules = fm.fileExists(atPath: source.appendingPathComponent("rules.txt").path)
        if !hasRules, let walker = fm.enumerator(at: source, includingPropertiesForKeys: nil) {
            for case let url as URL in walker where url.lastPathComponent == "rules.txt" { hasRules = true; break }
        }
        guard hasRules else { throw ImportError.noRules }
        try? fm.createDirectory(at: folder, withIntermediateDirectories: true)
        let destination = folder.appendingPathComponent(source.lastPathComponent, isDirectory: true)
        do {
            try? fm.removeItem(at: destination)
            try fm.copyItem(at: source, to: destination)
        } catch {
            throw ImportError.copyFailed(error)
        }
    }

    /// Removes the pack folder that holds the rules file of this pack.
    static func delete(_ pack: Pack) {
        guard let folder = packsFolder else { return }
        let fm = FileManager.default
        guard let walker = fm.enumerator(at: folder, includingPropertiesForKeys: nil) else { return }
        for case let url as URL in walker where url.lastPathComponent == "rules.txt" {
            let rulesFolder = url.deletingLastPathComponent()
            let relative = rulesFolder.path.replacingOccurrences(of: folder.path + "/", with: "")
            if pack.path.hasSuffix(relative) || pack.path.contains(relative) {
                try? fm.removeItem(at: rulesFolder)
                break
            }
        }
    }
}
