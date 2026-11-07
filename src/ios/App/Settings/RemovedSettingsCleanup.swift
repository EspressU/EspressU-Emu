// SPDX-License-Identifier: MPL-2.0

import Foundation

enum RemovedSettingsCleanup {
    private static let doneKey = "espressu.cleanup.removedSettings.1"

    static func run() {
        let defaults = UserDefaults.standard
        // The "Faster interpreter without JIT" switch is gone: block linking is always on now. Drop a stored
        // value from older versions every launch, so nothing can bring an old "off" back.
        defaults.removeObject(forKey: "espressu.tuning.interpreterBlockLinking")
        // Threaded dispatch was only an experiment on the fast-interpreter branch and is gone.
        defaults.removeObject(forKey: "espressu.tuning.interpreterThreaded")
        // "Faster interpreter (beta)" is gone too: the deferred instruction pointer is always on now.
        defaults.removeObject(forKey: "espressu.tuning.interpreterFast")
        guard !defaults.bool(forKey: doneKey) else { return }
        for key in [
            "espressu.cpu.favourAccuracy", FavourPerformance.storageKey, OneCoreMode.storageKey,
            ThermalSettings.thresholdKey,
            MotionSettings.invertHorizontalKey, MotionSettings.invertVerticalKey, MotionSettings.diagnosticKey,
            StikJITSetup.forceScriptKey,
            ExternalDisplaySystemSettings.enabledKey,
            "espressu.topBar.autoHideOverride", TopBarAutoHide.hideDelayKey, TopBarAutoHide.handleSizeKey,
        ] {
            defaults.removeObject(forKey: key)
        }
        defaults.removeObject(forKey: MotionSettings.sourceKey)
        defaults.set(true, forKey: doneKey)
    }
}
