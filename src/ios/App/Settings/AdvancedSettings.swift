// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum SettingsMode: String, CaseIterable, Identifiable {
    case basic
    case advanced

    var id: String { rawValue }

    var title: String {
        switch self {
        case .basic:    return "Basic"
        case .advanced: return "Advanced"
        }
    }

    static let storageKey = "espressu.settings.mode"
    static let defaultValue: SettingsMode = .basic

    static var current: SettingsMode {
        SettingsMode(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? defaultValue
    }

    static var isAdvanced: Bool { current == .advanced }

    static func isAdvanced(raw: String) -> Bool { raw == SettingsMode.advanced.rawValue }

    static func chooseInitialModeIfNeeded() {
        let defaults = UserDefaults.standard
        guard defaults.string(forKey: storageKey) == nil else { return }
        defaults.set((AdvancedSettings.hasCustomisedValues ? SettingsMode.advanced : .basic).rawValue, forKey: storageKey)
    }
}

struct AdvancedSetting {
    let key: String
    let defaultValue: Any?

    init(_ key: String, _ defaultValue: Any?) {
        self.key = key
        self.defaultValue = defaultValue
    }

    func isAtDefault(in defaults: UserDefaults = .standard) -> Bool {
        guard let stored = defaults.object(forKey: key) else { return true }
        guard let defaultValue else { return false }
        return (stored as AnyObject).isEqual(defaultValue)
    }
}

struct AdvancedGroup {
    let name: String
    let settings: [AdvancedSetting]
    let applyToBridge: (() -> Void)?
}

enum AdvancedSettings {
    private static let snapshotKey = "espressu.settings.advancedSnapshot"
    private static let perGameSnapshotKey = "espressu.settings.advancedSnapshot.perGame"

    private static let cpuAndPerformance = AdvancedGroup(name: "CPU and performance", settings: [
        AdvancedSetting("espressu.cpu.favourAccuracy", false),
        AdvancedSetting(FavourPerformance.storageKey, FavourPerformance.defaultValue),
        AdvancedSetting(OneCoreMode.storageKey, OneCoreMode.defaultValue),
        AdvancedSetting(CoreMode.storageKey, CoreMode.defaultValue.rawValue),
        AdvancedSetting(ThermalSettings.thresholdKey, ThermalSettings.defaultThreshold.rawValue),
        AdvancedSetting("espressu.cpu.multicore", nil),
    ]) {
        let defaults = UserDefaults.standard
        cemu_bridge_set_favour_accuracy(defaults.object(forKey: "espressu.cpu.favourAccuracy") as? Bool ?? false)
        cemu_bridge_set_favour_performance(FavourPerformance.isEnabled)
        cemu_bridge_set_low_power_mode(OneCoreMode.isEnabled)
        cemu_bridge_set_cpu_core_mode(CoreMode.current.bridgeValue)
    }

    private static let fullSpeedRenders = AdvancedGroup(name: "Steady frame rate", settings: [
        AdvancedSetting(FullSpeedRenders.storageKey, FullSpeedRenders.defaultValue),
        AdvancedSetting(FullSpeedRenders.shaderModeKey, FullSpeedRenders.defaultShaderMode.rawValue),
    ]) {
        FullSpeedRenders.applyToBridge()
    }

    private static let rendererAndFilters = AdvancedGroup(name: "Renderer and filters", settings: [
        AdvancedSetting(RendererAPI.storageKey, RendererAPI.defaultValue.rawValue),
        AdvancedSetting(MoltenVKBuild.storageKey, MoltenVKBuild.defaultValue.rawValue),
        AdvancedSetting(UpscaleFilterSetting.storageKey, UpscaleFilterSetting.defaultValue.rawValue),
        AdvancedSetting(DownscaleFilterSetting.storageKey, DownscaleFilterSetting.defaultValue.rawValue),
    ], applyToBridge: nil)

