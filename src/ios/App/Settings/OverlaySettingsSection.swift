import SwiftUI
import UIKit

enum OverlaySettings {
    static let positionKey = "espressu.overlay.position"
    static let defaultPosition = ScreenPosition.disabled

    static let textColorKey = "espressu.overlay.textColor"
    static let defaultTextColor: Int = 0xFFFFFFFF

    static let textScaleKey = "espressu.overlay.textScale"
    static let defaultTextScale = 100

    static let fpsKey = "espressu.overlay.fps"
    static let defaultFps = true

    static let drawcallsKey = "espressu.overlay.drawcalls"
    static let defaultDrawcalls = false

    static let cpuUsageKey = "espressu.overlay.cpuUsage"
    static let defaultCpuUsage = false

    static let cpuPerCoreUsageKey = "espressu.overlay.cpuPerCoreUsage"
    static let defaultCpuPerCoreUsage = false

    static let ramUsageKey = "espressu.overlay.ramUsage"
    static let defaultRamUsage = true

    static let vramUsageKey = "espressu.overlay.vramUsage"
    static let defaultVramUsage = false

    static let debugKey = "espressu.overlay.debug"
    static let defaultDebug = true
}

func packedColourBinding(_ value: Binding<Int>) -> Binding<Color> {
    Binding {
        let packed = UInt32(truncatingIfNeeded: value.wrappedValue)
        return Color(.sRGB,
                     red: Double((packed >> 16) & 0xFF) / 255,
                     green: Double((packed >> 8) & 0xFF) / 255,
                     blue: Double(packed & 0xFF) / 255,
                     opacity: 1)
    } set: { newValue in
        var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
        guard UIColor(newValue).getRed(&red, green: &green, blue: &blue, alpha: &alpha) else { return }
        func byte(_ component: CGFloat) -> UInt32 { UInt32(max(0, min(1, component)) * 255 + 0.5) }
        value.wrappedValue = Int(0xFF000000 | (byte(red) << 16) | (byte(green) << 8) | byte(blue))
    }
}

struct OverlaySettingsSection: View {
    @AppStorage(OverlaySettings.positionKey) private var positionRaw = OverlaySettings.defaultPosition.rawValue
    @AppStorage(OverlaySettings.textColorKey) private var textColor = OverlaySettings.defaultTextColor
    @AppStorage(OverlaySettings.textScaleKey) private var textScale = OverlaySettings.defaultTextScale
    @AppStorage(OverlaySettings.fpsKey) private var fpsEnabled = OverlaySettings.defaultFps
    @AppStorage(OverlaySettings.drawcallsKey) private var drawcallsEnabled = OverlaySettings.defaultDrawcalls
    @AppStorage(OverlaySettings.cpuUsageKey) private var cpuUsageEnabled = OverlaySettings.defaultCpuUsage
    @AppStorage(OverlaySettings.cpuPerCoreUsageKey) private var cpuPerCoreUsageEnabled = OverlaySettings.defaultCpuPerCoreUsage
    @AppStorage(OverlaySettings.ramUsageKey) private var ramUsageEnabled = OverlaySettings.defaultRamUsage
    @AppStorage(OverlaySettings.vramUsageKey) private var vramUsageEnabled = OverlaySettings.defaultVramUsage
    @AppStorage(OverlaySettings.debugKey) private var debugEnabled = OverlaySettings.defaultDebug
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    private var position: ScreenPosition {
        ScreenPosition(rawValue: positionRaw) ?? .disabled
    }

    private var isOff: Bool { position == .disabled }

