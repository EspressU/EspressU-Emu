// SPDX-License-Identifier: MPL-2.0

import Foundation

enum RemovedSettingsCleanup {
    private static let doneKey = "espressu.cleanup.removedSettings.1"

    static func run() {
        let defaults = UserDefaults.standard
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
