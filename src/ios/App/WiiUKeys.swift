import Foundation

enum WiiUKeys {
    static var directoryURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("keys")
    }

    static var fileURL: URL? {
        directoryURL?.appendingPathComponent("keys.txt")
    }

    private static var engineFileURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("mlc")
            .appendingPathComponent("keys.txt")
    }

    static func ensureDirectoryExists() {
        guard let directoryURL else { return }
        try? FileManager.default.createDirectory(at: directoryURL, withIntermediateDirectories: true)
    }

    enum ImportError: LocalizedError {
        case accessDenied
        case unreadable
        case noKeysFound
        case tooLarge
        case writeFailed(Error)

        var errorDescription: String? {
            switch self {
            case .accessDenied:
                return "Couldn't access that file."
            case .unreadable:
                return "That file isn't readable text - keys.txt is a plain text file with one key per line."
            case .noKeysFound:
                return "No keys found in that file. Each key is 32 hex characters on its own line; anything after a # is a comment."
            case .tooLarge:
                return "That file is far too big to be a keys.txt."
            case .writeFailed(let error):
                return "Couldn't save keys.txt: \(error.localizedDescription)"
            }
        }
    }

    private static let maximumFileSize = 1 << 20

    static func usableKeyCount(in text: String) -> Int {
        var count = 0
        for rawLine in text.split(whereSeparator: \.isNewline) {
            var line = Substring(rawLine)
            if let commentStart = line.firstIndex(where: { $0 == "#" || $0 == ";" }) {
                line = line[line.startIndex..<commentStart]
            }
            let stripped = line.filter { $0 != " " && $0 != "\t" && $0 != "-" && $0 != "_" }
            guard stripped.count == 32 else { continue }
            guard stripped.allSatisfy({ $0.isHexDigit }) else { continue }
            count += 1
        }
        return count
    }

    static func installedKeyCount() -> Int {
        guard let fileURL, let text = try? String(contentsOf: fileURL, encoding: .utf8) else { return 0 }
        return usableKeyCount(in: text)
    }

    static func keysFileExists() -> Bool {
        guard let fileURL else { return false }
        return FileManager.default.fileExists(atPath: fileURL.path)
    }

    @discardableResult
    static func importKeys(from source: URL) throws -> Int {
        guard source.startAccessingSecurityScopedResource() else {
            throw ImportError.accessDenied
        }
        defer { source.stopAccessingSecurityScopedResource() }

        let attributes = try? FileManager.default.attributesOfItem(atPath: source.path)
        if let size = attributes?[.size] as? Int, size > maximumFileSize {
            throw ImportError.tooLarge
        }

        guard let data = try? Data(contentsOf: source) else {
            throw ImportError.accessDenied
        }
        guard let text = String(data: data, encoding: .utf8)
                ?? String(data: data, encoding: .isoLatin1) else {
            throw ImportError.unreadable
        }

        let count = usableKeyCount(in: text)
        guard count > 0 else { throw ImportError.noKeysFound }

        guard let directoryURL, let fileURL else { throw ImportError.accessDenied }
        do {
            try FileManager.default.createDirectory(at: directoryURL, withIntermediateDirectories: true)
            try data.write(to: fileURL, options: .atomic)
        } catch {
            throw ImportError.writeFailed(error)
        }
        return count
    }

    static func removeKeys() throws {
        let manager = FileManager.default
        var firstError: Error?
        for url in [fileURL, engineFileURL].compactMap({ $0 }) {
            guard manager.fileExists(atPath: url.path) else { continue }
            do {
                try manager.removeItem(at: url)
            } catch {
                if firstError == nil { firstError = error }
            }
        }
        if let firstError { throw firstError }
    }
}