    private static let picture = AdvancedGroup(name: "Picture", settings: [
        AdvancedSetting("espressu.render.upsideDown", false),
        AdvancedSetting("espressu.render.framebufferFetch", true),
        AdvancedSetting(DisplayGammaSetting.storageKey, DisplayGammaSetting.defaultValue),
        AdvancedSetting("espressu.render.overrideAppGamma", false),
        AdvancedSetting(OverrideGammaSetting.storageKey, OverrideGammaSetting.defaultValue),
    ]) {
        let defaults = UserDefaults.standard
        cemu_bridge_set_render_upside_down(defaults.object(forKey: "espressu.render.upsideDown") as? Bool ?? false)
        cemu_bridge_set_framebuffer_fetch(defaults.object(forKey: "espressu.render.framebufferFetch") as? Bool ?? true)
        cemu_bridge_set_display_gamma(Float(
            defaults.object(forKey: DisplayGammaSetting.storageKey) as? Double ?? DisplayGammaSetting.defaultValue))
        cemu_bridge_set_override_app_gamma(defaults.object(forKey: "espressu.render.overrideAppGamma") as? Bool ?? false)
        cemu_bridge_set_override_gamma_value(Float(
            defaults.object(forKey: OverrideGammaSetting.storageKey) as? Double ?? OverrideGammaSetting.defaultValue))
    }

    private static let emulatedClock = AdvancedGroup(name: "Emulated clock", settings: [
        AdvancedSetting(TimebaseScale.storageKey, nil),
    ]) {
        if TimebaseScale.hasExplicitChoice {
            TimebaseScale.applyStoredChoiceIfAny()
        } else {
            TimebaseScale.clearChoice()
        }
    }

    private static let overlayDetails = AdvancedGroup(name: "Overlay details", settings: [
        AdvancedSetting(OverlaySettings.textColorKey, OverlaySettings.defaultTextColor),
        AdvancedSetting(OverlaySettings.textScaleKey, OverlaySettings.defaultTextScale),
        AdvancedSetting(OverlaySettings.drawcallsKey, OverlaySettings.defaultDrawcalls),
        AdvancedSetting(OverlaySettings.cpuUsageKey, OverlaySettings.defaultCpuUsage),
        AdvancedSetting(OverlaySettings.cpuPerCoreUsageKey, OverlaySettings.defaultCpuPerCoreUsage),
        AdvancedSetting(OverlaySettings.ramUsageKey, OverlaySettings.defaultRamUsage),
        AdvancedSetting(OverlaySettings.vramUsageKey, OverlaySettings.defaultVramUsage),
        AdvancedSetting(OverlaySettings.debugKey, OverlaySettings.defaultDebug),
    ]) {
        let defaults = UserDefaults.standard
        cemu_bridge_set_overlay_text_color(UInt32(truncatingIfNeeded:
            defaults.object(forKey: OverlaySettings.textColorKey) as? Int ?? OverlaySettings.defaultTextColor))
        cemu_bridge_set_overlay_text_scale(Int32(clamping:
            defaults.object(forKey: OverlaySettings.textScaleKey) as? Int ?? OverlaySettings.defaultTextScale))
        cemu_bridge_set_overlay_drawcalls(
            defaults.object(forKey: OverlaySettings.drawcallsKey) as? Bool ?? OverlaySettings.defaultDrawcalls)
        cemu_bridge_set_overlay_cpu_usage(
            defaults.object(forKey: OverlaySettings.cpuUsageKey) as? Bool ?? OverlaySettings.defaultCpuUsage)
        cemu_bridge_set_overlay_cpu_per_core_usage(
            defaults.object(forKey: OverlaySettings.cpuPerCoreUsageKey) as? Bool ?? OverlaySettings.defaultCpuPerCoreUsage)
        cemu_bridge_set_overlay_ram_usage(
            defaults.object(forKey: OverlaySettings.ramUsageKey) as? Bool ?? OverlaySettings.defaultRamUsage)
        cemu_bridge_set_overlay_vram_usage(
            defaults.object(forKey: OverlaySettings.vramUsageKey) as? Bool ?? OverlaySettings.defaultVramUsage)
        cemu_bridge_set_overlay_debug(
            defaults.object(forKey: OverlaySettings.debugKey) as? Bool ?? OverlaySettings.defaultDebug)
    }

