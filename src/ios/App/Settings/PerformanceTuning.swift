// SPDX-License-Identifier: MPL-2.0

import Foundation

/// Experimental performance options. Each one is off by default and keeps the
/// behaviour EspressU had before the option existed.
enum PerformanceTuning {
    static let freeMemoryOnWarningKey = "espressu.tuning.freeMemoryOnWarning"
    static let freeMemoryOnWarningDefault = false
    static let gpuFaultHistoryKey = "espressu.tuning.gpuFaultHistory"
    static let gpuFaultHistoryDefault = false

    static var advancedSettings: [AdvancedSetting] {
        [
            AdvancedSetting(freeMemoryOnWarningKey, freeMemoryOnWarningDefault),
            AdvancedSetting(gpuFaultHistoryKey, gpuFaultHistoryDefault),
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
    }

    static func applyDefaultsToBridge() {
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING, freeMemoryOnWarningDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY, gpuFaultHistoryDefault ? 1 : 0)
    }
}
