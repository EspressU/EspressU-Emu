// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum ActiveGameSettings {
    private static let lock = NSLock()
    private static var active: GameOverrides = .identity

    private static let swapKey = "espressu.perGame.swappedGlobals"

    static var overrides: GameOverrides {
        lock.lock()
        defer { lock.unlock() }
        return active
    }

    @discardableResult
    static func begin(gameID id: String) -> Bool {
        let before = RenderScale.current
        restoreSwappedGlobals()
        let next = PerGameSettingsStore.shared.activeOverrides(for: id)
        lock.lock()
        active = next
        lock.unlock()
        let defaults = UserDefaults.standard
        var values: [String: Any] = [:]
        var absent: [String] = []
        func swap(_ key: String, to value: Any) {
            if let old = defaults.object(forKey: key) { values[key] = old } else { absent.append(key) }
            defaults.set(value, forKey: key)
        }
        if let layout = next.screenLayout, ScreenLayout(rawValue: layout) != nil {
            swap(LocalScreenLayoutSettings.layoutKey, to: layout)
        }
        if let hide = next.autoHideControls {
            swap(ControllerLayoutSettings.autoHideWithControllerKey, to: hide)
        }
        if !values.isEmpty || !absent.isEmpty {
            defaults.set(["values": values, "absent": absent], forKey: swapKey)
        }
        return RenderScale.current != before
    }

    static func end() {
        lock.lock()
        active = .identity
        lock.unlock()
        restoreSwappedGlobals()
    }

    private static func restoreSwappedGlobals() {
        let defaults = UserDefaults.standard
        guard let saved = defaults.dictionary(forKey: swapKey) else { return }
        for (key, value) in saved["values"] as? [String: Any] ?? [:] { defaults.set(value, forKey: key) }
        for key in saved["absent"] as? [String] ?? [] { defaults.removeObject(forKey: key) }
        defaults.removeObject(forKey: swapKey)
    }
}