    var body: some View {
        Section {
            positionPicker
            if advanced {
                textColorField
                textScaleSlider
            }
            fpsToggle
            if advanced {
                drawcallsToggle
                cpuUsageToggle
                cpuPerCoreUsageToggle
                ramUsageToggle
                vramUsageToggle
                debugToggle
            }
        } header: {
            SettingsSectionHeader("Performance Overlay", icon: "speedometer", accent: .io)
        } footer: {
            InfoButton.footer(
                "Shows FPS, CPU and RAM use on screen. Pick a corner to turn it on.",
                title: "Performance Overlay",
                text: fullText)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var positionPicker: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Position")
                .font(.system(size: 13, weight: .semibold, design: .rounded))
            Picker("Position", selection: $positionRaw) {
                ForEach(ScreenPosition.allCases) { position in
                    Text(position.title).tag(position.rawValue)
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .tint(EspressUTheme.accentText)
        }
        .onValueChange(of: positionRaw) { newValue in
            cemu_bridge_set_overlay_position(Int32(newValue))
        }
    }

    private var textColorField: some View {
        ColorPicker("Text colour", selection: packedColourBinding($textColor), supportsOpacity: false)
            .font(.system(size: 15, weight: .semibold, design: .rounded))
            .disabled(isOff)
            .onValueChange(of: textColor) { newValue in
                cemu_bridge_set_overlay_text_color(UInt32(truncatingIfNeeded: newValue))
            }
    }

    private var textScaleSlider: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Text size")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text("\(textScale)%")
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            Slider(
                value: Binding(get: { Double(textScale) }, set: { textScale = Int($0) }),
                in: 50...200, step: 25)
        }
        .disabled(isOff)
        .onValueChange(of: textScale) { newValue in
            cemu_bridge_set_overlay_text_scale(Int32(newValue))
        }
    }

    private var fpsToggle: some View {
        Toggle(isOn: $fpsEnabled) {
            Text("Show FPS")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: fpsEnabled) { newValue in
            cemu_bridge_set_overlay_fps(newValue)
        }
    }

    private var drawcallsToggle: some View {
        Toggle(isOn: $drawcallsEnabled) {
            Text("Show draw calls")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: drawcallsEnabled) { newValue in
            cemu_bridge_set_overlay_drawcalls(newValue)
        }
    }

    private var cpuUsageToggle: some View {
        Toggle(isOn: $cpuUsageEnabled) {
            Text("Show CPU usage")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: cpuUsageEnabled) { newValue in
            cemu_bridge_set_overlay_cpu_usage(newValue)
        }
    }

    private var cpuPerCoreUsageToggle: some View {
        Toggle(isOn: $cpuPerCoreUsageEnabled) {
            Text("Show CPU usage per core")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: cpuPerCoreUsageEnabled) { newValue in
            cemu_bridge_set_overlay_cpu_per_core_usage(newValue)
        }
    }

    private var ramUsageToggle: some View {
        Toggle(isOn: $ramUsageEnabled) {
            Text("Show RAM usage")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: ramUsageEnabled) { newValue in
            cemu_bridge_set_overlay_ram_usage(newValue)
        }
    }

    private var vramUsageToggle: some View {
        Toggle(isOn: $vramUsageEnabled) {
            Text("Show VRAM usage")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: vramUsageEnabled) { newValue in
            cemu_bridge_set_overlay_vram_usage(newValue)
        }
    }

    private var debugToggle: some View {
        Toggle(isOn: $debugEnabled) {
            Text("Show debug info")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: debugEnabled) { newValue in
            cemu_bridge_set_overlay_debug(newValue)
        }
    }

    private var fullText: String {
        """
        The overlay is the engine's own readout, like desktop Cemu's. Position picks a corner of the TV screen; Off hides it, and the other rows stay dimmed until a corner is chosen.

        FPS is the frame rate the game is producing. CPU, per-core CPU, RAM and VRAM usage measure EspressU itself, not the whole device. Draw calls counts draw commands in the current frame. Debug info adds a few lines of renderer state.

        The panel only appears while at least one of FPS, draw calls, CPU, per-core CPU or RAM is on. VRAM usage and debug info are extra lines inside it, so on their own they show nothing.

        Text size and colour change the readout's look. Changes show up straight away.
        """
    }
}
