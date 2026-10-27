import Foundation

/// Installs a decrypted update or DLC folder (code, content and meta folders) next to a game in the library.
/// Nothing is decrypted: folders that carry encrypted content (title.tmd and ticket files) are refused.
enum DlcUpdateImport {
    enum ContentKind {
        case dlc
        case update

        var displayName: String {
            switch self {
            case .dlc: return "DLC"
            case .update: return "update"
            }
        }

        fileprivate var highWord: UInt64 {
            switch self {
            case .dlc: return 0x0005000C
            case .update: return 0x0005000E
            }
        }
    }

    enum ImportError: LocalizedError {
        case accessDenied
        case copyFailed(Error)
        case encrypted
        case notAFolder
        case unreadable
        case notUpdateOrDLC
        case noBaseGameMatch
        case alreadyInstalledSameOrNewer(installed: UInt16, imported: UInt16)

        var errorDescription: String? {
            switch self {
            case .accessDenied:
                return "Couldn't access that folder."
            case .copyFailed(let error):
                return "Couldn't copy the folder: \(error.localizedDescription)"
            case .encrypted:
                return "This content is encrypted. EspressU only runs decrypted content. Prepare it with your own tools."
            case .notAFolder:
                return "Pick a decrypted update or DLC folder. It has code, content and meta folders inside."
            case .unreadable:
                return "That folder couldn't be read. The meta files may be missing or damaged."
            case .notUpdateOrDLC:
                return "This is not an update or DLC."
            case .noBaseGameMatch:
                return "No game in your library matches this content. Add the game first."
            case .alreadyInstalledSameOrNewer(let installed, let imported):
                return "Version \(imported) isn't newer than what's already installed (version \(installed))."
            }
        }
    }

    struct ImportedContent {
        let kind: ContentKind
        let matchedGame: GameMetadata
    }

    private static let stagingDirectoryName = ".incoming-content"

    private static func mlcRoot() -> URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.appendingPathComponent("mlc")
    }

    static func baseTitleId(forContentTitleId titleId: UInt64) -> UInt64 {
        0x0005000000000000 | (titleId & 0xFFFFFFFF)
    }

    private static func contentTitleId(base: UInt64, kind: ContentKind) -> UInt64 {
        (kind.highWord << 32) | (base & 0xFFFFFFFF)
    }

    private static func folder(forContentTitleId titleId: UInt64) -> URL? {
        guard let root = mlcRoot() else { return nil }
        return root.appendingPathComponent("mlc01/usr/title")
            .appendingPathComponent(String(format: "%08x", UInt32(titleId >> 32)))
            .appendingPathComponent(String(format: "%08x", UInt32(titleId & 0xFFFFFFFF)))
    }

    static func `import`(from source: URL, library: [GameMetadata], matching game: GameMetadata? = nil) async throws -> ImportedContent {
        let scoped = source.startAccessingSecurityScopedResource()
        guard scoped || FileManager.default.isReadableFile(atPath: source.path) else { throw ImportError.accessDenied }
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }
        guard let root = mlcRoot() else { throw ImportError.accessDenied }

        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: source.path, isDirectory: &isDirectory), isDirectory.boolValue else {
            throw ImportError.notAFolder
        }
        return try await Task.detached {
            try copyInspectAndInstall(source: source, library: library, matching: game, mlcRoot: root)
        }.value
    }

    private static func copyInspectAndInstall(source: URL, library: [GameMetadata], matching game: GameMetadata?, mlcRoot: URL) throws -> ImportedContent {
        let fileManager = FileManager.default
        if GameManager.looksEncrypted(source) { throw ImportError.encrypted }
        guard GameManager.looksLikeGameFolder(source) else { throw ImportError.notAFolder }

        let stagingRoot = mlcRoot.appendingPathComponent(stagingDirectoryName)
        try? fileManager.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
        let staged = stagingRoot.appendingPathComponent(source.lastPathComponent)
        func cleanupStaged() { try? fileManager.removeItem(at: staged) }

        do {
            try? fileManager.removeItem(at: staged)
            try fileManager.copyItem(at: source, to: staged)
        } catch {
            cleanupStaged()
            throw ImportError.copyFailed(error)
        }

        var titleId: UInt64 = 0
        var version: UInt16 = 0
        var region: Int32 = 0
        var invalidReason: Int32 = 0
        let valid = staged.path.withCString { cemu_bridge_inspect_title($0, &titleId, &version, &region, &invalidReason) }
        guard valid else {
            cleanupStaged()
            throw ImportError.unreadable
        }

        let kind: ContentKind
        switch UInt64(titleId >> 32) {
        case ContentKind.update.highWord: kind = .update
        case ContentKind.dlc.highWord: kind = .dlc
        default:
            cleanupStaged()
            throw ImportError.notUpdateOrDLC
        }

        let base = baseTitleId(forContentTitleId: titleId)
        guard let matched = game.flatMap({ $0.titleId == base ? $0 : nil }) ?? (game == nil ? library.first(where: { $0.titleId == base }) : nil) else {
            cleanupStaged()
            throw ImportError.noBaseGameMatch
        }

        guard let destination = folder(forContentTitleId: titleId) else {
            cleanupStaged()
            throw ImportError.accessDenied
        }
        if fileManager.fileExists(atPath: destination.path) {
            var existing: UInt16 = 0
            let existingValid = destination.path.withCString { cemu_bridge_inspect_title($0, nil, &existing, nil, nil) }
            if existingValid && version <= existing {
                cleanupStaged()
                throw ImportError.alreadyInstalledSameOrNewer(installed: existing, imported: version)
            }
        }

        let backup = stagingRoot.appendingPathComponent(destination.lastPathComponent + ".previous")
        var movedAside = false
        do {
            try fileManager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            if fileManager.fileExists(atPath: destination.path) {
                try? fileManager.removeItem(at: backup)
                try fileManager.moveItem(at: destination, to: backup)
                movedAside = true
            }
            try fileManager.moveItem(at: staged, to: destination)
        } catch {
            if movedAside && !fileManager.fileExists(atPath: destination.path) {
                try? fileManager.moveItem(at: backup, to: destination)
            }
            cleanupStaged()
            throw ImportError.copyFailed(error)
        }
        if movedAside { try? fileManager.removeItem(at: backup) }
        return ImportedContent(kind: kind, matchedGame: matched)
    }

    static func installedContent(for game: GameMetadata) -> (hasUpdate: Bool, hasDLC: Bool) {
        guard let base = game.titleId else { return (false, false) }
        func exists(_ kind: ContentKind) -> Bool {
            guard let url = folder(forContentTitleId: contentTitleId(base: base, kind: kind)) else { return false }
            return FileManager.default.fileExists(atPath: url.path)
        }
        return (exists(.update), exists(.dlc))
    }

    static func remove(kind: ContentKind, for game: GameMetadata) throws {
        guard let base = game.titleId,
              let url = folder(forContentTitleId: contentTitleId(base: base, kind: kind)),
              FileManager.default.fileExists(atPath: url.path) else { return }
        try FileManager.default.removeItem(at: url)
    }
}
