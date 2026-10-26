import SwiftUI
import GameController

enum MotionSettings {
    static let enabledKey = "espressu.motion.enabled"
    static let sensitivityKey = "espressu.motion.sensitivity"
    static let sourceKey = "espressu.motion.source"
    static let invertHorizontalKey = "espressu.motion.invertHorizontal"
    static let invertVerticalKey = "espressu.motion.invertVertical"
    static let diagnosticKey = "espressu.motion.diagnostic"

    static let defaultEnabled = true
    static let defaultSensitivity = 1.0
    static let minSensitivity = 0.25
    static let maxSensitivity = 3.0

    static let sourceDevice = 0
    static let sourceController = 1
    static let sourceAutomatic = 2
    static let defaultSource = sourceAutomatic
    static let statusOff = 0
    static let statusDevice = 1
    static let statusController = 2
    static let statusUnavailable = 3

    static func resolvedSource(_ stored: Int) -> Int {
        guard stored == sourceAutomatic else { return stored }
        return GCController.controllers().isEmpty ? sourceDevice : sourceController
    }

    static func startObservingControllers() {
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect] {
            NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { _ in applyToBridge() }
        }
    }

    static func applyToBridge() {
        let defaults = UserDefaults.standard
        cemu_bridge_set_motion_enabled(defaults.object(forKey: enabledKey) as? Bool ?? defaultEnabled)
        let sensitivity = defaults.object(forKey: sensitivityKey) as? Double ?? defaultSensitivity
        cemu_bridge_set_motion_sensitivity(Float(sensitivity))
        cemu_bridge_set_motion_source(Int32(resolvedSource(defaults.object(forKey: sourceKey) as? Int ?? defaultSource)))
        cemu_bridge_set_motion_invert(defaults.bool(forKey: invertHorizontalKey), defaults.bool(forKey: invertVerticalKey))
        cemu_bridge_set_motion_diagnostic(defaults.bool(forKey: diagnosticKey))
    }
}

struct MotionSettingsSection: View {
    @AppStorage(MotionSettings.enabledKey)
    private var motionEnabled = MotionSettings.defaultEnabled
    @AppStorage(MotionSettings.sensitivityKey)
    private var sensitivity = MotionSettings.defaultSensitivity
    @AppStorage(MotionSettings.sourceKey)
    private var source = MotionSettings.defaultSource
    @AppStorage(MotionSettings.invertHorizontalKey)
    private var invertHorizontal = false
    @AppStorage(MotionSettings.invertVerticalKey)
    private var invertVertical = false
    @AppStorage(MotionSettings.diagnosticKey)
    private var logValues = false
    @State private var recentred = false
    @AppStorage(SettingsMode.storageKey)
    private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    private var statusText: String {
        switch Int(cemu_bridge_motion_status()) {
        case MotionSettings.statusDevice:
            return "Using this device's gyroscope and accelerometer."
        case MotionSettings.statusController:
            return "Using a connected controller's motion sensors."
        case MotionSettings.statusUnavailable:
            return "This device has no motion sensors, so the GamePad will stay still."
        default:
            return "Motion is off. Games see a GamePad that never moves."
        }
    }

    var body: some View {
        Section {
            Toggle(isOn: $motionEnabled) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Motion aiming")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(motionEnabled
                         ? "Tilt and turn the device to aim, like the GamePad."
                         : "Stick only. The GamePad never moves.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)
            .onValueChange(of: motionEnabled) { _ in MotionSettings.applyToBridge() }

            if motionEnabled {
                if advanced {
                    motionOptions
                } else {
                    recentreButton
                }
            }
        } header: {
            SettingsSectionHeader("Motion & Aiming", icon: "gyroscope", accent: .io)
        } footer: {
            InfoButton.footer(
                "For games that aim with the GamePad's gyro, such as Splatoon.",
                title: "Motion & Aiming",
                text: fullText)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    @ViewBuilder private var motionOptions: some View {
        VStack(alignment: .leading, spacing: 4) {
            Picker("Motion from", selection: $source) {
                Text("Automatic").tag(MotionSettings.sourceAutomatic)
                Text("This device").tag(MotionSettings.sourceDevice)
                Text("Controller").tag(MotionSettings.sourceController)
            }
            .pickerStyle(.segmented)
            .onValueChange(of: source) { _ in MotionSettings.applyToBridge() }
            Text(statusText)
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)
        }

        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Sensitivity")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text(String(format: "%.2fx", sensitivity))
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            Slider(value: $sensitivity, in: MotionSettings.minSensitivity...MotionSettings.maxSensitivity)
                .onValueChange(of: sensitivity) { _ in MotionSettings.applyToBridge() }
        }

        DisclosureGroup("If aim feels backwards") {
            Toggle("Reverse left and right", isOn: $invertHorizontal)
                .onValueChange(of: invertHorizontal) { _ in MotionSettings.applyToBridge() }
            Toggle("Reverse up and down", isOn: $invertVertical)
                .onValueChange(of: invertVertical) { _ in MotionSettings.applyToBridge() }
            Toggle("Log motion values", isOn: $logValues)
                .onValueChange(of: logValues) { _ in MotionSettings.applyToBridge() }
            Text("Leave these off unless aiming turns the wrong way. The log writes one line a second to the engine log, to report when something looks off.")
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)
        }
        .font(.system(size: 13, weight: .semibold, design: .rounded))

        recentreButton
    }

    private var recentreButton: some View {
        Button {
            cemu_bridge_motion_recenter()
            recentred = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { recentred = false }
        } label: {
            Label(recentred ? "Recentred" : "Recentre aim", systemImage: "scope")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
    }

    private var fullText: String {
        "Motion aiming feeds this device's gyroscope and accelerometer to the emulated GamePad, so games that aim by moving the GamePad work the way they do on a Wii U. Hold the device like the GamePad, screen toward you, and tilt or turn it. It follows the way the screen is turned, so it works in every orientation.\n\nMotion from: This device uses the iPad or iPhone itself. Controller uses the motion sensors of a connected controller that has them (DualShock 4, DualSense, Switch Pro Controller) and falls back to the device when there is none. Controller motion has had little testing.\n\nSensitivity is how far the GamePad turns for a given movement. 1.00x matches the real GamePad. Turn it up if you have to move the device a long way to turn.\n\nRecentre aim forgets where the GamePad has been pointing and treats the current position as straight ahead. Use it when aim has drifted, or after turning in your seat. Many games also have their own recentre button.\n\nIf aiming turns the wrong way, \"If aim feels backwards\" reverses left and right, or up and down.\n\nTurn Motion aiming off to aim with the sticks only. A game with its own motion option, Splatoon included, should have that switched off as well."
    }
}
