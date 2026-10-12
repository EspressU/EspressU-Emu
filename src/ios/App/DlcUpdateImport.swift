import Foundation

enum DlcUpdateImport {
    enum ContentKind {
        case dlc
        case update

        fileprivate var expectedTypeByte: Int32 {
            switch self {
            case .dlc: return 0x0C
            case .update: return 0x0E
            }
        }

        var displayName: String {
            switch self {
            case .dlc: return "DLC"
            case .update: return "update"
            }
        }
    }

    enum ImportError: LocalizedError {
        case accessDenied
        case copyFailed(Error)
        case invalidTitle(Int32)
        case wrongType(expected: ContentKind, actual: String)
        case noBaseGameMatch
        case alreadyInstalledSameOrNewer(installed: UInt16, imported: UInt16)
        case wuaReadFailed
        case noMatchingTitleInWua
        case wuaHasOtherContent
        case differentGame

        var errorDescription: String? {
            switch self {
            case .accessDenied:
                return "Couldn't access that file."
            case .copyFailed(let error):
                return "Couldn't copy the file: \(error.localizedDescription)"
            case .invalidTitle(let reason):
                switch reason {
                case 1:
                    return "That file couldn't be read."
                case 2:
                    return "This isn't a Wii U DLC or update file."
                case 3:
                    return "This file is encrypted and no matching key is installed. Add your keys.txt in Settings first."
                case 4:
                    return "This folder is missing title.tik (the ticket), which EspressU needs to decrypt it."
                case 5:
                    return "Meta files (app.xml, meta.xml, cos.xml) are missing or damaged."
                case 6:
                    return "This encrypted folder's title.tmd couldn't be read."
                case 7:
                    return "This encrypted folder's title.tik couldn't be read."
                case 8:
                    return "EspressU couldn't decrypt this folder. Its title.tik doesn't unlock the .app files."
                case 9:
                    return "This encrypted folder is missing one or more .app files listed in its title.tmd."
                default:
                    return "That file is corrupted or incomplete."
                }
            case .wrongType(let expected, let actual):
                return "This is \(actual), not \(expected == .dlc ? "DLC" : "an update"). Use the other import option."
            case .noBaseGameMatch:
                return "No game in your library matches this content."
            case .alreadyInstalledSameOrNewer(let installed, let imported):
                return "Version \(imported) isn't newer than what's already installed (version \(installed))."
            case .wuaReadFailed:
                return "The .wua file couldn't be read. It may be corrupted."
            case .noMatchingTitleInWua:
                return "The .wua doesn't contain a matching update or DLC for this game."
            case .wuaHasOtherContent:
                return "This .wua holds more than the one update or DLC for this game. Use a .wua with just that content."
            case .differentGame:
                return "This content is for a different game or region than the one you picked."
            }
        }
    }

    struct ImportedContent {
        let titleId: UInt64
        let baseTitleId: UInt64
        let matchedGame: GameMetadata?
    }

    private static let stagingDirectoryName = ".incoming-dlcupdate"

    private static func mlcRoot() -> URL? {
        _ = migratedMisplacedContent
        return FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("mlc")
    }

    private static let migratedMisplacedContent: Void = {
        let fm = FileManager.default
        guard let docs = fm.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        let wrongRoot = docs.appendingPathComponent("mlc/usr/title", isDirectory: true)
        let rightRoot = docs.appendingPathComponent("mlc/mlc01/usr/title", isDirectory: true)
        guard let highs = try? fm.contentsOfDirectory(at: wrongRoot, includingPropertiesForKeys: nil) else { return }
        for high in highs {
            guard let lows = try? fm.contentsOfDirectory(at: high, includingPropertiesForKeys: nil) else { continue }
            let rightHigh = rightRoot.appendingPathComponent(high.lastPathComponent, isDirectory: true)
            for low in lows {
                let target = rightHigh.appendingPathComponent(low.lastPathComponent, isDirectory: true)
                if fm.fileExists(atPath: target.path) { continue }
                try? fm.createDirectory(at: rightHigh, withIntermediateDirectories: true)
                try? fm.moveItem(at: low, to: target)
            }
        }
    }()

    static func migrateMisplacedContentIfNeeded() {
        _ = migratedMisplacedContent
    }

