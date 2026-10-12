// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum CoverArtFetcher {
    private static let cacheDirectoryName = ".boxart"

    private static let regions = ["US", "EN", "JA", "DE", "FR"]
    private static let extensions = ["jpg", "png"]

    private enum Tier: String, CaseIterable {
        case hq = "coverHQ"
        case standard = "cover"
    }

    private static func hqPrefix(_ gameID: String) -> String { "\(gameID).hq" }

    private static func notFoundMarkerPath(for gameID: String, in libraryDirectory: URL) -> URL {
        libraryDirectory.appendingPathComponent(cacheDirectoryName).appendingPathComponent("\(gameID).notfound")
    }

    private static func noHQMarkerPath(for gameID: String, in libraryDirectory: URL) -> URL {
        libraryDirectory.appendingPathComponent(cacheDirectoryName).appendingPathComponent("\(gameID).nohq")
    }

    private static let notFoundRetryInterval: TimeInterval = 30 * 24 * 3600
    private static let networkBackoff: TimeInterval = 120

    private static func existing(_ name: String, in dir: URL) -> String? {
        let p = dir.appendingPathComponent(name).path
        return FileManager.default.fileExists(atPath: p) ? p : nil
    }

    static func cachedHQCoverPath(for gameID: String, in dir: URL) -> String? { cachedHQPath(for: gameID, in: dir) }
    static func cachedStandardCoverPath(for gameID: String, in dir: URL) -> String? { cachedStandardPath(for: gameID, in: dir) }

    private static func cachedHQPath(for gameID: String, in libraryDirectory: URL) -> String? {
        let dir = libraryDirectory.appendingPathComponent(cacheDirectoryName)
        for ext in extensions { if let p = existing("\(hqPrefix(gameID)).\(ext)", in: dir) { return p } }
        return nil
    }

    private static func cachedStandardPath(for gameID: String, in libraryDirectory: URL) -> String? {
        let dir = libraryDirectory.appendingPathComponent(cacheDirectoryName)
        for ext in extensions { if let p = existing("\(gameID).\(ext)", in: dir) { return p } }
        return nil
    }

    private static func cachedImagePath(for gameID: String, in libraryDirectory: URL) -> String? {
        cachedHQPath(for: gameID, in: libraryDirectory) ?? cachedStandardPath(for: gameID, in: libraryDirectory)
    }

    private actor RequestGate {
        private var busy = false
        private var waiters: [CheckedContinuation<Void, Never>] = []
        func acquire() async {
            if !busy { busy = true; return }
            await withCheckedContinuation { waiters.append($0) }
        }
        func release() {
            if waiters.isEmpty { busy = false } else { waiters.removeFirst().resume() }
        }
    }
    private static let gate = RequestGate()
    private static let requestSpacing: UInt64 = 300_000_000

    private final class NetworkState {
        private let lock = NSLock()
        private var failedAt: Date?
        func noteFailure() { lock.lock(); failedAt = Date(); lock.unlock() }
        func noteSuccess() { lock.lock(); failedAt = nil; lock.unlock() }
        func isBackingOff(_ interval: TimeInterval) -> Bool {
            lock.lock(); defer { lock.unlock() }
            guard let f = failedAt else { return false }
            return Date().timeIntervalSince(f) < interval
        }
    }
    private static let networkState = NetworkState()

    static func deriveGameTdbId(romPath: String) -> String? {
        var buffer = [CChar](repeating: 0, count: 7)
        let ok = romPath.withCString { cPath in
            buffer.withUnsafeMutableBufferPointer { buf in
                cemu_bridge_derive_gametdb_id(cPath, buf.baseAddress, buf.count)
            }
        }
        guard ok else { return nil }
        return String(cString: buffer)
    }

    static func resolvedTdbId(gameID: String, romPath: String) -> String? {
        GameDataStore.shared.coverTdbID(gameID: gameID, derived: deriveGameTdbId(romPath: romPath))
    }

    static func clearCache(gameID: String, in libraryDirectory: URL) {
        let dir = libraryDirectory.appendingPathComponent(cacheDirectoryName)
        for ext in extensions {
            try? FileManager.default.removeItem(at: dir.appendingPathComponent("\(hqPrefix(gameID)).\(ext)"))
            try? FileManager.default.removeItem(at: dir.appendingPathComponent("\(gameID).\(ext)"))
        }
        try? FileManager.default.removeItem(at: notFoundMarkerPath(for: gameID, in: libraryDirectory))
        try? FileManager.default.removeItem(at: noHQMarkerPath(for: gameID, in: libraryDirectory))
    }

    static func cachedCoverPath(for gameID: String, romPath: String, in libraryDirectory: URL) -> String? {
        cachedImagePath(for: gameID, in: libraryDirectory)
    }

    static func shouldAttemptFetch(gameID: String, romPath: String, in libraryDirectory: URL) -> Bool {
        let fm = FileManager.default
        if networkState.isBackingOff(networkBackoff) { return false }
        for ext in ["jpg", "jpeg", "png"] where fm.fileExists(atPath: libraryDirectory.appendingPathComponent("\(gameID)_cover.\(ext)").path) {
            return false
        }
        if cachedHQPath(for: gameID, in: libraryDirectory) != nil { return false }
        if cachedStandardPath(for: gameID, in: libraryDirectory) != nil {
            if fm.fileExists(atPath: noHQMarkerPath(for: gameID, in: libraryDirectory).path) { return false }
        } else {
            let marker = notFoundMarkerPath(for: gameID, in: libraryDirectory).path
            if let attrs = try? fm.attributesOfItem(atPath: marker),
               let when = attrs[.modificationDate] as? Date,
               Date().timeIntervalSince(when) < notFoundRetryInterval {
                return false
            }
        }
        return resolvedTdbId(gameID: gameID, romPath: romPath) != nil
    }

    enum LookupError: LocalizedError {
        case network(Error)
        var errorDescription: String? {
            switch self {
            case .network(let error):
                return "Couldn't reach GameTDB: \(error.localizedDescription)"
            }
        }
    }

    static func fetchArt(forGameTdbId tdbId: String) async throws -> (data: Data, ext: String, isHQ: Bool)? {
        try await fetchArt(forGameTdbId: tdbId, tiers: Tier.allCases)
    }

    private static func fetchArt(forGameTdbId tdbId: String, tiers: [Tier]) async throws -> (data: Data, ext: String, isHQ: Bool)? {
        guard tdbId.range(of: "^[A-Za-z0-9]{4,6}$", options: .regularExpression) != nil else { return nil }
        for tier in tiers {
            for region in regions {
                for ext in extensions {
                    guard let url = URL(string: "https://art.gametdb.com/wiiu/\(tier.rawValue)/\(region)/\(tdbId).\(ext)") else { continue }
                    do {
                        if let data = try await fetch(url), isImage(data) {
                            networkState.noteSuccess()
                            return (data, ext, tier == .hq)
                        }
                    } catch {
                        networkState.noteFailure()
                        throw LookupError.network(error)
                    }
                }
            }
        }
        networkState.noteSuccess()
        return nil
    }

    static func fetchAndCache(gameID: String, romPath: String, in libraryDirectory: URL) async -> String? {
        guard let tdbId = resolvedTdbId(gameID: gameID, romPath: romPath) else { return nil }

        let cacheDirectory = libraryDirectory.appendingPathComponent(cacheDirectoryName)
        try? FileManager.default.createDirectory(at: cacheDirectory, withIntermediateDirectories: true)

        let upgrading = cachedStandardPath(for: gameID, in: libraryDirectory) != nil
        let found: (data: Data, ext: String, isHQ: Bool)?
        do {
            found = try await fetchArt(forGameTdbId: tdbId, tiers: upgrading ? [.hq] : Tier.allCases)
        } catch {
            return nil
        }

        guard let found else {
            if upgrading {
                FileManager.default.createFile(atPath: noHQMarkerPath(for: gameID, in: libraryDirectory).path, contents: nil)
            } else {
                FileManager.default.createFile(atPath: notFoundMarkerPath(for: gameID, in: libraryDirectory).path, contents: nil)
            }
            return nil
        }

        let name = found.isHQ ? "\(hqPrefix(gameID)).\(found.ext)" : "\(gameID).\(found.ext)"
        let cached = cacheDirectory.appendingPathComponent(name)
        guard (try? found.data.write(to: cached, options: .atomic)) != nil else { return nil }
        try? FileManager.default.removeItem(at: notFoundMarkerPath(for: gameID, in: libraryDirectory))
        if found.isHQ {
            for ext in extensions { try? FileManager.default.removeItem(at: cacheDirectory.appendingPathComponent("\(gameID).\(ext)")) }
            try? FileManager.default.removeItem(at: noHQMarkerPath(for: gameID, in: libraryDirectory))
        } else {
            FileManager.default.createFile(atPath: noHQMarkerPath(for: gameID, in: libraryDirectory).path, contents: nil)
        }
        return cached.path
    }

    private static func isImage(_ data: Data) -> Bool {
        let b = [UInt8](data.prefix(4))
        return b.starts(with: [0xFF, 0xD8, 0xFF]) || b == [0x89, 0x50, 0x4E, 0x47]
    }

    private static func fetch(_ url: URL) async throws -> Data? {
        await gate.acquire()
        defer { Task { try? await Task.sleep(nanoseconds: requestSpacing); await gate.release() } }
        var request = URLRequest(url: url)
        request.setValue("EspressU", forHTTPHeaderField: "User-Agent")
        request.timeoutInterval = 30
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        switch http.statusCode {
        case 200: return data
        case 404: return nil
        default: throw URLError(.badServerResponse)
        }
    }
}
