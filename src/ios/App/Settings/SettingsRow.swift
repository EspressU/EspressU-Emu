// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct SettingsRow: View {
    let label: String
    let value: String
    var icon: String? = nil

    var body: some View {
        if UIStyle.isClassic {
            HStack {
                Text(label)
                Spacer(minLength: 12)
                Text(value)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.trailing)
            }
        } else {
            modernRow
        }
    }

    private var modernRow: some View {
        HStack(spacing: 10) {
            if let icon {
                Image(systemName: icon)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(EspressUTheme.brownMid)
                    .frame(width: 20)
            }

            Text(label)
                .font(.system(size: 15, weight: .semibold, design: .rounded))

            Spacer(minLength: 12)

            Text(value)
                .font(.system(size: 15, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
                .multilineTextAlignment(.trailing)
        }
        .frame(minHeight: 30)
    }
}

struct DestructiveSettingsLabel: View {
    let title: String
    let systemImage: String

    var body: some View {
        if UIStyle.isClassic {
            Label(title, systemImage: systemImage)
                .foregroundColor(.red)
        } else {
            Label(title, systemImage: systemImage)
                .font(.system(size: 15, weight: .semibold, design: .rounded))
                .foregroundColor(.red)
        }
    }
}
