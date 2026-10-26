import SwiftUI

enum NotificationSettings {
    static let positionKey = "espressu.notification.position"
    static let defaultPosition = ScreenPosition.topLeft

    static let textColorKey = "espressu.notification.textColor"
    static let defaultTextColor: Int = 0xFFFFFFFF

    static let textScaleKey = "espressu.notification.textScale"
    static let defaultTextScale = 100

    static let controllerProfilesKey = "espressu.notification.controllerProfiles"
    static let defaultControllerProfiles = true

    static let controllerBatteryKey = "espressu.notification.controllerBattery"
    static let defaultControllerBattery = false

    static let shaderCompilingKey = "espressu.notification.shaderCompiling"
    static let defaultShaderCompiling = true

    static let friendsKey = "espressu.notification.friends"
    static let defaultFriends = true
}

struct NotificationSettingsSection: View {
    @AppStorage(NotificationSettings.positionKey) private var positionRaw = NotificationSettings.defaultPosition.rawValue
    @AppStorage(NotificationSettings.textColorKey) private var textColor = NotificationSettings.defaultTextColor
    @AppStorage(NotificationSettings.textScaleKey) private var textScale = NotificationSettings.defaultTextScale
    @AppStorage(NotificationSettings.controllerProfilesKey) private var controllerProfilesEnabled = NotificationSettings.defaultControllerProfiles
    @AppStorage(NotificationSettings.controllerBatteryKey) private var controllerBatteryEnabled = NotificationSettings.defaultControllerBattery
    @AppStorage(NotificationSettings.shaderCompilingKey) private var shaderCompilingEnabled = NotificationSettings.defaultShaderCompiling
    @AppStorage(NotificationSettings.friendsKey) private var friendsEnabled = NotificationSettings.defaultFriends
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
            controllerProfilesToggle
            controllerBatteryToggle
            shaderCompilingToggle
            friendsToggle
        } header: {
            SettingsSectionHeader("Notifications", icon: "bell", accent: .io)
        } footer: {
            InfoButton.footer(
                "Pop-ups for controllers, shader compiling and friends. They show in the corner you pick; choose Off to hide them all.",
                title: "Notifications",
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
            cemu_bridge_set_notification_position(Int32(newValue))
        }
    }

    private var textColorField: some View {
        ColorPicker("Text colour", selection: packedColourBinding($textColor), supportsOpacity: false)
            .font(.system(size: 15, weight: .semibold, design: .rounded))
            .disabled(isOff)
            .onValueChange(of: textColor) { newValue in
                cemu_bridge_set_notification_text_color(UInt32(truncatingIfNeeded: newValue))
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
            cemu_bridge_set_notification_text_scale(Int32(newValue))
        }
    }

    private var controllerProfilesToggle: some View {
        Toggle(isOn: $controllerProfilesEnabled) {
            Text("Controller profiles")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: controllerProfilesEnabled) { newValue in
            cemu_bridge_set_notification_controller_profiles(newValue)
        }
    }

    private var controllerBatteryToggle: some View {
        Toggle(isOn: $controllerBatteryEnabled) {
            Text("Low controller battery")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: controllerBatteryEnabled) { newValue in
            cemu_bridge_set_notification_controller_battery(newValue)
        }
    }

    private var shaderCompilingToggle: some View {
        Toggle(isOn: $shaderCompilingEnabled) {
            Text("Shader compiling")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: shaderCompilingEnabled) { newValue in
            cemu_bridge_set_notification_shader_compiling(newValue)
        }
    }

    private var friendsToggle: some View {
        Toggle(isOn: $friendsEnabled) {
            Text("Friends")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .disabled(isOff)
        .onValueChange(of: friendsEnabled) { newValue in
            cemu_bridge_set_notification_friends(newValue)
        }
    }

    private var fullText: String {
        """
        Position picks a corner of the TV screen; Off hides every pop-up, and the other rows stay dimmed until a corner is chosen.

        Controller profiles: your account name and each controller's saved profile, for a few seconds after a game starts. Low controller battery: a paired controller is running down. Shader compiling: shown while the engine builds new shaders. Friends: friend activity from your account.
        """
    }
}
