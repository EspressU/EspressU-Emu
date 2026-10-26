import SwiftUI
import UIKit

enum AudioChannelSetting: Int, CaseIterable, Identifiable {
    case mono = 0
    case stereo = 1
    case surround = 2

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .mono:     return "Mono"
        case .stereo:   return "Stereo"
        case .surround: return "Surround"
        }
    }
}

enum AudioSettings {
    static let tvEnabledKey = "espressu.audio.tvEnabled"
    static let defaultTvEnabled = true

    static let tvVolumeKey = "espressu.audio.tvVolume"
    static let defaultTvVolume = 50

    static let tvChannelsKey = "espressu.audio.tvChannels"
    static let defaultTvChannels = AudioChannelSetting.stereo.rawValue

    static let padEnabledKey = "espressu.audio.padEnabled"
    static let defaultPadEnabled = false

    static let padVolumeKey = "espressu.audio.padVolume"
    static let defaultPadVolume = 50

    static let padChannelsKey = "espressu.audio.padChannels"
    static let defaultPadChannels = AudioChannelSetting.stereo.rawValue

    static let microphoneEnabledKey = "espressu.audio.microphoneEnabled"
    static let defaultMicrophoneEnabled = false

    static let inputVolumeKey = "espressu.audio.inputVolume"
    static let defaultInputVolume = 50
}

struct AudioSettingsSection: View {
    @AppStorage(AudioSettings.tvEnabledKey) private var tvEnabled = AudioSettings.defaultTvEnabled
    @AppStorage(AudioSettings.tvVolumeKey) private var tvVolume = AudioSettings.defaultTvVolume
    @AppStorage(AudioSettings.tvChannelsKey) private var tvChannelsRaw = AudioSettings.defaultTvChannels

    @AppStorage(AudioSettings.padEnabledKey) private var padEnabled = AudioSettings.defaultPadEnabled
    @AppStorage(AudioSettings.padVolumeKey) private var padVolume = AudioSettings.defaultPadVolume
    @AppStorage(AudioSettings.padChannelsKey) private var padChannelsRaw = AudioSettings.defaultPadChannels

    @AppStorage(AudioSettings.microphoneEnabledKey) private var microphoneEnabled = AudioSettings.defaultMicrophoneEnabled
    @AppStorage(AudioSettings.inputVolumeKey) private var inputVolume = AudioSettings.defaultInputVolume
    @State private var showMicDenied = false
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    private var deviceName: String {
        UIDevice.current.userInterfaceIdiom == .pad ? "iPad" : "iPhone"
    }

    var body: some View {
        Section {
            RespectSilentModeToggle()
            tvGroup
            padGroup
            microphoneGroup
        } header: {
            SettingsSectionHeader("Audio", icon: "speaker.wave.2", accent: .io)
        } footer: {
            InfoButton.footer(
                "TV and GamePad audio have their own volume and channel layout. GamePad audio plays through this device's speaker or headphones. Use the microphone to give games your real voice and sounds through the GamePad mic; otherwise the in-game Blow button covers games that ask you to blow.",
                title: "Audio",
                text: fullText)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    @ViewBuilder private var tvGroup: some View {
        Toggle(isOn: $tvEnabled) {
            Text("TV audio")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: tvEnabled) { newValue in
            cemu_bridge_set_tv_audio_enabled(newValue)
        }

        if tvEnabled {
            volumeRow(label: "TV volume", volume: $tvVolume) { newValue in
                cemu_bridge_set_tv_volume(Int32(newValue))
            }
            if advanced {
                Picker("TV channels", selection: $tvChannelsRaw) {
                    ForEach(AudioChannelSetting.allCases) { channels in
                        Text(channels.title).tag(channels.rawValue)
                    }
                }
                .pickerStyle(.menu)
                .tint(EspressUTheme.accentText)
                .foregroundColor(EspressUTheme.brownDarkest)
                .onValueChange(of: tvChannelsRaw) { newValue in
                    cemu_bridge_set_tv_channels(Int32(newValue))
                }
            }
        }
    }

    @ViewBuilder private var padGroup: some View {
        Toggle(isOn: $padEnabled) {
            Text("GamePad audio")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: padEnabled) { newValue in
            cemu_bridge_set_pad_audio_enabled(newValue)
        }

        if padEnabled {
            volumeRow(label: "GamePad volume", volume: $padVolume) { newValue in
                cemu_bridge_set_pad_volume(Int32(newValue))
            }
            if advanced {
                Picker("GamePad channels", selection: $padChannelsRaw) {
                    ForEach(AudioChannelSetting.allCases) { channels in
                        Text(channels.title).tag(channels.rawValue)
                    }
                }
                .pickerStyle(.menu)
                .tint(EspressUTheme.accentText)
                .foregroundColor(EspressUTheme.brownDarkest)
                .onValueChange(of: padChannelsRaw) { newValue in
                    cemu_bridge_set_pad_channels(Int32(newValue))
                }
            }
        }
    }

    @ViewBuilder private var microphoneGroup: some View {
        Toggle(isOn: $microphoneEnabled) {
            Text("Use \(deviceName) microphone")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: microphoneEnabled) { newValue in
            guard newValue else {
                cemu_bridge_set_microphone_enabled(false)
                return
            }
            MicrophoneAccess.request { granted in
                if granted {
                    cemu_bridge_set_microphone_enabled(true)
                } else {
                    microphoneEnabled = false
                    cemu_bridge_set_microphone_enabled(false)
                    showMicDenied = true
                }
            }
        }
        .onAppear {
            if microphoneEnabled && MicrophoneAccess.status == .denied {
                microphoneEnabled = false
                cemu_bridge_set_microphone_enabled(false)
            }
        }
        .alert("Microphone access is off", isPresented: $showMicDenied) {
            Button("Open Settings") { MicrophoneAccess.openSystemSettings() }
            Button("Not Now", role: .cancel) {}
        } message: {
            Text("EspressU can't use your \(deviceName)'s microphone until you allow it in iOS Settings. Until then, games use the Blow button in the top bar instead.")
        }

        if microphoneEnabled {
            volumeRow(label: "Microphone volume", volume: $inputVolume) { newValue in
                cemu_bridge_set_input_volume(Int32(newValue))
            }
        }
    }

    private func volumeRow(label: String, volume: Binding<Int>, onChange: @escaping (Int) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text("\(volume.wrappedValue)%")
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            Slider(
                value: Binding<Double>(
                    get: { Double(volume.wrappedValue) },
                    set: { newValue in
                        let rounded = Int(newValue.rounded())
                        volume.wrappedValue = rounded
                        onChange(rounded)
                    }),
                in: 0...100,
                step: 1)
                .accessibilityLabel(label)
        }
    }

    private var fullText: String {
        "TV and GamePad audio are separate tracks with their own on/off, volume and channel layout.\n\nChannels: Mono mixes everything to one channel, Stereo splits left and right (default), Surround asks the game for more channels; most games only use stereo. Turning TV or GamePad audio on or off, and changing channels, applies the next time you start a game.\n\nGamePad audio plays whatever the game sends to the GamePad speaker. Many games send nothing different from the TV mix.\n\nUse microphone feeds your device's real microphone to games that ask for the GamePad mic, while the game has the mic open. iOS asks for permission the first time you turn it on; if you say no, it switches back off. While it's on, the in-game Blow button is hidden. When it's off, games still see a GamePad mic, but it only hears the Blow button (a simulated puff of air) and iOS never asks for permission. Microphone volume (50 is normal, 100 is twice as loud) applies the next time a game opens the mic."
    }
}
