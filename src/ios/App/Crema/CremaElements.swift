// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.


import CoreGraphics
import Foundation

/// One control the player can move and resize on its own.
enum CremaElement: String, CaseIterable, Codable, Hashable {
    case dpad, leftStick, rightStick
    case faceButtons
    case zl, l, r, zr
    case minus, home, plus

    var title: String {
        switch self {
        case .dpad: return "D-pad"
        case .leftStick: return "Left stick"
        case .rightStick: return "Right stick"
        case .faceButtons: return "A B X Y buttons"
        case .zl: return "ZL button"
        case .l: return "L button"
        case .r: return "R button"
        case .zr: return "ZR button"
        case .minus: return "Minus button"
        case .home: return "HOME button"
        case .plus: return "Plus button"
        }
    }
}

/// How one control differs from its default spot: a shift in units (`CremaLayout.unit`) and a size factor.
struct CremaElementTweak: Equatable {
    static let minScale = 0.6
    static let maxScale = 1.8

    var x = 0.0
    var y = 0.0
    var scale = 1.0

    var isDefault: Bool { abs(x) < 0.001 && abs(y) < 0.001 && abs(scale - 1) < 0.001 }
}

/// Every control the player has moved or resized, stored as one JSON string such as
/// `{"dpad":{"x":0.5,"y":-1,"s":1.2}}`. Controls that are not listed stay where EspressU puts them.
struct CremaElementOverrides: Equatable {
    private var tweaks: [CremaElement: CremaElementTweak] = [:]

    init() {}

    /// Reads the stored string. An empty string means "all default"; anything unreadable gives nil.
    init?(json: String) {
        if json.isEmpty { return }
        guard let data = json.data(using: .utf8),
              let raw = try? JSONSerialization.jsonObject(with: data) as? [String: [String: Any]] else { return nil }
        func read(_ fields: [String: Any]) -> CremaElementTweak {
            func number(_ key: String) -> Double? {
                guard let value = fields[key] as? NSNumber else { return nil }
                let double = value.doubleValue
                return double.isFinite ? double : nil
            }
            return CremaElementTweak(
                x: min(max(number("x") ?? 0, -40), 40),
                y: min(max(number("y") ?? 0, -40), 40),
                scale: min(max(number("s") ?? 1, CremaElementTweak.minScale), CremaElementTweak.maxScale))
        }
        for (name, fields) in raw {
            guard let element = CremaElement(rawValue: name) else { continue }
            let tweak = read(fields)
            if !tweak.isDefault { tweaks[element] = tweak }
        }
        // Older versions stored A, B, X and Y one by one; the whole cluster now takes their mean.
        if tweaks[.faceButtons] == nil {
            let legacy = ["a", "b", "x", "y"].compactMap { raw[$0] }.map(read)
            if !legacy.isEmpty {
                let count = Double(legacy.count)
                let tweak = CremaElementTweak(x: legacy.map(\.x).reduce(0, +) / count,
                                              y: legacy.map(\.y).reduce(0, +) / count,
                                              scale: legacy.map(\.scale).reduce(0, +) / count)
                if !tweak.isDefault { tweaks[.faceButtons] = tweak }
            }
        }
    }

    subscript(element: CremaElement) -> CremaElementTweak {
        get { tweaks[element] ?? CremaElementTweak() }
        set {
            if newValue.isDefault { tweaks[element] = nil } else { tweaks[element] = newValue }
        }
    }

    var isEmpty: Bool { tweaks.isEmpty }

    var json: String {
        guard !tweaks.isEmpty else { return "" }
        var raw: [String: [String: Double]] = [:]
        for (element, tweak) in tweaks {
            raw[element.rawValue] = ["x": (tweak.x * 1000).rounded() / 1000,
                                     "y": (tweak.y * 1000).rounded() / 1000,
                                     "s": (tweak.scale * 1000).rounded() / 1000]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: raw, options: [.sortedKeys]) else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }
}
