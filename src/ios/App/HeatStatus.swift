// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

enum HeatDisplayMode: String, CaseIterable, Identifiable {
    case word
    case celsius
    case fahrenheit

    var id: String { rawValue }

    var title: String {
        switch self {
        case .word:       return "Word"
        case .celsius:    return "°C"
        case .fahrenheit: return "°F"
        }
    }

    static let storageKey = "espressu.thermal.displayMode"
    static var current: HeatDisplayMode {
        guard let raw = UserDefaults.standard.string(forKey: storageKey),
              let value = HeatDisplayMode(rawValue: raw) else { return .word }
        return value
    }
}

enum HeatBand: Int, Comparable {
    case cool = 0
    case fair
    case warm
    case hot
    case extremelyHot

    static func < (lhs: HeatBand, rhs: HeatBand) -> Bool { lhs.rawValue < rhs.rawValue }

    var word: String {
        switch self {
        case .cool:         return "Cool"
        case .fair:         return "Fair"
        case .warm:         return "Warm"
        case .hot:          return "Hot"
        case .extremelyHot: return "Extremely Hot"
        }
    }

    var color: Color {
        switch self {
        case .cool:         return EspressUTheme.pixelBlue
        case .fair:         return Color(red: 0.16, green: 0.62, blue: 0.35)
        case .warm:         return EspressUTheme.cremaTopDark
        case .hot:          return Color(red: 0.90, green: 0.45, blue: 0.10)
        case .extremelyHot: return Color(red: 0.85, green: 0.18, blue: 0.18)
        }
    }

    static func from(thermalState: ProcessInfo.ThermalState) -> HeatBand {
        switch thermalState {
        case .nominal:  return .cool
        case .fair:     return .warm
        case .serious:  return .hot
        case .critical: return .extremelyHot
        @unknown default: return .warm
        }
    }

    static func from(celsius: Double) -> HeatBand {
        switch celsius {
        case ..<30:  return .cool
        case ..<35:  return .fair
        case ..<40:  return .warm
        case ..<45:  return .hot
        default:     return .extremelyHot
        }
    }
}

@MainActor
enum HeatStatus {
    static var temperatureCelsius: Double? {
        let value = cemu_bridge_device_temperature_celsius()
        return value.isNaN ? nil : value
    }

    static var band: HeatBand {
        if let celsius = temperatureCelsius {
            return .from(celsius: celsius)
        }
        return .from(thermalState: ThermalMonitor.shared.state)
    }

    static func text(for mode: HeatDisplayMode) -> String {
        let band = band
        guard mode != .word else { return band.word }
        guard let celsius = temperatureCelsius else { return band.word }
        switch mode {
        case .celsius:    return String(format: "%.0f°C", celsius)
        case .fahrenheit: return String(format: "%.0f°F", celsius * 9.0 / 5.0 + 32.0)
        case .word:       return band.word
        }
    }

    static var hasRealTemperature: Bool { temperatureCelsius != nil }
}

struct HeatStatusBadge: View {
    @ObservedObject private var thermal = ThermalMonitor.shared
    @AppStorage(HeatDisplayMode.storageKey) private var mode = HeatDisplayMode.word.rawValue

    private var displayMode: HeatDisplayMode { HeatDisplayMode(rawValue: mode) ?? .word }

    var body: some View {
        let band = HeatStatus.band
        return HStack(spacing: 6) {
            Image(systemName: "thermometer.medium")
                .font(.system(size: 12, weight: .bold))
            Text(HeatStatus.text(for: displayMode))
                .font(.system(size: 13, weight: .bold))
        }
        .foregroundColor(UIStyle.isClassic
                         ? LegibleInk.ensure(band.color, on: Color(.secondarySystemGroupedBackground))
                         : LegibleInk.on(band.color, light: EspressUTheme.sparkleCream))
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(
            Group {
                if UIStyle.isClassic {
                    Color.clear
                } else {
                    Capsule().fill(band.color)
                }
            }
        )
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Device heat: \(band.word)")
    }
}
