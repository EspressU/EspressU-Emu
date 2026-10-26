// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct DisplaySettingsSection: View {
    @AppStorage(LocalScreenLayoutSettings.layoutKey)
    private var screenLayout = ScreenLayout.initialValue
    @AppStorage(LocalScreenLayoutSettings.showSwapButtonKey)
    private var showLocalSwapButton = LocalScreenLayoutSettings.defaultShowSwapButton

    @AppStorage(BrightnessBoost.key)
    private var brightnessBoost = BrightnessBoost.defaultValue
    @AppStorage(ScreenBrightness.maxWhilePlayingKey)
    private var maxBrightnessWhilePlaying = ScreenBrightness.defaultMaxWhilePlaying

    @AppStorage(ExternalDisplaySystemSettings.enabledKey)
    private var externalDisplaySystemEnabled = ExternalDisplaySystemSettings.defaultEnabled
    @AppStorage(DisplayLayoutSettings.swapKey)
    private var swapScreens = DisplayLayoutSettings.defaultSwap
    @AppStorage(DisplayLayoutSettings.showSwapButtonKey)
    private var showSwapButton = DisplayLayoutSettings.defaultShowSwapButton
    @AppStorage(DisplayLayoutSettings.deviceShowsKey)
    private var deviceShows = DisplayLayoutSettings.defaultDeviceShows

    var body: some View {
        Section {
            HStack {
                Text("Screen Layout")
                Button {
                    screenLayoutInfoShown = true
                } label: {
                    Image(systemName: "info.circle")
                }
                .foregroundColor(.secondary)
                .buttonStyle(.plain)
                .accessibilityLabel("About Screen Layout")

                Spacer()

                Picker("Screen Layout", selection: $screenLayout) {
                    ForEach(ScreenLayout.allCases) { layout in
                        Text(layout.string).tag(layout)
                    }
                }
                .pickerStyle(.menu)
                .labelsHidden()
                .tint(EspressUTheme.accentText)
            }
            .alert("Screen Layout", isPresented: $screenLayoutInfoShown) {
                Button("OK", role: .cancel) { }
            } message: {
                Text(screenLayout.description)
            }

            if screenLayout == .singleScreen {
                Toggle(isOn: $showLocalSwapButton) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Show swap button")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text("A small button on screen to switch between the TV and GamePad screens.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
            }

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Brightness boost")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Spacer()
                    Text(BrightnessBoost.label(for: brightnessBoost))
                        .font(.system(size: 13))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
                HStack(spacing: 8) {
                    Text("Off").font(.system(size: 12)).foregroundColor(EspressUTheme.secondaryText)
                    Slider(value: $brightnessBoost, in: 0...BrightnessBoost.maxValue)
                        .tint(EspressUTheme.accentText)
                        .accessibilityLabel("Brightness boost")
                    Text("Strong").font(.system(size: 12)).foregroundColor(EspressUTheme.secondaryText)
                }
                Text("Brightness boost lightens the game picture beyond your screen's maximum. Higher settings wash out dark areas.")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }

            Toggle(isOn: $maxBrightnessWhilePlaying) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Max screen brightness while playing")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("Sets your screen to full brightness during a game and puts it back afterwards.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)

            Toggle(isOn: $externalDisplaySystemEnabled) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Use an external display")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("Experimental. Turn it on before connecting a second screen.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)
            .onValueChange(of: externalDisplaySystemEnabled) { _ in
                DisplayRouter.shared.reapplyForExternalDisplaySystemToggle()
            }

            if externalDisplaySystemEnabled {
                Toggle(isOn: $swapScreens) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("GamePad screen on the external display")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text(swapScreens
                             ? "On: the external display shows the GamePad screen."
                             : "Off: the external display shows the TV screen.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
                .onValueChange(of: swapScreens) { _ in
                    DisplayRouter.shared.rerouteForScreenLayoutChange()
                }

                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("When a TV is connected, this device shows")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Spacer()
                        Picker("When a TV is connected, this device shows", selection: $deviceShows) {
                            ForEach(DeviceScreenMode.allCases) { mode in
                                Text(mode.string).tag(mode)
                            }
                        }
                        .pickerStyle(.menu)
                        .labelsHidden()
                        .tint(EspressUTheme.accentText)
                    }
                    Text(deviceShowsCaption)
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
                .onValueChange(of: deviceShows) { _ in
                    DisplayRouter.shared.rerouteForScreenLayoutChange()
                }

                Toggle(isOn: $showSwapButton) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Show swap button during play")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text("A small button over the game while an external display is connected, to flip the setting above without leaving the game.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
            }
        } header: {
            SettingsSectionHeader("Display", icon: "rectangle.on.rectangle", accent: .io)
        } footer: {
            InfoButton.footer(
                "Screen Layout arranges the TV and GamePad on this device. The external display is off until you turn it on, and needs a second screen that EspressU can open a window on; AirPlay mirroring doesn't count.",
                title: "Display",
                text: "The Wii U has two screens: the TV and the GamePad.\n\nScreen Layout: Single Screen shows one at a time with a swap button; Adaptive shows both, stacked in portrait and side by side in landscape; Both Screens (GamePad Top Right) keeps the TV full size with a small GamePad inset.\n\nAllow portrait during games (iPhone): hold the phone upright and the picture goes along the top with the controls below it. Menus stay in landscape. Turn it off if you'd rather a game never turns when the phone tilts; the iOS rotation lock works too.\n\nThe external display is off by default; turn it on before connecting a display. Then choose which Wii U screen it shows, and what this device shows next to it: the other screen, the same screen, or only the controls. It is experimental and hasn't been tested on much hardware.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    @State private var screenLayoutInfoShown = false

    private var deviceShowsCaption: String {
        let tvScreen = swapScreens ? "GamePad screen" : "TV screen"
        let otherScreen = swapScreens ? "TV screen" : "GamePad screen"
        switch deviceShows {
        case .otherScreen:
            return "The external display shows the \(tvScreen) and this device shows the \(otherScreen)."
        case .sameScreen:
            return "Both show the \(tvScreen)."
        case .nothing:
            return "The external display shows the \(tvScreen). This device shows only the on-screen controls on black."
        }
    }
}
