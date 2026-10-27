// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

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

}

@MainActor
enum HeatStatus {
    /// Heat comes only from `ProcessInfo.thermalState`; iOS offers no public temperature reading.
    static var band: HeatBand {
        .from(thermalState: ThermalMonitor.shared.state)
    }
}

struct HeatStatusBadge: View {
    @ObservedObject private var thermal = ThermalMonitor.shared
    var body: some View {
        let band = HeatStatus.band
        return HStack(spacing: 6) {
            Image(systemName: "thermometer.medium")
                .font(.system(size: 12, weight: .bold))
            Text(band.word)
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
