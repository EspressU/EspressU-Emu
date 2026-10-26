// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
#if os(iOS)
import UIKit
#endif

enum RenderScale: String, CaseIterable, Identifiable {
    case native
    case high
    case balanced
    case battery

    var id: String { rawValue }

    var factor: Double {
        switch self {
        case .native:   return 1.0
        case .high:     return 0.75
        case .balanced: return 0.5
        case .battery:  return 0.375
        }
    }

    var title: String {
        switch self {
        case .native:   return "Native"
        case .high:     return "High"
        case .balanced: return "Balanced"
        case .battery:  return "Battery saver"
        }
    }

    var summary: String {
        switch self {
        case .native:   return "Sharpest. Four times the pixels of Balanced, and the shortest battery life."
        case .high:     return "Slightly softer than Native, noticeably cheaper to draw."
        case .balanced: return "Still above the Wii U's own 720p. The best trade on most games."
        case .battery:  return "Softest, coolest, longest-running. Use it when a game won't hold its frame rate."
        }
    }

    static let storageKey = "renderScale"

    static var storedChoice: RenderScale {
        guard let raw = UserDefaults.standard.string(forKey: storageKey),
              let value = RenderScale(rawValue: raw) else { return deviceDefault }
        return value
    }

    static var current: RenderScale {
        if !ThermalSettings.isHoldingScale,
           let own = ActiveGameSettings.overrides.renderScale.flatMap(RenderScale.init(rawValue:)) {
            return own
        }
        return storedChoice
    }

    static var deviceDefault: RenderScale {
        #if os(iOS)
        if DeviceCapabilities.current.startsAtHighRenderScale {
            return .high
        }
        let screen = UIScreen.main
        let nativeScale = Double(screen.scale)
        let longEdge = Double(max(screen.bounds.width, screen.bounds.height)) * nativeScale
        let shortEdge = Double(min(screen.bounds.width, screen.bounds.height)) * nativeScale
        let memoryGiB = Double(ProcessInfo.processInfo.physicalMemory) / 1_073_741_824.0
        let pixelBudget: Double = memoryGiB >= 5.0 ? 2_500_000 : (memoryGiB >= 3.0 ? 2_000_000 : 1_600_000)
        let ladder: [RenderScale] = [.balanced, .high, .native]
        func pixels(_ scale: RenderScale) -> Double { longEdge * scale.factor * shortEdge * scale.factor }
        let sharpEnough = ladder.first { longEdge * $0.factor >= 1_100 } ?? .native
        if pixels(sharpEnough) <= pixelBudget { return sharpEnough }
        return ladder.last { pixels($0) <= pixelBudget } ?? .balanced
        #else
        return .balanced
        #endif
    }
}

enum OneCoreMode {
    static let storageKey = "espressu.cpu.lowPowerMode"
    static let defaultValue = false

    static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: storageKey) as? Bool ?? defaultValue
    }
}

enum FavourPerformance {
    static let storageKey = "espressu.cpu.favourPerformance"
    static let defaultValue = false

    static var isEnabled: Bool {
        ActiveGameSettings.overrides.favourPerformance
            ?? UserDefaults.standard.object(forKey: storageKey) as? Bool ?? defaultValue
    }
}

enum FullSpeedRenders {
    static let storageKey = "espressu.render.fullSpeedRenders"
    static let defaultValue = false
    static let shaderModeKey = "espressu.render.fullSpeedShaderMode"
    enum ShaderMode: Int, CaseIterable, Identifiable {
        case waitForIt = 0
        case keepGoing = 1
        var id: Int { rawValue }
        var title: String {
            switch self {
            case .waitForIt: return "Wait for it"
            case .keepGoing: return "Keep going"
            }
        }
        var summary: String {
            switch self {
            case .waitForIt: return "Never flickers. A short hitch the first time a new effect appears."
            case .keepGoing: return "Never freezes. A new effect can be missing for a moment the first time."
            }
        }
    }
    static let defaultShaderMode = ShaderMode.waitForIt

    static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: storageKey) as? Bool ?? defaultValue
    }
    static var shaderMode: ShaderMode {
        ShaderMode(rawValue: UserDefaults.standard.object(forKey: shaderModeKey) as? Int ?? defaultShaderMode.rawValue)
            ?? defaultShaderMode
    }
    static func applyToBridge() {
        cemu_bridge_set_full_speed_renders(isEnabled, Int32(shaderMode.rawValue))
    }
}

enum PadSurfaceScale {
    static let maxLongSidePixels: Double = 1708
    /// The Wii U GamePad's own width, used when the lower-resolution GamePad screen is chosen.
    static let lowResolutionLongSidePixels: Double = 854

