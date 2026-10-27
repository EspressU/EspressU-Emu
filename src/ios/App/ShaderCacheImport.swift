import Foundation
import UniformTypeIdentifiers

enum ShaderCacheImport {
    enum Kind {
        case shaders
        case pipeline

        var bridgeKind: Int32 {
            switch self {
            case .shaders: return Int32(CEMU_CACHE_FILE_SHADERS.rawValue)
            case .pipeline: return Int32(CEMU_CACHE_FILE_PIPELINE.rawValue)
            }
        }
    }

    struct Outcome {
        let message: String
        let failed: Bool
    }

    static var pickerTypes: [UTType] {
        [UTType(filenameExtension: "bin"), .data].compactMap { $0 }
    }

    static func titleId(for game: GameMetadata) -> UInt64? {
        game.titleId ?? GameManager.deriveBaseTitleId(romPath: game.romPath)
    }

    static func isForeignPipeline(fileName: String) -> Bool {
        fileName.lowercased().contains("vkpipeline")
    }

    static func stage(_ picked: URL) throws -> URL {
        let scoped = picked.startAccessingSecurityScopedResource()
        defer { if scoped { picked.stopAccessingSecurityScopedResource() } }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("cache-import", isDirectory: true)
        try? FileManager.default.removeItem(at: folder)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let staged = folder.appendingPathComponent(UUID().uuidString + ".bin")
        try FileManager.default.copyItem(at: picked, to: staged)
        return staged
    }

    static func run(kind: Kind, game: GameMetadata, library: [GameMetadata], staged: URL, originalName: String) -> Outcome {
        defer { try? FileManager.default.removeItem(at: staged) }
        if kind == .pipeline && isForeignPipeline(fileName: originalName) {
            return Outcome(message: "\"\(originalName)\" is a pipeline cache from desktop Cemu. EspressU can't use those, only ones named ..._mtlpipeline.bin. Learned shaders from desktop Cemu can still be imported.", failed: true)
        }
        guard let titleId = titleId(for: game) else {
            return Outcome(message: "Couldn't read this game's title ID, so there's no cache to put the file in.", failed: true)
        }
        if cemu_bridge_is_title_running() {
            return Outcome(message: "Close the game first, then import.", failed: true)
        }
        let gameName = game.displayTitle ?? game.title

        var info = CemuCacheFileInfo()
        staged.path.withCString { cemu_bridge_cache_file_inspect(titleId, $0, &info) }

        switch Int(info.status) {
        case Int(CEMU_CACHE_STATUS_NOT_A_CACHE.rawValue):
            return Outcome(message: "\"\(originalName)\" isn't a shader cache file, or it's damaged. Pick a .bin file from a Cemu or EspressU shaderCache folder.", failed: true)
        case Int(CEMU_CACHE_STATUS_OTHER_GAME.rawValue):
            return Outcome(message: otherGameMessage(stamp: info.stamp, originalName: originalName, gameName: gameName, library: library), failed: true)
        case Int(CEMU_CACHE_STATUS_OLD_FORMAT.rawValue):
            return Outcome(message: "\"\(originalName)\" is a learned-shader cache from a desktop Cemu older than 1.16, which keyed shaders differently. It can't be converted. Use a cache from Cemu 1.16 or newer, or from EspressU.", failed: true)
        case Int(CEMU_CACHE_STATUS_NOTHING_USABLE.rawValue):
            return Outcome(message: "\"\(originalName)\" is a cache for \(gameName), but none of its \(info.entryCount) entries can be used.", failed: true)
        default:
            break
        }

        if Int32(info.kind) != kind.bridgeKind {
            return Outcome(message: kind == .shaders
                ? "\"\(originalName)\" is a pipeline cache, not learned shaders. Use \"Import shader pipeline caches\" for it."
                : "\"\(originalName)\" is a learned-shader cache, not a pipeline cache. Use \"Import learned shaders\" for it.", failed: true)
        }

        var added: Int32 = 0
        var already: Int32 = 0
        var skipped: Int32 = 0
        let result = staged.path.withCString {
            cemu_bridge_cache_file_import(titleId, $0, kind.bridgeKind,
                                          &added, &already, &skipped)
        }
        switch Int(result) {
        case Int(CEMU_CACHE_STATUS_OK.rawValue):
            break
        case Int(CEMU_CACHE_STATUS_TITLE_RUNNING.rawValue):
            return Outcome(message: "Close the game first, then import.", failed: true)
        case -2:
            return Outcome(message: "This game's own cache file was saved by a different version of the cache format, so nothing was changed. Clear this game's shaders in its options, then import again.", failed: true)
        case -1:
            return Outcome(message: "This game's own cache file couldn't be opened or created, so nothing was changed. If storage is full, free some up and try again.", failed: true)
        default:
            return Outcome(message: "Couldn't import \"\(originalName)\" (code \(result)). Nothing was changed.", failed: true)
        }

        return Outcome(message: successMessage(kind: kind, added: Int(added), already: Int(already), skipped: Int(skipped)),
                       failed: false)
    }

    private static func otherGameMessage(stamp: UInt32, originalName: String, gameName: String, library: [GameMetadata]) -> String {
        for other in library {
            guard let otherId = titleId(for: other), cemu_bridge_cache_stamp_kind(otherId, stamp) != 0 else { continue }
            let otherName = other.displayTitle ?? other.title
            if otherName != gameName {
                return "\"\(originalName)\" is a cache for \(otherName), not \(gameName). Nothing was imported."
            }
        }
        let digits = originalName.prefix(16)
        if digits.count == 16, digits.allSatisfy({ $0.isHexDigit }) {
            return "\"\(originalName)\" is a cache for another game (title ID \(digits.uppercased())), not \(gameName). Nothing was imported."
        }
        return "\"\(originalName)\" is a cache for a different game, not \(gameName). Nothing was imported."
    }

    private static func successMessage(kind: Kind, added: Int, already: Int, skipped: Int) -> String {
        let noun = kind == .shaders ? "shader" : "pipeline"
        let plural = { (n: Int) in "\(n) \(noun)\(n == 1 ? "" : "s")" }
        var parts: [String] = []
        if added == 0 && skipped == 0 {
            parts.append("Nothing new: this game already had all \(plural(already)) in that file.")
        } else if added == 0 {
            parts.append("Nothing was added.")
        } else {
            var line = "Added \(plural(added))"
            if already > 0 { line += " (\(already) already there)" }
            parts.append(line + ".")
        }
        if skipped > 0 {
            parts.append("\(skipped) \(skipped == 1 ? "entry was" : "entries were") left out: damaged, not in a usable format, or there wasn't room on this device. If storage is low, free some up and import the file again; entries already added are kept.")
        }
        parts.append("The file you picked wasn't changed.")
        switch kind {
        case .shaders:
            parts.append("Added to the Metal shader file. Shaders from desktop Cemu are converted the first time you start the game, which is a normal shader load; after that they load as fast as the game's own.")
        case .pipeline:
            parts.append("It's used the next time you start the game.")
            parts.append("A pipeline only loads when the shaders it uses are in the game's learned shaders, so import those too.")
        }
        return parts.joined(separator: " ")
    }
}
