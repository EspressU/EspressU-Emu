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

/// Covers come from the game's own icon data. Nothing is downloaded.
/// A picture the user picks from Files is stored as `<id>_cover.<ext>` and wins over this.
enum CoverSourceChain {
    static func cachedPath(_ context: CoverContext) -> String? {
        GameIcon.existingCachedIconPath(for: context.gameID, in: context.libraryDirectory)
    }

    static func acquireMissing(_ context: CoverContext) async -> String? {
        guard context.currentCoverPath == nil, let dump = context.dumpDirectoryPath else { return nil }
        return GameIcon.cachedIconPath(for: context.gameID, dump: URL(fileURLWithPath: dump), in: context.libraryDirectory)
    }
}
