// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import UIKit

struct CoverContext {
    let gameID: String
    let romPath: String
    let dumpDirectoryPath: String?
    let libraryDirectory: URL
    let currentCoverPath: String?
    var region: String? = nil
    var titles: [String] = []
}

protocol CoverSource {
    var id: String { get }
    func cachedPath(_ context: CoverContext) -> String?
    func needsAcquire(_ context: CoverContext) -> Bool
    func acquire(_ context: CoverContext) async -> String?
}

extension CoverSource {
    func needsAcquire(_ context: CoverContext) -> Bool { false }
    func acquire(_ context: CoverContext) async -> String? { nil }
}

enum CoverSourceChain {
    static var sources: [CoverSource] {
        [GameTDBHQCoverSource(), GameTDBStandardCoverSource(), ExtractedIconCoverSource()]
    }

    static func cachedPath(_ context: CoverContext) -> String? {
        for source in sources {
            if let path = source.cachedPath(context) { return path }
        }
        return nil
    }

    static func acquireMissing(_ context: CoverContext) async -> String? {
        for source in sources where source.needsAcquire(context) {
            if let path = await source.acquire(context) { return path }
        }
        return nil
    }
}

struct GameTDBHQCoverSource: CoverSource {
    let id = "gametdb-hq"
    func cachedPath(_ c: CoverContext) -> String? { CoverArtFetcher.cachedHQCoverPath(for: c.gameID, in: c.libraryDirectory) }
    func needsAcquire(_ c: CoverContext) -> Bool {
        CoverArtFetcher.shouldAttemptFetch(gameID: c.gameID, romPath: c.romPath, in: c.libraryDirectory)
    }
    func acquire(_ c: CoverContext) async -> String? {
        await CoverArtFetcher.fetchAndCache(gameID: c.gameID, romPath: c.romPath, in: c.libraryDirectory)
    }
}

struct GameTDBStandardCoverSource: CoverSource {
    let id = "gametdb-standard"
    func cachedPath(_ c: CoverContext) -> String? { CoverArtFetcher.cachedStandardCoverPath(for: c.gameID, in: c.libraryDirectory) }
}

struct ExtractedIconCoverSource: CoverSource {
    let id = "extracted-icon"
    func cachedPath(_ c: CoverContext) -> String? { WiiUIcon.existingCachedIconPath(for: c.gameID, in: c.libraryDirectory) }
    func needsAcquire(_ c: CoverContext) -> Bool {
        (c.currentCoverPath == nil || GenericCover.isGeneric(c.currentCoverPath)) && c.dumpDirectoryPath != nil
    }
    func acquire(_ c: CoverContext) async -> String? {
        guard let dump = c.dumpDirectoryPath else { return nil }
        return WiiUIcon.cachedIconPath(for: c.gameID, dump: URL(fileURLWithPath: dump), in: c.libraryDirectory)
    }
}

enum GenericCover {
    static func isGeneric(_ path: String?) -> Bool {
        guard let path else { return false }
        return (path as NSString).deletingLastPathComponent == ArtLocations.genericDirectory.path
    }
}
