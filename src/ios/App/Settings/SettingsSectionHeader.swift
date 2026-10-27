// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

enum SettingsSectionAccent {
    case core
    case io
    case content
    case identity
    case system
    case preview

    var color: Color {
        switch self {
        case .core: return EspressUTheme.accentText
        case .io: return EspressUTheme.readable(\.blueberryNavyLight, \.blueberryNavyDark)
        case .content: return EspressUTheme.readable(\.cremaTopDarkLight, \.cremaTopDarkDark)
        case .identity: return EspressUTheme.alertText
        case .system: return EspressUTheme.brownMid
        case .preview: return EspressUTheme.cautionText
        }
    }
}

struct SettingsSectionHeader: View {
    let title: String
    let icon: String
    var accent: SettingsSectionAccent = .core

    init(_ title: String, icon: String, accent: SettingsSectionAccent = .core) {
        self.title = title
        self.icon = icon
        self.accent = accent
    }

    var body: some View {
        if UIStyle.isClassic {
            Text(title)
                .textCase(nil)
                .accessibilityAddTraits(.isHeader)
        } else {
            modernHeader
        }
    }

    private var modernHeader: some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(accent.color)
                .frame(width: 22, height: 22)
                .background(
                    RoundedRectangle(cornerRadius: 7, style: .continuous)
                        .fill(accent.color.opacity(0.16))
                )

            Text(title)
                .font(.footnote.weight(.semibold))
                .foregroundColor(EspressUTheme.brownDark)
                .textCase(nil)

            Spacer(minLength: 0)
        }
        .padding(.bottom, 5)
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(.isHeader)
    }
}
