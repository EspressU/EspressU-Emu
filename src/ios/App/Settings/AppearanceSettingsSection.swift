// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct AppearanceSettingsSection: View {
    @Binding var showingThemePicker: Bool

    @ObservedObject private var themeStore = EspressUThemeStore.shared

    var body: some View {
        Section {

            Button(action: { showingThemePicker = true }) {
                HStack {
                    Label("Accent colour", systemImage: "paintpalette")
                        .font(.subheadline.weight(.semibold))
                    Spacer(minLength: 12)
                    Text(themeStore.current.name)
                        .font(.subheadline)
                        .foregroundColor(EspressUTheme.brownMid)
                }
            }
            .foregroundColor(EspressUTheme.brownDarkest)
        } header: {
            SettingsSectionHeader("Appearance", icon: "paintpalette", accent: .identity)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}
