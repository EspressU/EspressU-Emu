// SPDX-License-Identifier: MPL-2.0

import Foundation

/// Experimental performance options. Each one is off by default and keeps the
/// behaviour EspressU had before the option existed.
enum PerformanceTuning {
    static let freeMemoryOnWarningKey = "espressu.tuning.freeMemoryOnWarning"
    static let freeMemoryOnWarningDefault = false
    static let gpuFaultHistoryKey = "espressu.tuning.gpuFaultHistory"
    static let gpuFaultHistoryDefault = false
    static let metalFxUpscaleKey = "espressu.tuning.metalFxUpscale"
    static let metalFxUpscaleDefault = false
    static let metal4OutputKey = "espressu.tuning.metal4Output"
    static let metal4OutputDefault = false

    static var advancedSettings: [AdvancedSetting] {
        [
            AdvancedSetting(freeMemoryOnWarningKey, freeMemoryOnWarningDefault),
            AdvancedSetting(gpuFaultHistoryKey, gpuFaultHistoryDefault),
            AdvancedSetting(metalFxUpscaleKey, metalFxUpscaleDefault),
            AdvancedSetting(metal4OutputKey, metal4OutputDefault),
        ]
    }

    static func bool(_ key: String, _ fallback: Bool) -> Bool {
        UserDefaults.standard.object(forKey: key) as? Bool ?? fallback
    }

    static func applyToBridge() {
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING,
                               bool(freeMemoryOnWarningKey, freeMemoryOnWarningDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY,
                               bool(gpuFaultHistoryKey, gpuFaultHistoryDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METALFX_UPSCALE,
                               bool(metalFxUpscaleKey, metalFxUpscaleDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METAL4_OUTPUT,
                               bool(metal4OutputKey, metal4OutputDefault) ? 1 : 0)
    }

    static func applyDefaultsToBridge() {
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING, freeMemoryOnWarningDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY, gpuFaultHistoryDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METALFX_UPSCALE, metalFxUpscaleDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METAL4_OUTPUT, metal4OutputDefault ? 1 : 0)
    }
}
