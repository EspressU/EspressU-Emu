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
    static let padPresentPacingKey = "espressu.tuning.padPresentPacing"
    static let padPresentPacingDefault = false
    static let padLowResolutionKey = "espressu.tuning.padLowResolution"
    static let padLowResolutionDefault = false
    static let interpreterBlockLinkingKey = "espressu.tuning.interpreterBlockLinking"
    static let interpreterBlockLinkingDefault = false
    static let diagnosticsLogKey = "espressu.tuning.diagnosticsLog"
    static let diagnosticsLogDefault = false
    static let jitSkipOverlapsKey = "espressu.tuning.jitSkipOverlaps"
    static let jitSkipOverlapsDefault = false

    static var advancedSettings: [AdvancedSetting] {
        [
            AdvancedSetting(freeMemoryOnWarningKey, freeMemoryOnWarningDefault),
            AdvancedSetting(gpuFaultHistoryKey, gpuFaultHistoryDefault),
            AdvancedSetting(metalFxUpscaleKey, metalFxUpscaleDefault),
            AdvancedSetting(metal4OutputKey, metal4OutputDefault),
            AdvancedSetting(padPresentPacingKey, padPresentPacingDefault),
            AdvancedSetting(padLowResolutionKey, padLowResolutionDefault),
            AdvancedSetting(interpreterBlockLinkingKey, interpreterBlockLinkingDefault),
            AdvancedSetting(diagnosticsLogKey, diagnosticsLogDefault),
            AdvancedSetting(jitSkipOverlapsKey, jitSkipOverlapsDefault),
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
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_PRESENT_PACING,
                               bool(padPresentPacingKey, padPresentPacingDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_INTERPRETER_BLOCK_LINKING,
                               bool(interpreterBlockLinkingKey, interpreterBlockLinkingDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_LOW_RESOLUTION,
                               bool(padLowResolutionKey, padLowResolutionDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_DIAGNOSTICS_LOG,
                               bool(diagnosticsLogKey, diagnosticsLogDefault) ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_JIT_SKIP_OVERLAPS,
                               bool(jitSkipOverlapsKey, jitSkipOverlapsDefault) ? 1 : 0)
    }

    static func applyDefaultsToBridge() {
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING, freeMemoryOnWarningDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY, gpuFaultHistoryDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METALFX_UPSCALE, metalFxUpscaleDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_METAL4_OUTPUT, metal4OutputDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_PRESENT_PACING, padPresentPacingDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_INTERPRETER_BLOCK_LINKING, interpreterBlockLinkingDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_PAD_LOW_RESOLUTION, padLowResolutionDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_DIAGNOSTICS_LOG, diagnosticsLogDefault ? 1 : 0)
        cemu_bridge_set_tuning(CEMU_BRIDGE_TUNING_JIT_SKIP_OVERLAPS, jitSkipOverlapsDefault ? 1 : 0)
    }
}
