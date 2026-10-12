import Foundation
import UniformTypeIdentifiers

enum GameSaveTransfer {

    private static var documents: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
    }

    private static var saveRoot: URL? {
        _ = migratedMisplacedSaves
        return documents?.appendingPathComponent("mlc/mlc01/usr/save", isDirectory: true)
    }

    private static let migratedMisplacedSaves: Void = {
        let fm = FileManager.default
        guard let docs = documents else { return }
        let wrongRoot = docs.appendingPathComponent("mlc/usr/save", isDirectory: true)
        let rightRoot = docs.appendingPathComponent("mlc/mlc01/usr/save", isDirectory: true)
        guard let highs = try? fm.contentsOfDirectory(at: wrongRoot, includingPropertiesForKeys: nil) else { return }
        for high in highs where high.hasDirectoryPath || (try? high.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
            guard let lows = try? fm.contentsOfDirectory(at: high, includingPropertiesForKeys: nil) else { continue }
            let rightHigh = existingChild(of: rightRoot, named: high.lastPathComponent)
                ?? rightRoot.appendingPathComponent(high.lastPathComponent, isDirectory: true)
            for low in lows where low.lastPathComponent != ".DS_Store" {
                if let existing = existingChild(of: rightHigh, named: low.lastPathComponent),
                   let entries = try? fm.contentsOfDirectory(atPath: existing.path),
                   !entries.filter({ $0 != ".DS_Store" }).isEmpty {
                    continue
                }
                if let empty = existingChild(of: rightHigh, named: low.lastPathComponent) {
                    try? fm.removeItem(at: empty)
                }
                try? fm.createDirectory(at: rightHigh, withIntermediateDirectories: true)
                try? fm.moveItem(at: low, to: rightHigh.appendingPathComponent(low.lastPathComponent, isDirectory: true))
            }
        }
    }()

    static func migrateMisplacedSavesIfNeeded() {
        _ = migratedMisplacedSaves
    }

    static func saveDirectory(for game: GameMetadata) -> URL? {
        guard let titleId = resolvedTitleId(for: game), let root = saveRoot else { return nil }
        let high = String(format: "%08X", UInt32(truncatingIfNeeded: titleId >> 32))
        let low = String(format: "%08X", UInt32(truncatingIfNeeded: titleId))
        let highDir = existingChild(of: root, named: high) ?? root.appendingPathComponent(high, isDirectory: true)
        return existingChild(of: highDir, named: low) ?? highDir.appendingPathComponent(low, isDirectory: true)
    }

    private static func resolvedTitleId(for game: GameMetadata) -> UInt64? {
        game.titleId ?? GameManager.deriveBaseTitleId(romPath: game.romPath)
    }

    private static func existingChild(of parent: URL, named name: String) -> URL? {
        guard let entries = try? FileManager.default.contentsOfDirectory(
            at: parent, includingPropertiesForKeys: [.isDirectoryKey]) else { return nil }
        return entries.first { $0.lastPathComponent.caseInsensitiveCompare(name) == .orderedSame }
    }

    static func hasSave(for game: GameMetadata) -> Bool {
        guard let dir = saveDirectory(for: game) else { return false }
        let entries = (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []
        return !entries.filter { $0 != ".DS_Store" }.isEmpty
    }

    static func export(_ game: GameMetadata, completion: @escaping (Result<String, Error>) -> Void) {
        guard let dir = saveDirectory(for: game), hasSave(for: game) else {
            completion(.failure(TransferError.noSaveYet))
            return
        }
        do {
            let staging = FileManager.default.temporaryDirectory
                .appendingPathComponent("save-export-\(UUID().uuidString)", isDirectory: true)
            try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
            let named = staging.appendingPathComponent(exportName(for: game), isDirectory: true)
            try FileManager.default.copyItem(at: dir, to: named)

            DocumentImport.presentExport([named]) { result in
                switch result {
                case .success(let urls):
                    guard let dest = urls.first else {
                        completion(.failure(TransferError.cancelled))
                        return
                    }
                    completion(.success("Saved to \(dest.lastPathComponent)."))
                case .failure(let error):
                    completion(.failure(error))
                }
            }
        } catch {
            completion(.failure(error))
        }
    }

    private static func exportName(for game: GameMetadata) -> String {
        let unsafe = CharacterSet(charactersIn: "/\\:*?\"<>|")
        let title = game.title.components(separatedBy: unsafe).joined(separator: "-")
        let trimmed = String(title.prefix(60)).trimmingCharacters(in: .whitespacesAndNewlines)
        let id = resolvedTitleId(for: game).map { String(format: "%016llX", $0) } ?? "unknown"
        return "\(trimmed.isEmpty ? "Wii U game" : trimmed) [\(id)] save"
    }

    private enum PickedShape {
        case titleFolder
        case userFolder
        case highFolder(titleFolder: URL)
    }

    private static func shape(of url: URL, preferredTitleFolder: String? = nil) -> PickedShape? {
        let fm = FileManager.default
        func hasDir(_ name: String) -> Bool {
            existingChild(of: url, named: name).map { u in
                (try? u.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
            } ?? false
        }
        if hasDir("user") { return .titleFolder }
        if hasDir("common") { return .userFolder }

        let children = (try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])) ?? []
        let hexDirs = children.filter {
            $0.lastPathComponent.count == 8
                && $0.lastPathComponent.allSatisfy(\.isHexDigit)
                && (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
        }
        let titleFolders = hexDirs.filter { existingChild(of: $0, named: "user") != nil }
        if let preferred = preferredTitleFolder,
           let match = titleFolders.first(where: { $0.lastPathComponent.caseInsensitiveCompare(preferred) == .orderedSame }) {
            return .highFolder(titleFolder: match)
        }
        if titleFolders.count == 1, let only = titleFolders.first {
            return .highFolder(titleFolder: only)
        }
        if titleFolders.count > 1 { return nil }
        if !hexDirs.isEmpty { return .userFolder }
        return nil
    }

    static func importSave(_ game: GameMetadata, from picked: URL) throws -> String {
        guard let destination = saveDirectory(for: game) else { throw TransferError.unknownTitleId }

        let scoped = picked.startAccessingSecurityScopedResource()
        defer { if scoped { picked.stopAccessingSecurityScopedResource() } }

        guard let shape = shape(of: picked, preferredTitleFolder: destination.lastPathComponent) else { throw TransferError.unrecognisedFolder }

        let fm = FileManager.default
        var backupNote = ""
        var movedAside: URL?
        if fm.fileExists(atPath: destination.path) {
            let stamp = ISO8601DateFormatter().string(from: Date())
                .replacingOccurrences(of: ":", with: "-")
            guard let docs = documents else { throw TransferError.unknownTitleId }
            let backups = docs.appendingPathComponent("save-backups/\(game.id)/\(stamp)", isDirectory: true)
            try fm.createDirectory(at: backups.deletingLastPathComponent(), withIntermediateDirectories: true)
            try fm.moveItem(at: destination, to: backups)
            movedAside = backups
            backupNote = " Your previous save was backed up first."
        }

        do {
            try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)

            switch shape {
            case .titleFolder:
                try fm.copyItem(at: picked, to: destination)
            case .highFolder(let titleFolder):
                try fm.copyItem(at: titleFolder, to: destination)
            case .userFolder:
                try fm.createDirectory(at: destination, withIntermediateDirectories: true)
                try fm.copyItem(at: picked, to: destination.appendingPathComponent("user", isDirectory: true))
            }
        } catch {
            try? fm.removeItem(at: destination)
            if let movedAside {
                try? fm.moveItem(at: movedAside, to: destination)
            }
            throw error
        }
        return "Save imported.\(backupNote) Start the game to check it."
    }

    enum TransferError: LocalizedError {
        case noSaveYet
        case unknownTitleId
        case unrecognisedFolder
        case cancelled

        var errorDescription: String? {
            switch self {
            case .noSaveYet:
                return "There's no save for this game yet. Play it once, then export."
            case .unknownTitleId:
                return "EspressU couldn't read this game's title ID, so it doesn't know where its save lives."
            case .unrecognisedFolder:
                return "That folder doesn't look like a Wii U save. Pick the folder named after the game's title ID, or the 'user' folder inside it."
            case .cancelled:
                return "Export cancelled."
            }
        }
    }
}
