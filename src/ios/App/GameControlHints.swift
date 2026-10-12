// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
#if os(iOS)
import UIKit

enum GameControlHints {
    private static let splatoonTitleIds: Set<UInt64> = [
        0x0005000010176900,
        0x0005000010176A00,
        0x0005000010162B00,
    ]

    private static let appliedKey = "espressu.hints.appliedGlobals"
    private static let lock = NSLock()

    static func applyBeforeLaunch(titleId: UInt64?) {
        lock.lock()
        defer { lock.unlock() }
        undoLocked()
        guard let titleId, splatoonTitleIds.contains(titleId) else { return }
        let defaults = UserDefaults.standard
        var applied: [String: String] = [:]

        if defaults.object(forKey: ControllerLayoutSettings.joystickKey) == nil {
            defaults.set(true, forKey: ControllerLayoutSettings.joystickKey)
            applied[ControllerLayoutSettings.joystickKey] = described(ControllerLayoutSettings.joystickKey)
        }

        if defaults.object(forKey: LocalScreenLayoutSettings.layoutKey) == nil {
            let layout: ScreenLayout = UIDevice.current.userInterfaceIdiom == .pad ? .bothScreens : .smallGamePadTopRight
            defaults.set(layout.rawValue, forKey: LocalScreenLayoutSettings.layoutKey)
            applied[LocalScreenLayoutSettings.layoutKey] = described(LocalScreenLayoutSettings.layoutKey)
        }

        if !applied.isEmpty { defaults.set(applied, forKey: appliedKey) }
    }

    static func restoreGlobals() {
        lock.lock()
        defer { lock.unlock() }
        undoLocked()
    }

    private static func undoLocked() {
        let defaults = UserDefaults.standard
        guard let applied = defaults.dictionary(forKey: appliedKey) as? [String: String] else { return }
        defaults.removeObject(forKey: appliedKey)
        for (key, written) in applied where described(key) == written {
            defaults.removeObject(forKey: key)
        }
    }

    private static func described(_ key: String) -> String? {
        UserDefaults.standard.object(forKey: key).map { "\($0)" }
    }
}
#endif
