// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Metal

enum RendererAPI: Int, CaseIterable, Identifiable {
    case metal = 2

    var id: Int { rawValue }

    static let storageKey = "espressu.render.graphicsAPI"
    static let defaultValue: RendererAPI = .metal
}

enum ScaleFilter: Int, CaseIterable, Identifiable {
    case linear = 0
    case bicubic = 1
    case bicubicHermite = 2
    case nearestNeighbor = 3

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .linear:         return "Linear"
        case .bicubic:        return "Bicubic"
        case .bicubicHermite: return "Bicubic Hermite"
        case .nearestNeighbor: return "Nearest Neighbor"
        }
    }
}

enum UpscaleFilterSetting {
    static let storageKey = "espressu.render.upscaleFilter"
    static let defaultValue = ScaleFilter.bicubic
}

enum DownscaleFilterSetting {
    static let storageKey = "espressu.render.downscaleFilter"
    static let defaultValue = ScaleFilter.linear
}

enum DisplayGammaSetting {
    static let storageKey = "espressu.render.displayGamma"
    static let defaultValue: Double = 2.2
    static let minValue: Double = 1.0
    static let maxValue: Double = 3.0
}

enum OverrideGammaSetting {
    static let storageKey = "espressu.render.overrideGammaValue"
    static let defaultValue: Double = 2.2
    static let minValue: Double = 1.0
    static let maxValue: Double = 3.0
}