    private static let notificationLooks = AdvancedGroup(name: "Notification looks", settings: [
        AdvancedSetting(NotificationSettings.textColorKey, NotificationSettings.defaultTextColor),
        AdvancedSetting(NotificationSettings.textScaleKey, NotificationSettings.defaultTextScale),
    ]) {
        let defaults = UserDefaults.standard
        cemu_bridge_set_notification_text_color(UInt32(truncatingIfNeeded:
            defaults.object(forKey: NotificationSettings.textColorKey) as? Int ?? NotificationSettings.defaultTextColor))
        cemu_bridge_set_notification_text_scale(Int32(clamping:
            defaults.object(forKey: NotificationSettings.textScaleKey) as? Int ?? NotificationSettings.defaultTextScale))
    }

    private static let audioChannels = AdvancedGroup(name: "Audio channels", settings: [
        AdvancedSetting(AudioSettings.tvChannelsKey, AudioSettings.defaultTvChannels),
        AdvancedSetting(AudioSettings.padChannelsKey, AudioSettings.defaultPadChannels),
    ]) {
        let defaults = UserDefaults.standard
        cemu_bridge_set_tv_channels(Int32(clamping:
            defaults.object(forKey: AudioSettings.tvChannelsKey) as? Int ?? AudioSettings.defaultTvChannels))
        cemu_bridge_set_pad_channels(Int32(clamping:
            defaults.object(forKey: AudioSettings.padChannelsKey) as? Int ?? AudioSettings.defaultPadChannels))
    }

    private static let motionTuning = AdvancedGroup(name: "Motion tuning", settings: [
        AdvancedSetting(MotionSettings.sourceKey, MotionSettings.defaultSource),
        AdvancedSetting(MotionSettings.sensitivityKey, MotionSettings.defaultSensitivity),
        AdvancedSetting(MotionSettings.invertHorizontalKey, false),
        AdvancedSetting(MotionSettings.invertVerticalKey, false),
        AdvancedSetting(MotionSettings.diagnosticKey, false),
    ]) {
        MotionSettings.applyToBridge()
    }

    private static let stickFeel = AdvancedGroup(name: "Stick feel", settings: [
        AdvancedSetting(ControllerLayoutSettings.deadzoneKey, ControllerLayoutSettings.defaultDeadzone),
    ], applyToBridge: nil)

    private static let topBar = AdvancedGroup(name: "Top bar", settings: [
        AdvancedSetting(TopBarAutoHide.hideDelayKey, TopBarAutoHide.defaultHideDelaySeconds),
        AdvancedSetting(TopBarAutoHide.handleSizeKey, TopBarAutoHide.defaultHandleSize.rawValue),
    ], applyToBridge: nil)

    private static let diagnostics = AdvancedGroup(name: "Diagnostics", settings: [
        AdvancedSetting(LaunchLogSettings.showKey, false),
        AdvancedSetting(PadDiagnostics.enabledKey, PadDiagnostics.defaultEnabled),
    ], applyToBridge: nil)

    private static let libraryOrganization = AdvancedGroup(name: "Library organization", settings: [
        AdvancedSetting(LibraryGrouping.storageKey, LibraryGrouping.defaultValue.rawValue),
        AdvancedSetting(LibraryFilter.storageKey, LibraryFilter.defaultValue.rawValue),
    ], applyToBridge: nil)

    static let groups: [AdvancedGroup] = [
        cpuAndPerformance,
        fullSpeedRenders,
        rendererAndFilters,
        picture,
        emulatedClock,
        overlayDetails,
        notificationLooks,
        audioChannels,
        motionTuning,
        stickFeel,
        topBar,
        diagnostics,
        libraryOrganization,
    ]

