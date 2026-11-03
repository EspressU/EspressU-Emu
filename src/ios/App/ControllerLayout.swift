// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import UIKit

/// Stored settings for the on-screen controls (Crema). Older builds stored more keys here; whatever they
/// left in UserDefaults is simply ignored.
enum ControllerLayoutSettings {
    static let scaleKey = "espressu.controls.scale"
    static let opacityKey = "espressu.controls.opacity"
    static let deadzoneKey = "espressu.controls.stick.deadzone"
    static let stickSpacingKey = "espressu.controls.stickSpacing"
    static let shoulderOffsetKey = "espressu.controls.shoulderOffset"
    /// One JSON string with every control the player moved or resized on its own (see `CremaElementOverrides`).
    static let elementsKey = "espressu.controls.elements"
    static let hapticsKey = "espressu.pad.haptics"
    static let autoHideWithControllerKey = "espressu.controls.autoHideWithController"

    static let defaultScale: Double = 1.0
    static let minScale: Double = 0.6
    static let maxScale: Double = 1.6

    static let defaultOpacity: Double = 0.85

    static let defaultDeadzone: Double = 0.06
    static let minDeadzone: Double = 0.0
    static let maxDeadzone: Double = 0.30

    static let defaultStickSpacing: Double = 0
    static let minStickSpacing: Double = -3.0
    static let maxStickSpacing: Double = 1.5
    static let stickSpacingStep: Double = 0.25

    static let defaultShoulderOffset: Double = 0
    static let minShoulderOffset: Double = -4.0
    static let maxShoulderOffset: Double = 1.5
    static let shoulderOffsetStep: Double = 0.25

    static let defaultElements = ""

    static let defaultHaptics = true
    static let defaultAutoHideWithController = true

    static func stickSpacingLabel(_ value: Double) -> String {
        abs(value) < 0.01 ? "default" : (value < 0 ? "closer" : "wider")
    }

    static func shoulderOffsetLabel(_ value: Double) -> String {
        abs(value) < 0.01 ? "default" : (value < 0 ? "higher" : "lower")
    }

    static var supportsShoulderOffset: Bool {
        UIDevice.current.userInterfaceIdiom == .pad
    }

    static func effectiveShoulderOffset(_ stored: Double) -> Double {
        supportsShoulderOffset ? stored : defaultShoulderOffset
    }

    /// Puts size, opacity, spacing and shoulder height back to how EspressU ships them.
    static func reset() {
        let defaults = UserDefaults.standard
        for key in [scaleKey, opacityKey, stickSpacingKey, shoulderOffsetKey, elementsKey] {
            defaults.removeObject(forKey: key)
        }
    }
}
