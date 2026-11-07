// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

import Foundation

/// Graphics and diagnostics options the front end hands to the core (HostTuning).
/// Freeing memory on warnings, the GPU fault history and skipping duplicate JIT work and keeping the TV smooth
/// with both screens used to be experimental switches and are always on now.
enum PerformanceTuning {
    static let metalFxUpscaleKey = "espressu.tuning.metalFxUpscale"
    static let metalFxUpscaleDefault = false
    static let padLowResolutionKey = "espressu.tuning.padLowResolution"
    static let padLowResolutionDefault = false
    /// "Logs" in Device report and logs: a summary line in the EspressU report every few seconds.
    static let logsKey = "espressu.logs.enabled"
    static let logsDefault = true

    static var advancedSettings: [AdvancedSetting] {
        [
            AdvancedSetting(metalFxUpscaleKey, metalFxUpscaleDefault),
            AdvancedSetting(padLowResolutionKey, padLowResolutionDefault),
            AdvancedSetting(logsKey, logsDefault),
        ]
    }

    static func bool(_ key: String, _ fallback: Bool) -> Bool {
        UserDefaults.standard.object(forKey: key) as? Bool ?? fallback
    }

    static func applyToBridge() {
        applyAlwaysOn()
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METALFX_UPSCALE,
                               bool(metalFxUpscaleKey, metalFxUpscaleDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_LOW_RESOLUTION,
                               bool(padLowResolutionKey, padLowResolutionDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_DIAGNOSTICS_LOG,
                               bool(logsKey, logsDefault) ? 1 : 0)
    }

    static func applyDefaultsToBridge() {
        applyAlwaysOn()
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METALFX_UPSCALE, metalFxUpscaleDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_LOW_RESOLUTION, padLowResolutionDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_DIAGNOSTICS_LOG, logsDefault ? 1 : 0)
    }

    private static func applyAlwaysOn() {
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING, 1)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY, 1)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_JIT_SKIP_OVERLAPS, 1)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_PRESENT_PACING, 1)
    }
}
