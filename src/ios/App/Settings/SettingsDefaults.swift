// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

extension Notification.Name {
    static let espressuSettingsWereReset = Notification.Name("espressu.settings.wereReset")
}

enum SettingsDefaults {
    private static let alwaysExcludedKeys: Set<String> = [
        "espressu.theme.selectedId",
        "espressu.cpu.autoDemoted",
        "espressu.cpu.autoMultiPending",
        "espressu.render.vulkanFailedBuild",
        "espressu.render.vulkanFailureReason",
        "espressu.timebase.clearedAccidentalChoice",
    ]

    private static let alwaysExcludedPrefixes = ["espressu.library.", "espressu.graphicPacks."]

    private static let perGamePrefix = "espressu.touchlab.adaptive."

    private static let notPortableKeys: Set<String> = [
        PerGameSettingsStore.storageKey,
        "espressu.perGame.swappedGlobals",
        "espressu.thermal.scaleBeforeThrottle",
        "espressu.onboarding.reopen",
        OnboardingState.completedKey,
    ]
    private static let notPortablePrefixes = [
        "espressu.theme.", "espressu.hints.", "espressu.title.", "espressu.settings.advancedSnapshot",
    ]

    static func isPortableSetting(_ key: String) -> Bool {
        if key == RenderScale.storageKey || key == TimebaseScale.storageKey { return true }
        guard key.hasPrefix("espressu.") else { return false }
        if alwaysExcludedKeys.contains(key) || notPortableKeys.contains(key) { return false }
        if alwaysExcludedPrefixes.contains(where: { key.hasPrefix($0) }) { return false }
        if notPortablePrefixes.contains(where: { key.hasPrefix($0) }) { return false }
        return !key.hasPrefix(perGamePrefix)
    }

    @MainActor
    static func reset(includingPerGameOverrides: Bool) {
        let defaults = UserDefaults.standard
        ThermalMonitor.shared.titleStopped()
        var excluded = alwaysExcludedKeys
        if !includingPerGameOverrides {
            excluded.insert(PerGameSettingsStore.storageKey)
        }
        defaults.removeObject(forKey: RenderScale.storageKey)
        DisplayRouter.shared.reapplyRenderScale(reason: "settings reset")
        TimebaseScale.clearChoice()
        AdvancedSettings.resetModeAndSnapshot()
        for key in defaults.dictionaryRepresentation().keys where key.hasPrefix("espressu.") {
            if excluded.contains(key) { continue }
            if alwaysExcludedPrefixes.contains(where: { key.hasPrefix($0) }) { continue }
            if !includingPerGameOverrides && key.hasPrefix(perGamePrefix) { continue }
            defaults.removeObject(forKey: key)
        }
        if includingPerGameOverrides {
            PerGameSettingsStore.shared.removeAllOverrides()
        }
        UIStyleStore.shared.reloadFromDefaults()
        ControllerCustomLayout.shared.resetAll()
        PreviewPadStore.shared.reloadFromDefaults()
        pushDefaultsToBridge()
        DevicePairing.shared.cancel()
        StikJITSetup.removePairingFile()
        StikJITCoordinator.shared.settingsWereReset()
        NotificationCenter.default.post(name: .espressuSettingsWereReset, object: nil)
    }

    @MainActor
    private static func pushDefaultsToBridge() {
        cemu_bridge_set_recompiler_enabled(true)
        cemu_bridge_set_favour_accuracy(false)
        cemu_bridge_set_favour_performance(FavourPerformance.defaultValue)
        cemu_bridge_set_full_speed_renders(FullSpeedRenders.defaultValue, Int32(FullSpeedRenders.defaultShaderMode.rawValue))
        cemu_bridge_set_low_power_mode(OneCoreMode.defaultValue)
        MotionSettings.applyToBridge()
        cemu_bridge_set_cpu_core_mode(CoreMode.defaultValue.bridgeValue)
        cemu_bridge_set_async_shader_compile(true)
        cemu_bridge_set_vsync_enabled(true)
        cemu_bridge_set_stretch_to_fill(FrameStretch.defaultValue)
        cemu_bridge_set_render_upside_down(false)
        cemu_bridge_set_framebuffer_fetch(true)
        cemu_bridge_set_display_gamma(Float(DisplayGammaSetting.defaultValue))
        cemu_bridge_set_override_app_gamma(false)
        cemu_bridge_set_override_gamma_value(Float(OverrideGammaSetting.defaultValue))
        PerformanceTuning.applyDefaultsToBridge()

        cemu_bridge_set_tv_audio_enabled(AudioSettings.defaultTvEnabled)
        cemu_bridge_set_tv_volume(Int32(AudioSettings.defaultTvVolume))
        cemu_bridge_set_tv_channels(Int32(AudioSettings.defaultTvChannels))
        cemu_bridge_set_pad_audio_enabled(AudioSettings.defaultPadEnabled)
        cemu_bridge_set_pad_volume(Int32(AudioSettings.defaultPadVolume))
        cemu_bridge_set_pad_channels(Int32(AudioSettings.defaultPadChannels))
        cemu_bridge_set_microphone_enabled(AudioSettings.defaultMicrophoneEnabled)
        cemu_bridge_set_input_volume(Int32(AudioSettings.defaultInputVolume))

        cemu_bridge_set_overlay_position(Int32(OverlaySettings.defaultPosition.rawValue))
        cemu_bridge_set_overlay_text_color(UInt32(truncatingIfNeeded: OverlaySettings.defaultTextColor))
        cemu_bridge_set_overlay_text_scale(Int32(OverlaySettings.defaultTextScale))
        cemu_bridge_set_overlay_fps(OverlaySettings.defaultFps)
        cemu_bridge_set_overlay_drawcalls(OverlaySettings.defaultDrawcalls)
        cemu_bridge_set_overlay_cpu_usage(OverlaySettings.defaultCpuUsage)
        cemu_bridge_set_overlay_cpu_per_core_usage(OverlaySettings.defaultCpuPerCoreUsage)
        cemu_bridge_set_overlay_ram_usage(OverlaySettings.defaultRamUsage)
        cemu_bridge_set_overlay_vram_usage(OverlaySettings.defaultVramUsage)
        cemu_bridge_set_overlay_debug(OverlaySettings.defaultDebug)

        cemu_bridge_set_notification_position(Int32(NotificationSettings.defaultPosition.rawValue))
        cemu_bridge_set_notification_text_color(UInt32(truncatingIfNeeded: NotificationSettings.defaultTextColor))
        cemu_bridge_set_notification_text_scale(Int32(NotificationSettings.defaultTextScale))
        cemu_bridge_set_notification_controller_profiles(NotificationSettings.defaultControllerProfiles)
        cemu_bridge_set_notification_controller_battery(NotificationSettings.defaultControllerBattery)
        cemu_bridge_set_notification_shader_compiling(NotificationSettings.defaultShaderCompiling)
        cemu_bridge_set_notification_friends(NotificationSettings.defaultFriends)
    }
}
