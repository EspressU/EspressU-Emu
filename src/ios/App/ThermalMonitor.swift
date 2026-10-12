// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import Combine
#if os(iOS)
import UIKit
#endif

enum ThermalSettings {
    static let scaleKey = "espressu.thermal.scaleBeforeThrottle"

    static var isHoldingScale: Bool { UserDefaults.standard.string(forKey: scaleKey) != nil }

    static let thresholdKey = "espressu.thermal.coolDownThreshold"
    static let defaultThreshold = Threshold.serious

    enum Threshold: String, CaseIterable, Identifiable {
        case serious
        case critical

        var id: String { rawValue }

        var title: String {
            switch self {
            case .serious:  return "Serious"
            case .critical: return "Critical"
            }
        }

        var summary: String {
            switch self {
            case .serious:  return "Cools down as soon as iOS starts slowing the device."
            case .critical: return "Waits until iOS is slowing the device hard. The game keeps its quality longer, and may stutter or run hot first."
            }
        }
    }

    static var threshold: Threshold {
        Threshold(rawValue: UserDefaults.standard.string(forKey: thresholdKey) ?? "") ?? defaultThreshold
    }
}

@MainActor
final class ThermalMonitor: ObservableObject {
    static let shared = ThermalMonitor()

    static let autoThrottleKey = "espressu.thermal.autoReduceQuality"
    static let autoThrottleDefault = true

    @Published private(set) var state: ProcessInfo.ThermalState = ProcessInfo.processInfo.thermalState

    private var userChosenScale: RenderScale? {
        get { RenderScale(rawValue: UserDefaults.standard.string(forKey: Self.scaleBeforeThrottleKey) ?? "") }
        set {
            if let newValue {
                UserDefaults.standard.set(newValue.rawValue, forKey: Self.scaleBeforeThrottleKey)
            } else {
                UserDefaults.standard.removeObject(forKey: Self.scaleBeforeThrottleKey)
            }
        }
    }

    private static let scaleBeforeThrottleKey = ThermalSettings.scaleKey
    private var isThrottling = false
    private var observing = false

    var onNotice: (@MainActor (String) -> Void)?

    private init() {}

    var autoThrottleEnabled: Bool {
        UserDefaults.standard.object(forKey: Self.autoThrottleKey) as? Bool ?? Self.autoThrottleDefault
    }

    var description: String {
        switch state {
        case .nominal:  return "nominal"
        case .fair:     return "fair"
        case .serious:  return "serious (iOS is throttling)"
        case .critical: return "critical (iOS is throttling hard)"
        @unknown default: return "unknown"
        }
    }

    private func restoreScaleIfKilledWhileThrottled() {
        guard let chosen = userChosenScale else { return }
        UserDefaults.standard.set(chosen.rawValue, forKey: RenderScale.storageKey)
        userChosenScale = nil
        cemu_bridge_log_line("iOS thermal: last run ended while throttled; restored render scale to \(chosen.rawValue)")
    }

    func thresholdChanged() {
        applyAutoThrottleIfNeeded()
    }

    func startObserving() {
        guard !observing else { return }
        observing = true
        restoreScaleIfKilledWhileThrottled()
        state = ProcessInfo.processInfo.thermalState
        NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification,
            object: nil,
            queue: .main) { [weak self] _ in
                Task { @MainActor in self?.thermalStateChanged() }
            }
        cemu_bridge_log_line("iOS thermal: monitoring started, state \(description)")
    }

    private func thermalStateChanged() {
        let newState = ProcessInfo.processInfo.thermalState
        guard newState != state else { return }
        state = newState
        cemu_bridge_log_line("iOS thermal: state changed to \(description)")
        applyAutoThrottleIfNeeded()
    }

    private func throttleMicros(for state: ProcessInfo.ThermalState) -> UInt32 {
        switch state {
        case .serious:  return 0
        case .critical: return 125
        default:        return 0
        }
    }

    private func applyAutoThrottleIfNeeded() {
        guard autoThrottleEnabled else {
            if isThrottling { unwind(reason: "auto-reduce turned off") }
            return
        }

        let shouldThrottle = state == .critical || (state == .serious && ThermalSettings.threshold == .serious)

        let micros: UInt32 = shouldThrottle ? throttleMicros(for: state) : 0
        cemu_bridge_set_thermal_throttle_micros(micros)

        if shouldThrottle && !isThrottling {
            userChosenScale = RenderScale.storedChoice
            UserDefaults.standard.set(RenderScale.battery.rawValue, forKey: RenderScale.storageKey)
            isThrottling = true
            DisplayRouter.shared.reapplyRenderScale(reason: "thermal state \(description)")
            cemu_bridge_log_line("iOS thermal: reduced render scale to battery saver while hot")
            onNotice?("The device is hot, so EspressU made the picture softer to keep the game running. It goes back when the device cools.")
        } else if !shouldThrottle && isThrottling {
            unwind(reason: "cooled to \(description)")
            onNotice?("The device has cooled down. The picture is back to normal.")
        }
    }

    private func unwind(reason: String) {
        cemu_bridge_set_thermal_throttle_micros(0)
        restoreChosenScale()
        isThrottling = false
        DisplayRouter.shared.reapplyRenderScale(reason: "thermal: \(reason)")
        cemu_bridge_log_line("iOS thermal: released the governor and restored the chosen render scale (\(reason))")
    }

    private func restoreChosenScale() {
        if let restored = userChosenScale, RenderScale.storedChoice == .battery {
            UserDefaults.standard.set(restored.rawValue, forKey: RenderScale.storageKey)
        }
        userChosenScale = nil
    }

    func titleStopped() {
        cemu_bridge_set_thermal_throttle_micros(0)
        guard isThrottling else { return }
        restoreChosenScale()
        isThrottling = false
        cemu_bridge_log_line("iOS thermal: title stopped while throttled; governor released and render scale restored")
    }
}