    private static func titleTypeName(forByte byte: Int32) -> String {
        switch byte {
        case 0x00: return "a base game"
        case 0x02: return "a demo"
        case 0x0E: return "an update"
        case 0x0C: return "DLC"
        case 0x0F: return "homebrew"
        default: return "not a recognizable game title"
        }
    }

    static func `import`(
        from source: URL,
        kind: ContentKind,
        library: [GameMetadata],
        manualMatch: GameMetadata? = nil,
        strictMatch: Bool = false,
        allowReinstall: Bool = false
    ) async throws -> ImportedContent {
        let scoped = source.startAccessingSecurityScopedResource()
        guard scoped || FileManager.default.isReadableFile(atPath: source.path) else {
            throw ImportError.accessDenied
        }
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }

        guard let mlcRoot = mlcRoot() else { throw ImportError.accessDenied }
        let fileManager = FileManager.default

        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: source.path, isDirectory: &isDirectory) else {
            throw ImportError.accessDenied
        }

        if !isDirectory.boolValue {
            let ext = (source.path as NSString).pathExtension.lowercased()
            guard ext == "wua" else {
                throw ImportError.accessDenied
            }
            return try await Task.detached {
                try importFromWua(
                    source: source, kind: kind, library: library, manualMatch: manualMatch,
                    allowReinstall: allowReinstall, mlcRoot: mlcRoot
                )
            }.value
        }

        let kindForFolder: GameManager.NUSFolderKind = kind == .dlc ? .dlc : .update
        let installSource = GameManager.nusSubfolder(in: source, kind: kindForFolder) ?? source

        return try await Task.detached {
            try copyInspectAndInstall(
                source: installSource, kind: kind, library: library, manualMatch: manualMatch,
                strictMatch: strictMatch, allowReinstall: allowReinstall, mlcRoot: mlcRoot
            )
        }.value
    }

    private static func copyInspectAndInstall(
        source: URL,
        kind: ContentKind,
        library: [GameMetadata],
        manualMatch: GameMetadata?,
        strictMatch: Bool,
        allowReinstall: Bool,
        mlcRoot: URL
    ) throws -> ImportedContent {
        let fileManager = FileManager.default
        let stagingRoot = mlcRoot.appendingPathComponent(stagingDirectoryName)
        try? fileManager.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
        let staged = stagingRoot.appendingPathComponent(source.lastPathComponent)

        do {
            if fileManager.fileExists(atPath: staged.path) {
                try fileManager.removeItem(at: staged)
            }
            try fileManager.copyItem(at: source, to: staged)
        } catch {
            try? fileManager.removeItem(at: staged)
            throw ImportError.copyFailed(error)
        }

        func cleanupStaged() { try? fileManager.removeItem(at: staged) }

        var titleId: UInt64 = 0
        var version: UInt16 = 0
        var region: Int32 = 0
        var invalidReason: Int32 = 0
        let valid = staged.path.withCString { cPath in
            cemu_bridge_inspect_title(cPath, &titleId, &version, &region, &invalidReason)
        }

        guard valid else {
            cleanupStaged()
            throw ImportError.invalidTitle(invalidReason)
        }

        let actualTypeByte = cemu_bridge_get_title_type(titleId)
        guard actualTypeByte == kind.expectedTypeByte else {
            cleanupStaged()
            throw ImportError.wrongType(expected: kind, actual: titleTypeName(forByte: actualTypeByte))
        }

        let baseTitleId = cemu_bridge_derive_base_title_id(titleId)

        let matchedGame: GameMetadata?
        if let manualMatch {
            matchedGame = manualMatch
        } else if let found = library.first(where: { $0.titleId == baseTitleId }) {
            matchedGame = found
        } else {
            matchedGame = nil
        }
        guard matchedGame != nil else {
            cleanupStaged()
            throw ImportError.noBaseGameMatch
        }
        if strictMatch, let manualMatch, manualMatch.titleId != baseTitleId {
            cleanupStaged()
            throw ImportError.differentGame
        }

        var upperHexBuf = [CChar](repeating: 0, count: 9)
        var lowerHexBuf = [CChar](repeating: 0, count: 9)
        cemu_bridge_get_mlc_title_path_components(titleId, &upperHexBuf, &lowerHexBuf)
        let upperHex = String(cString: upperHexBuf)
        let lowerHex = String(cString: lowerHexBuf)

        let destination = mlcRoot
            .appendingPathComponent("mlc01/usr/title")
            .appendingPathComponent(upperHex)
            .appendingPathComponent(lowerHex)

        if fileManager.fileExists(atPath: destination.path) {
            var existingVersion: UInt16 = 0
            let existingValid = destination.path.withCString { cPath in
                cemu_bridge_inspect_title(cPath, nil, &existingVersion, nil, nil)
            }
            if existingValid && !versionAllowed(installed: existingVersion, imported: version, allowReinstall: allowReinstall) {
                cleanupStaged()
                throw ImportError.alreadyInstalledSameOrNewer(installed: existingVersion, imported: version)
            }
        }
        if let wuaVersion = installedWuaVersion(kind: kind, baseTitleId: baseTitleId, mlcRoot: mlcRoot),
           !versionAllowed(installed: wuaVersion, imported: version, allowReinstall: allowReinstall) {
            cleanupStaged()
            throw ImportError.alreadyInstalledSameOrNewer(installed: wuaVersion, imported: version)
        }

        let backup = stagingRoot.appendingPathComponent(lowerHex + ".previous")
        var movedAside = false
        do {
            try fileManager.createDirectory(
                at: destination.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
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
        try? fileManager.removeItem(at: wuaCopyURL(kind: kind, baseTitleId: baseTitleId, mlcRoot: mlcRoot))

        return ImportedContent(titleId: titleId, baseTitleId: baseTitleId, matchedGame: matchedGame)
    }

    private static func versionAllowed(installed: UInt16, imported: UInt16, allowReinstall: Bool) -> Bool {
        imported > installed || (allowReinstall && imported == installed)
    }

    private static func wuaCopyURL(kind: ContentKind, baseTitleId: UInt64, mlcRoot: URL) -> URL {
        mlcRoot
            .appendingPathComponent("wua-content")
            .appendingPathComponent(String(format: "%016llx", baseTitleId))
            .appendingPathComponent(kind == .update ? "update.wua" : "dlc.wua")
    }

    private static func wuaTitles(at path: String) -> [(id: UInt64, version: UInt16)]? {
        var buffer = [CChar](repeating: 0, count: 64 * 1024)
        let count = cemu_bridge_wua_list_titles(path, &buffer, buffer.count)
        guard count > 0 else { return nil }
        var titles: [(id: UInt64, version: UInt16)] = []
        for line in String(cString: buffer).split(separator: "\n") {
            let parts = line.split(separator: " ")
            guard parts.count == 2, let id = UInt64(parts[0], radix: 16), let version = UInt16(parts[1]) else {
                return nil
            }
            titles.append((id, version))
        }
        return titles.isEmpty ? nil : titles
    }

    private static func installedWuaVersion(kind: ContentKind, baseTitleId: UInt64, mlcRoot: URL) -> UInt16? {
        let url = wuaCopyURL(kind: kind, baseTitleId: baseTitleId, mlcRoot: mlcRoot)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        return wuaTitles(at: url.path)?.first?.version ?? 0
    }

    private static func importFromWua(
        source: URL,
        kind: ContentKind,
        library: [GameMetadata],
        manualMatch: GameMetadata?,
        allowReinstall: Bool,
        mlcRoot: URL
    ) throws -> ImportedContent {
        let fileManager = FileManager.default
        let stagingRoot = mlcRoot.appendingPathComponent(stagingDirectoryName)
        try? fileManager.createDirectory(at: stagingRoot, withIntermediateDirectories: true)
        let staged = stagingRoot.appendingPathComponent("incoming-\(UUID().uuidString).wua")
        defer { try? fileManager.removeItem(at: staged) }

        do {
            try fileManager.copyItem(at: source, to: staged)
        } catch {
            throw ImportError.copyFailed(error)
        }

        guard let titles = wuaTitles(at: staged.path) else { throw ImportError.wuaReadFailed }

        let matching = titles.filter { cemu_bridge_get_title_type($0.id) == kind.expectedTypeByte }
        guard !matching.isEmpty else { throw ImportError.noMatchingTitleInWua }
        guard matching.count == titles.count, titles.count == 1 else { throw ImportError.wuaHasOtherContent }

        let title = titles[0]
        let baseTitleId = cemu_bridge_derive_base_title_id(title.id)
        let matchedGame: GameMetadata?
        if let manualMatch {
            guard manualMatch.titleId == baseTitleId else { throw ImportError.differentGame }
            matchedGame = manualMatch
        } else if let found = library.first(where: { $0.titleId == baseTitleId }) {
            matchedGame = found
        } else {
            throw ImportError.noBaseGameMatch
        }

        let folderVersion: UInt16? = {
            guard let destination = mlcDestination(forContentTitleId: title.id) else { return nil }
            var existing: UInt16 = 0
            let valid = destination.path.withCString { cemu_bridge_inspect_title($0, nil, &existing, nil, nil) }
            return valid ? existing : nil
        }()
        for existing in [folderVersion, installedWuaVersion(kind: kind, baseTitleId: baseTitleId, mlcRoot: mlcRoot)] {
            if let existing, !versionAllowed(installed: existing, imported: title.version, allowReinstall: allowReinstall) {
                throw ImportError.alreadyInstalledSameOrNewer(installed: existing, imported: title.version)
            }
        }

        let destination = wuaCopyURL(kind: kind, baseTitleId: baseTitleId, mlcRoot: mlcRoot)
        let backup = stagingRoot.appendingPathComponent("wua-\(kind == .update ? "update" : "dlc").previous")
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
            throw ImportError.copyFailed(error)
        }
        if movedAside { try? fileManager.removeItem(at: backup) }
        if let folder = mlcDestination(forContentTitleId: title.id) {
            try? fileManager.removeItem(at: folder)
        }

        return ImportedContent(titleId: title.id, baseTitleId: baseTitleId, matchedGame: matchedGame)
    }

    static func installedContent(for game: GameMetadata) -> (hasUpdate: Bool, hasDLC: Bool) {
        guard let baseTitleId = game.titleId else { return (false, false) }
        let root = mlcRoot()
        func wuaInstalled(_ kind: ContentKind) -> Bool {
            guard let root else { return false }
            return FileManager.default.fileExists(atPath: wuaCopyURL(kind: kind, baseTitleId: baseTitleId, mlcRoot: root).path)
        }
        return (
            hasUpdate: mlcDestination(forContentTitleId: cemu_bridge_derive_content_title_id(baseTitleId, true)) != nil
                || wuaInstalled(.update),
            hasDLC: mlcDestination(forContentTitleId: cemu_bridge_derive_content_title_id(baseTitleId, false)) != nil
                || wuaInstalled(.dlc)
        )
    }

    @discardableResult
    static func remove(kind: ContentKind, for game: GameMetadata) throws -> Bool {
        guard let baseTitleId = game.titleId else { return false }
        let contentTitleId = cemu_bridge_derive_content_title_id(baseTitleId, kind == .update)
        var removed = false
        if let root = mlcRoot() {
            let wua = wuaCopyURL(kind: kind, baseTitleId: baseTitleId, mlcRoot: root)
            if FileManager.default.fileExists(atPath: wua.path) {
                try FileManager.default.removeItem(at: wua)
                removed = true
            }
        }
        if let destination = mlcDestination(forContentTitleId: contentTitleId) {
            try FileManager.default.removeItem(at: destination)
            removed = true
        }
        return removed
    }

    static func installedFolder(kind: ContentKind, for game: GameMetadata) -> URL? {
        guard let baseTitleId = game.titleId else { return nil }
        return mlcDestination(forContentTitleId: cemu_bridge_derive_content_title_id(baseTitleId, kind == .update))
    }

    private static func mlcDestination(forContentTitleId titleId: UInt64) -> URL? {
        guard titleId != 0, let mlcRoot = mlcRoot() else { return nil }

        var upperHexBuf = [CChar](repeating: 0, count: 9)
        var lowerHexBuf = [CChar](repeating: 0, count: 9)
        cemu_bridge_get_mlc_title_path_components(titleId, &upperHexBuf, &lowerHexBuf)

        let destination = mlcRoot
            .appendingPathComponent("mlc01/usr/title")
            .appendingPathComponent(String(cString: upperHexBuf))
            .appendingPathComponent(String(cString: lowerHexBuf))
        return FileManager.default.fileExists(atPath: destination.path) ? destination : nil
    }
}
