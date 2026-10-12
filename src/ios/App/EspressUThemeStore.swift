// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Combine

struct EspressUThemeDefinition: Identifiable, Equatable {
    let id: String
    let name: String
    let iconId: String

    let backgroundTopLight: String, backgroundTopDark: String
    let backgroundBottomLight: String, backgroundBottomDark: String
    let cremaTopLightLight: String, cremaTopLightDark: String
    let cremaTopDarkLight: String, cremaTopDarkDark: String
    let creamLight: String, creamDark: String
    let wrapperLight: String, wrapperDark: String
    let blueberryNavyLight: String, blueberryNavyDark: String
    let pixelBlueLight: String, pixelBlueDark: String
    let blushPinkLight: String, blushPinkDark: String
    let brownDarkestLight: String, brownDarkestDark: String
    let brownDarkLight: String, brownDarkDark: String
    let brownMidLight: String, brownMidDark: String
    let sparkleCreamLight: String, sparkleCreamDark: String
    let shadowLight: String, shadowDark: String

    var backgroundStopsLight: [String] = []
    var backgroundStopsDark: [String] = []

    var backgroundStopLocations: [Double] = []

    static func == (lhs: EspressUThemeDefinition, rhs: EspressUThemeDefinition) -> Bool { lhs.id == rhs.id }
}

final class EspressUThemeStore: ObservableObject {
    static let shared = EspressUThemeStore()

    @Published private(set) var current: EspressUThemeDefinition

    private static let storageKey = "espressu.theme.selectedId"

    private init() {
        let storedId = UserDefaults.standard.string(forKey: Self.storageKey)
        current = EspressUThemePresets.all.first(where: { $0.id == storedId }) ?? EspressUThemePresets.espressU
    }

    func select(_ theme: EspressUThemeDefinition) {
        guard theme.id != current.id else { return }
        current = theme
        UserDefaults.standard.set(theme.id, forKey: Self.storageKey)
    }
}
