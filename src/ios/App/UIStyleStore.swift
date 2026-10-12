// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Combine

final class UIStyleStore: ObservableObject {
    static let shared = UIStyleStore()

    static let classicUIKey = "espressu.ui.classic"
    static let disableLiquidGlassKey = "espressu.ui.disableLiquidGlass"

    static let classicUIDefault = false
    static let disableLiquidGlassDefault = false

    @Published var useClassicUI: Bool {
        didSet {
            guard oldValue != useClassicUI else { return }
            UserDefaults.standard.set(useClassicUI, forKey: Self.classicUIKey)
        }
    }

    @Published var disableLiquidGlass: Bool {
        didSet {
            guard oldValue != disableLiquidGlass else { return }
            UserDefaults.standard.set(disableLiquidGlass, forKey: Self.disableLiquidGlassKey)
        }
    }

    private init() {
        let defaults = UserDefaults.standard
        useClassicUI = defaults.object(forKey: Self.classicUIKey) as? Bool ?? Self.classicUIDefault
        disableLiquidGlass = defaults.object(forKey: Self.disableLiquidGlassKey) as? Bool ?? Self.disableLiquidGlassDefault
    }

    func reloadFromDefaults() {
        let defaults = UserDefaults.standard
        let classic = defaults.object(forKey: Self.classicUIKey) as? Bool ?? Self.classicUIDefault
        let noGlass = defaults.object(forKey: Self.disableLiquidGlassKey) as? Bool ?? Self.disableLiquidGlassDefault
        if useClassicUI != classic { useClassicUI = classic }
        if disableLiquidGlass != noGlass { disableLiquidGlass = noGlass }
    }
}

enum UIStyle {
    static var isClassic: Bool { UIStyleStore.shared.useClassicUI }

    static var glassDisabled: Bool {
        UIStyleStore.shared.disableLiquidGlass || UIStyleStore.shared.useClassicUI
    }

    static var allowsLiquidGlass: Bool { !glassDisabled }
}