    static func scale(forPoints size: CGSize, renderScale: Double) -> Double {
        let longSide = Double(max(size.width, size.height))
        guard longSide > 0 else { return renderScale }
        let lowResolution = PerformanceTuning.bool(PerformanceTuning.padLowResolutionKey, PerformanceTuning.padLowResolutionDefault)
        return max(0.5, min(renderScale, (lowResolution ? lowResolutionLongSidePixels : maxLongSidePixels) / longSide))
    }
}

enum CoreMode: String, CaseIterable, Identifiable {
    case auto, single, multi

    var id: String { rawValue }

    var title: String {
        switch self {
        case .auto:   return "Auto"
        case .single: return "One core"
        case .multi:  return "Three cores (Experimental)"
        }
    }

    var summary: String {
        switch self {
        case .auto:   return "Picks per game and per device. Uses three cores when the game's profile asks for them or this device has the performance cores, memory and cooling headroom, and one core otherwise."
        case .single: return "One core. Cooler, and often faster on devices without spare performance cores."
        case .multi:  return "Three cores. Can be faster on a device with spare performance cores and cooling, but heats up quickly on most iPads and has hung some games."
        }
    }

    var bridgeValue: Int32 {
        switch self {
        case .auto:   return 0
        case .single: return 1
        case .multi:  return 2
        }
    }

    static let storageKey = "espressu.cpu.coreMode"
    private static let legacyKey = "espressu.cpu.multicore"
    static let defaultValue: CoreMode = .auto

    static var current: CoreMode {
        let defaults = UserDefaults.standard
        if let raw = defaults.string(forKey: storageKey), let value = CoreMode(rawValue: raw) {
            return value
        }
        if let legacy = defaults.object(forKey: legacyKey) as? Bool {
            return legacy ? .multi : .single
        }
        return defaultValue
    }
}

enum AutoCoreHistory {
    private static let demotedKey = "espressu.cpu.autoDemoted"
    private static let pendingKey = "espressu.cpu.autoMultiPending"

    static func isDemotedAtLaunch(gameID: String) -> Bool {
        let defaults = UserDefaults.standard
        var demoted = Set(defaults.stringArray(forKey: demotedKey) ?? [])
        if let pending = defaults.string(forKey: pendingKey) {
            defaults.removeObject(forKey: pendingKey)
            if demoted.insert(pending).inserted {
                defaults.set(demoted.sorted(), forKey: demotedKey)
                cemu_bridge_log_line("CPU cores: \(pending) ended a three-core run without a clean stop, so Auto keeps it on one core from now on")
            }
        }
        return demoted.contains(gameID)
    }

    static func isDemoted(gameID: String) -> Bool {
        Set(UserDefaults.standard.stringArray(forKey: demotedKey) ?? []).contains(gameID)
    }

    static func adoptTitleIDKey(from oldID: String, to newID: String) {
        let defaults = UserDefaults.standard
        var demoted = Set(defaults.stringArray(forKey: demotedKey) ?? [])
        guard demoted.contains(oldID) else { return }
        demoted.remove(oldID)
        demoted.insert(newID)
        defaults.set(demoted.sorted(), forKey: demotedKey)
    }

    static func sessionStarted(gameID: String) {
        UserDefaults.standard.set(gameID, forKey: pendingKey)
    }

    static func sessionEnded(gameID: String, stalled: Bool) {
        let defaults = UserDefaults.standard
        guard defaults.string(forKey: pendingKey) == gameID else { return }
        defaults.removeObject(forKey: pendingKey)
        guard stalled else { return }
        var demoted = Set(defaults.stringArray(forKey: demotedKey) ?? [])
        if demoted.insert(gameID).inserted {
            defaults.set(demoted.sorted(), forKey: demotedKey)
            cemu_bridge_log_line("CPU cores: a three-core run of \(gameID) stalled, so Auto keeps it on one core from now on")
        }
    }
}

enum FrameStretch {
    static let storageKey = "espressu.render.stretchToFit"
    static let defaultValue = false

    static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: storageKey) as? Bool ?? defaultValue
    }
}

#if os(iOS)
extension UIScreen {
    var effectiveRenderScale: Double {
        var choice = RenderScale.current
        if FavourPerformance.isEnabled && choice.factor > RenderScale.balanced.factor {
            choice = .balanced
        }
        var value = Double(scale) * choice.factor
        if choice != .battery {
            let shortSide = Double(min(bounds.width, bounds.height))
            if shortSide > 0 {
                value = max(value, min(720.0 / shortSide, Double(scale)))
            }
        }
        return max(0.5, value)
    }
}
#endif
