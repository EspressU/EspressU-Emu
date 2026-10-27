import SwiftUI
import GameController

enum MotionSettings {
    static let enabledKey = "espressu.motion.enabled"
    static let sensitivityKey = "espressu.motion.sensitivity"
    static let sourceKey = "espressu.motion.source"
    static let invertHorizontalKey = "espressu.motion.invertHorizontal"
    static let invertVerticalKey = "espressu.motion.invertVertical"
    static let diagnosticKey = "espressu.motion.diagnostic"

    static let defaultEnabled = true
    static let defaultSensitivity = 1.0
    static let minSensitivity = 0.25
    static let maxSensitivity = 3.0

    static let sourceDevice = 0
    static let sourceController = 1
    static let sourceAutomatic = 2
    static let defaultSource = sourceAutomatic
    static let statusOff = 0
    static let statusDevice = 1
    static let statusController = 2
    static let statusUnavailable = 3

    static func resolvedSource(_ stored: Int) -> Int {
        guard stored == sourceAutomatic else { return stored }
        return GCController.controllers().isEmpty ? sourceDevice : sourceController
    }

    static func startObservingControllers() {
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect] {
            NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { _ in applyToBridge() }
        }
    }

    static func applyToBridge() {
        let defaults = UserDefaults.standard
        cemu_bridge_set_motion_enabled(defaults.object(forKey: enabledKey) as? Bool ?? defaultEnabled)
        let sensitivity = defaults.object(forKey: sensitivityKey) as? Double ?? defaultSensitivity
        cemu_bridge_set_motion_sensitivity(Float(sensitivity))
        cemu_bridge_set_motion_source(Int32(resolvedSource(defaults.object(forKey: sourceKey) as? Int ?? defaultSource)))
        cemu_bridge_set_motion_invert(defaults.bool(forKey: invertHorizontalKey), defaults.bool(forKey: invertVerticalKey))
        cemu_bridge_set_motion_diagnostic(defaults.bool(forKey: diagnosticKey))
    }
}
