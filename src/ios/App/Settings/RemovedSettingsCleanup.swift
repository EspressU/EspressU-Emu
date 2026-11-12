// SPDX-License-Identifier: MPL-2.0

import Foundation

enum RemovedSettingsCleanup {
    private static let doneKey = "espressu.cleanup.removedSettings.2"

    static func run() {
        let defaults = UserDefaults.standard
        // The "Faster interpreter without JIT" switch is gone: block linking is always on now. Drop a stored
        // value from older versions every launch, so nothing can bring an old "off" back.
        defaults.removeObject(forKey: "espressu.tuning.interpreterBlockLinking")
        // Threaded dispatch was only an experiment on the fast-interpreter branch and is gone.
        defaults.removeObject(forKey: "espressu.tuning.interpreterThreaded")
        // "Faster interpreter (beta)" is gone too: the deferred instruction pointer is always on now.
        defaults.removeObject(forKey: "espressu.tuning.interpreterFast")
        // Free memory on warnings, the GPU fault history, "No duplicate JIT work" and "Keep the TV smooth with both screens" are always on now.
        defaults.removeObject(forKey: "espressu.tuning.freeMemoryOnWarning")
        defaults.removeObject(forKey: "espressu.tuning.gpuFaultHistory")
        defaults.removeObject(forKey: "espressu.tuning.jitSkipOverlaps")
        defaults.removeObject(forKey: "espressu.tuning.padPresentPacing")
        // "Log the experimental options" became "Logs" (on by default) under its own key.
        defaults.removeObject(forKey: "espressu.tuning.diagnosticsLog")
        defaults.removeObject(forKey: "espressu.tuning.metalFxUpscale")
        // The first frame generation (on the Metal 4 output) is gone; its stored mode must not turn the new
        // switch on. The new one lives under "espressu.tuning.frameInterpolation".
        defaults.removeObject(forKey: "espressu.tuning.frameGeneration")
        guard !defaults.bool(forKey: doneKey) else { return }
        for key in [
            "espressu.cpu.favourAccuracy", FavourPerformance.storageKey, OneCoreMode.storageKey,
            ThermalSettings.thresholdKey,
            MotionSettings.invertHorizontalKey, MotionSettings.invertVerticalKey, MotionSettings.diagnosticKey,
            ExternalDisplaySystemSettings.enabledKey,
            "espressu.topBar.autoHideOverride", TopBarAutoHide.hideDelayKey, TopBarAutoHide.handleSizeKey,
        ] {
            defaults.removeObject(forKey: key)
        }
        defaults.removeObject(forKey: MotionSettings.sourceKey)
        removeStoreBuildLeftovers(defaults)
        defaults.set(true, forKey: doneKey)
    }

    /// Keys written by earlier builds for features the Store build no longer has
    /// (JIT, online accounts, graphic packs, network cover art).
    private static func removeStoreBuildLeftovers(_ defaults: UserDefaults) {
        let exact = [
            "espressu.cpu.recompiler", "espressu.tuning.jitSkipOverlaps",
            "espressu.jit.enabled", "espressu.jit.forceScript",
            "espressu.thermal.displayMode",
        ]
        let prefixes = ["espressu.jit.", "espressu.stikjit.", "espressu.pairing.", "espressu.graphicPacks.",
                        "espressu.cover.fetch", "espressu.network.", "espressu.gamedata."]
        for key in exact { defaults.removeObject(forKey: key) }
        for key in defaults.dictionaryRepresentation().keys where prefixes.contains(where: { key.hasPrefix($0) }) {
            defaults.removeObject(forKey: key)
        }
    }
}
