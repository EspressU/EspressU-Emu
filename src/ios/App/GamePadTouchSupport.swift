// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct CompactTopBarRevealHandle: View {
    let onReveal: () -> Void

    var body: some View {
        Capsule()
            .fill(Color.white.opacity(0.6))
            .shadow(color: Color.black.opacity(0.45), radius: 1.5, x: 0, y: 0.5)
            .frame(width: 80, height: 8)
            .padding(.top, 8)
            .frame(width: 140, height: 52, alignment: .top)
            .contentShape(Rectangle())
            .onTapGesture { onReveal() }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Show top bar")
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { onReveal() }
    }
}
