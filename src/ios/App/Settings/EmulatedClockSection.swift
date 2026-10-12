// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct EmulatedClockSection: View {
    @State private var timebaseRaw = TimebaseScale.current.rawValue
    @State private var resetCount = 0

    private var timebase: TimebaseScale {
        TimebaseScale(rawValue: timebaseRaw) ?? .realTime
    }

    private var speedChoice: Binding<Int> {
        Binding(
            get: { timebaseRaw },
            set: { raw in
                timebaseRaw = raw
                guard let scale = TimebaseScale(rawValue: raw) else { return }
                TimebaseScale.apply(scale)
            })
    }

    var body: some View {
        Section {
            Picker("Speed", selection: speedChoice) {
                ForEach(TimebaseScale.allCases) { scale in
                    Text(scale.title).tag(scale.rawValue)
                }
            }
            .pickerStyle(.menu)
            .tint(EspressUTheme.accentText)
            .foregroundColor(EspressUTheme.brownDarkest)

            if TimebaseScale.hasExplicitChoice {
                Button("Let EspressU choose again") {
                    TimebaseScale.clearChoice()
                    timebaseRaw = TimebaseScale.realTime.rawValue
                    resetCount += 1
                }
                .font(.system(size: 15, weight: .semibold, design: .rounded))
            } else {
                Text("Automatic: EspressU slows the clock itself if a game doesn't start drawing.")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        } header: {
            SettingsSectionHeader("Emulated Clock", icon: "clock", accent: .core)
        } footer: {
            InfoButton.footer(
                "\(timebase.summary) Changes how fast the game thinks time passes; it doesn't change how fast EspressU runs.",
                title: "Emulated Clock",
                text: "Until you choose a value, EspressU picks one itself: if a game hasn't started drawing after 12 seconds, it slows its clock a step (down to 1/64). Choosing a value here turns that off.\n\nWithout the recompiler the emulated CPU is much slower than the console's, so games can fall behind their own timers and never draw a frame. Slowing the game's clock gives them room. Emulation accuracy isn't affected.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onReceive(NotificationCenter.default.publisher(for: .espressuSettingsWereReset)) { _ in
            timebaseRaw = TimebaseScale.current.rawValue
            resetCount += 1
        }
    }
}
