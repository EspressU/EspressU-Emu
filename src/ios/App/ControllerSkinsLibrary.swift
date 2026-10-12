// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

extension WiiUControllerSkin {

    static let standard = WiiUControllerSkin(
        name: "Standard",
        dpadColor: ControllerSkinPalette.Standard.dpad,
        buttonColors: [
            "A": ControllerSkinPalette.Standard.a,
            "B": ControllerSkinPalette.Standard.b,
            "X": ControllerSkinPalette.Standard.x,
            "Y": ControllerSkinPalette.Standard.y
        ],
        backgroundColor: ControllerSkinPalette.Standard.background,
        borderColor: Color.white.opacity(0.1),
        shadowOpacity: 0.4,
        cornerRadius: 24
    )
}
