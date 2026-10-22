// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit
import Darwin

enum JITSettings {
    static let enabledKey = "espressu.cpu.recompiler"

    static var enabled: Bool {
        UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true
    }

    static func setEnabled(_ on: Bool) {
        UserDefaults.standard.set(on, forKey: enabledKey)
        cemu_bridge_set_recompiler_enabled(on)
        NotificationCenter.default.post(name: .espressuJITSettingChanged, object: nil)
    }

    static func migrate() {
        let defaults = UserDefaults.standard
        for key in ["espressu.jit.enabler", "espressu.jit.customURL", "espressu.jit.askOnLaunch"] {
            defaults.removeObject(forKey: key)
        }
    }
}

extension Notification.Name {
    static let espressuJITSettingChanged = Notification.Name("espressu.jit.settingChanged")
}

enum JITStatus {
    private typealias CsopsFn = @convention(c) (pid_t, UInt32, UnsafeMutableRawPointer?, Int) -> Int32

    static var isActive: Bool {
        guard let sym = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "csops") else { return false }
        let csops = unsafeBitCast(sym, to: CsopsFn.self)
        var flags: UInt32 = 0
        guard csops(getpid(), 0, &flags, MemoryLayout<UInt32>.size) == 0 else { return false }
        return flags & 0x1000_0000 != 0
    }

    /// The flag stays set after the JIT helper has let go, so a game start also asks the engine side to map a page.
    static var isUsable: Bool { isActive && cemu_bridge_jit_probe() }
}
