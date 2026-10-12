// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct AppearanceSettingsSection: View {
    @Binding var showingThemePicker: Bool

    @ObservedObject private var style = UIStyleStore.shared
    @ObservedObject private var themeStore = EspressUThemeStore.shared

    var body: some View {
        Section {

            Button(action: { showingThemePicker = true }) {
                HStack {
                    Label("Theme", systemImage: "paintpalette")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Spacer(minLength: 12)
                    Text(themeStore.current.name)
                        .font(.system(size: 15, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                }
            }
            .foregroundColor(EspressUTheme.brownDarkest)

            Toggle(isOn: $style.disableLiquidGlass) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Flat surfaces")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(style.useClassicUI
                         ? "Already on while Classic UI is on."
                         : "Removes the highlights and shading on cards and buttons.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)
            .disabled(style.useClassicUI)

            Toggle(isOn: $style.useClassicUI) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Use Classic UI")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("The v2.0 look: flat cards, plain headers, system-font rows.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)
        } header: {
            SettingsSectionHeader("Appearance", icon: "paintpalette", accent: .identity)
        } footer: {
            InfoButton.footer(
                "Both change how EspressU looks, not what it does.",
                title: "Appearance",
                text: "Flat surfaces removes the soft highlights on cards and buttons.\n\nClassic UI restores the flat v2.0 styling: plain cards, text headers, system-font rows. It turns on flat surfaces too. All settings and screens are still there; newer screens just have no classic form.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}
