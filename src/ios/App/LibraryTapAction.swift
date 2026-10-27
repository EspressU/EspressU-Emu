// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

enum LibraryTapAction: String, CaseIterable, Identifiable {
    case openPage
    case playImmediately

    static let storageKey = "espressu.library.tapAction"
    static let defaultValue = LibraryTapAction.openPage

    var id: String { rawValue }

    var title: String {
        switch self {
        case .openPage: return "Open its page"
        case .playImmediately: return "Play immediately"
        }
    }

    static func current(raw: String) -> LibraryTapAction { LibraryTapAction(rawValue: raw) ?? defaultValue }
}
