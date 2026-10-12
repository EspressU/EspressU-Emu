// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Combine

struct ControlOverride: Codable, Equatable {
    var dx: Double = 0
    var dy: Double = 0
    var scale: Double = 1.0

    static let identity = ControlOverride()
    var isIdentity: Bool { self == ControlOverride.identity }
}

final class ControllerCustomLayout: ObservableObject {
    static let shared = ControllerCustomLayout()

    static let storageKey = "espressu.controls.elements"
    static let minScale: Double = 0.5
    static let maxScale: Double = 2.0

    @Published private(set) var overrides: [String: ControlOverride]

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let data = defaults.data(forKey: Self.storageKey),
           let decoded = try? JSONDecoder().decode([String: ControlOverride].self, from: data) {
            overrides = decoded
        } else {
            overrides = [:]
        }
    }

    static func groupID(for controlID: String) -> String {
        switch controlID {
        case "L", "ZL": return "shoulderL"
        case "R", "ZR": return "shoulderR"
        default: return controlID
        }
    }

    func override(for controlID: String) -> ControlOverride {
        overrides[Self.groupID(for: controlID)] ?? .identity
    }

    func move(_ controlID: String, to translation: CGSize, from origin: ControlOverride) {
        var next = origin
        next.dx = origin.dx + Double(translation.width)
        next.dy = origin.dy + Double(translation.height)
        write(next, for: controlID)
    }

    func setScale(_ scale: Double, for controlID: String) {
        var next = override(for: controlID)
        next.scale = min(max(scale, Self.minScale), Self.maxScale)
        write(next, for: controlID)
    }

    func reset(_ controlID: String) {
        overrides.removeValue(forKey: Self.groupID(for: controlID))
        persist()
    }

    func resetAll() {
        overrides.removeAll()
        persist()
    }

    func reloadFromDefaults() {
        if let data = defaults.data(forKey: Self.storageKey),
           let decoded = try? JSONDecoder().decode([String: ControlOverride].self, from: data) {
            overrides = decoded
        } else {
            overrides = [:]
        }
    }

    var hasCustomisations: Bool { !overrides.isEmpty }

    func flushPending() {
        guard pendingPersist != nil else { return }
        pendingPersist?.cancel()
        pendingPersist = nil
        persist()
    }

    func commit() {
        pendingPersist?.cancel()
        persist()
    }

    private func write(_ value: ControlOverride, for controlID: String) {
        let key = Self.groupID(for: controlID)
        if value.isIdentity {
            overrides.removeValue(forKey: key)
        } else {
            overrides[key] = value
        }
        schedulePersist()
    }

    private var pendingPersist: DispatchWorkItem?

    private func schedulePersist() {
        pendingPersist?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.persist() }
        pendingPersist = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    private func persist() {
        guard let data = try? JSONEncoder().encode(overrides) else { return }
        defaults.set(data, forKey: Self.storageKey)
    }
}