    static var allSettings: [AdvancedSetting] { groups.flatMap { $0.settings } }

    static var hasCustomisedValues: Bool {
        allSettings.contains { !$0.isAtDefault() }
            || PerGameSettingsStore.shared.overridesByGame.values.contains { !$0.advancedFields.isIdentity }
    }

    static var hasSnapshot: Bool {
        let defaults = UserDefaults.standard
        let values = defaults.dictionary(forKey: snapshotKey) ?? [:]
        return !values.isEmpty || defaults.data(forKey: perGameSnapshotKey) != nil
    }

    static func switchToBasic() {
        let defaults = UserDefaults.standard
        let perGame = PerGameSettingsStore.shared
        if hasCustomisedValues {
            var values: [String: Any] = [:]
            for setting in allSettings {
                if let stored = defaults.object(forKey: setting.key) { values[setting.key] = stored }
            }
            defaults.set(values, forKey: snapshotKey)
            let advancedPerGame = perGame.overridesByGame
                .mapValues { $0.advancedFields }
                .filter { !$0.value.isIdentity }
            if advancedPerGame.isEmpty {
                defaults.removeObject(forKey: perGameSnapshotKey)
            } else if let data = try? JSONEncoder().encode(advancedPerGame) {
                defaults.set(data, forKey: perGameSnapshotKey)
            }
        }
        clearAdvanced()
        NotificationCenter.default.post(name: .espressuSettingsWereReset, object: nil)
    }

    static func clearAdvanced() {
        let defaults = UserDefaults.standard
        let perGame = PerGameSettingsStore.shared
        for setting in allSettings { defaults.removeObject(forKey: setting.key) }
        perGame.replaceOverrides(perGame.overridesByGame.mapValues { $0.removingAdvancedFields() })
        applyToBridge()
    }

    static func restoreSnapshot() {
        let defaults = UserDefaults.standard
        let values = defaults.dictionary(forKey: snapshotKey) ?? [:]
        let known = Set(allSettings.map { $0.key })
        for (key, value) in values where known.contains(key) {
            defaults.set(value, forKey: key)
        }
        if let data = defaults.data(forKey: perGameSnapshotKey),
           let saved = try? JSONDecoder().decode([String: GameOverrides].self, from: data) {
            let perGame = PerGameSettingsStore.shared
            var next = perGame.overridesByGame
            for (game, advanced) in saved {
                next[game] = (next[game] ?? .identity).adopting(advancedFieldsOf: advanced)
            }
            perGame.replaceOverrides(next)
        }
        discardSnapshot()
        applyToBridge()
        NotificationCenter.default.post(name: .espressuSettingsWereReset, object: nil)
    }

    static func discardSnapshot() {
        let defaults = UserDefaults.standard
        defaults.removeObject(forKey: snapshotKey)
        defaults.removeObject(forKey: perGameSnapshotKey)
    }

    static func resetModeAndSnapshot() {
        discardSnapshot()
        UserDefaults.standard.removeObject(forKey: SettingsMode.storageKey)
    }

    static func applyToBridge() {
        for group in groups { group.applyToBridge?() }
    }
}

extension GameOverrides {
    var advancedFields: GameOverrides {
        var copy = self
        copy.preCompileShaders = nil
        return copy
    }

    func removingAdvancedFields() -> GameOverrides {
        var copy = GameOverrides()
        copy.preCompileShaders = preCompileShaders
        return copy
    }

    func adopting(advancedFieldsOf other: GameOverrides) -> GameOverrides {
        var copy = other
        copy.preCompileShaders = preCompileShaders
        return copy
    }
}

extension PerGameSettingsStore {
    func activeOverrides(for gameID: String) -> GameOverrides {
        let stored = overrides(for: gameID)
        return SettingsMode.isAdvanced ? stored : stored.removingAdvancedFields()
    }
}
