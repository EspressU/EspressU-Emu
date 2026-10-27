// SPDX-License-Identifier: MPL-2.0

import Foundation

enum RemovedSettingsCleanup {
    private static let doneKey = "espressu.cleanup.removedSettings.2"

    static func run() {
        let defaults = UserDefaults.standard
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
