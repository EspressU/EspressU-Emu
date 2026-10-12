// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

enum PreviewLayoutPreset: String, CaseIterable, Identifiable {
    case native, iPadPro2020, compact

    var id: String { rawValue }

    var title: String {
        switch self {
        case .native:      return "Native (this device)"
        case .iPadPro2020: return "iPad Pro 12.9\" (2020)"
        case .compact:     return "Compact"
        }
    }

    var summary: String {
        switch self {
        case .native:
            return "This device's own layout."
        case .iPadPro2020:
            return "The layout from a 12.9-inch iPad Pro, fitted to your screen."
        case .compact:
            return "70% of life-size: smaller buttons, more of the picture."
        }
    }

    private var reference: (container: CGSize, safeArea: CGRect, pointsPerInch: CGFloat, userScale: CGFloat)? {
        switch self {
        case .native:
            return nil
        case .iPadPro2020:
            return (CGSize(width: 1366, height: 1024), CGRect(x: 0, y: 0, width: 1366, height: 1004), 132, 1.0)
        case .compact:
            return (CGSize(width: 1366, height: 1024), CGRect(x: 0, y: 0, width: 1366, height: 1004), 132, 0.7)
        }
    }

    func layoutFile(displayMode: PadLayout.DisplayMode) -> EspressULayoutFile? {
        guard let ref = reference else { return nil }
        let layout = PadLayout.resolve(container: ref.container, safeArea: ref.safeArea,
                                       pointsPerInch: ref.pointsPerInch, userScale: ref.userScale,
                                       displayMode: displayMode)
        return EspressULayoutFile.capture(name: title, from: layout, safeArea: ref.safeArea,
                                        pointsPerInch: ref.pointsPerInch)
    }
}

enum PreviewColourPreset: String, CaseIterable, Identifiable {
    case wiiUWhite, wiiUBlack, superFamicom

    var id: String { rawValue }

    var file: EspressUColourFile {
        switch self {
        case .wiiUWhite:    return EspressUColourPresets.wiiUWhite
        case .wiiUBlack:    return EspressUColourPresets.wiiUBlack
        case .superFamicom: return EspressUColourPresets.superFamicom
        }
    }
}

final class PreviewPadStore: ObservableObject {
    static let shared = PreviewPadStore()

    static let enabledKey = "espressu.preview.enabled"

    static let defaultEnabled = false
    static let layoutPresetKey = "espressu.preview.layoutPreset"
    static let colourPresetKey = "espressu.preview.colourPreset"
    static let customColoursKey = "espressu.preview.customColours"
    static let displayModeKey = "espressu.preview.displayMode"
    static let adjustmentsKey = "espressu.preview.groupAdjustments"

    @Published var layoutPreset: PreviewLayoutPreset {
        didSet { defaults.set(layoutPreset.rawValue, forKey: Self.layoutPresetKey) }
    }
    @Published var colourPreset: PreviewColourPreset {
        didSet {
            defaults.set(colourPreset.rawValue, forKey: Self.colourPresetKey)
            customColours = nil
        }
    }
    @Published private(set) var customColours: EspressUColourFile? {
        didSet { persistCustomColours() }
    }
    var colourFile: EspressUColourFile { customColours ?? colourPreset.file }
    @Published var displayMode: PadLayout.DisplayMode {
        didSet { defaults.set(displayMode.rawValue, forKey: Self.displayModeKey) }
    }
    @Published var adjustments: [String: GroupPlacement] {
        didSet { persistAdjustments() }
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        layoutPreset = PreviewLayoutPreset(rawValue: defaults.string(forKey: Self.layoutPresetKey) ?? "") ?? .native
        colourPreset = PreviewColourPreset(rawValue: defaults.string(forKey: Self.colourPresetKey) ?? "") ?? .wiiUWhite
        displayMode = PadLayout.DisplayMode(rawValue: defaults.string(forKey: Self.displayModeKey) ?? "") ?? .fit
        customColours = Self.loadCustomColours(defaults)
        if let data = defaults.data(forKey: Self.adjustmentsKey),
           let decoded = try? JSONDecoder().decode([String: GroupPlacement].self, from: data) {
            adjustments = decoded
        } else {
            adjustments = [:]
        }
    }

    func reloadFromDefaults() {
        let custom = Self.loadCustomColours(defaults)
        layoutPreset = PreviewLayoutPreset(rawValue: defaults.string(forKey: Self.layoutPresetKey) ?? "") ?? .native
        colourPreset = PreviewColourPreset(rawValue: defaults.string(forKey: Self.colourPresetKey) ?? "") ?? .wiiUWhite
        displayMode = PadLayout.DisplayMode(rawValue: defaults.string(forKey: Self.displayModeKey) ?? "") ?? .fit
        customColours = custom
        if let data = defaults.data(forKey: Self.adjustmentsKey),
           let decoded = try? JSONDecoder().decode([String: GroupPlacement].self, from: data) {
            adjustments = decoded
        } else {
            adjustments = [:]
        }
    }

    func adjustment(for group: PadGroup) -> GroupPlacement {
        adjustments[group.rawValue] ?? GroupPlacement(dx: 0, dy: 0, scale: 1)
    }

    func setAdjustment(_ placement: GroupPlacement, for group: PadGroup) {
        adjustments[group.rawValue] = placement.clamped
    }

    func resetAdjustments() {
        adjustments = [:]
    }

    func applyImportedLayout(_ file: EspressULayoutFile) {
        layoutPreset = .native
        adjustments = file.groups
    }

    func applyImportedColours(_ file: EspressUColourFile) {
        customColours = file
    }

    func effectiveLayoutFile(container: CGSize, safeArea: CGRect, pointsPerInch: CGFloat) -> EspressULayoutFile {
        let base = layoutPreset.layoutFile(displayMode: displayMode)
            ?? {
                let native = PadLayout.resolve(container: container, safeArea: safeArea,
                                               pointsPerInch: pointsPerInch, displayMode: displayMode)
                return EspressULayoutFile.capture(name: "native", from: native, safeArea: safeArea,
                                                pointsPerInch: pointsPerInch)
            }()
        var merged = base
        merged.name = "\(layoutPreset.title) (edited)"
        for g in PadGroup.allCases {
            let a = adjustment(for: g)
            guard !a.dx.isZero || !a.dy.isZero || a.scale != 1 else { continue }
            var v = merged.groups[g.rawValue] ?? GroupPlacement(dx: 0, dy: 0, scale: 1)
            v.dx += a.dx; v.dy += a.dy; v.scale *= a.scale
            merged.groups[g.rawValue] = v.clamped
        }
        return merged
    }

    private static func loadCustomColours(_ defaults: UserDefaults) -> EspressUColourFile? {
        guard let data = defaults.data(forKey: customColoursKey) else { return nil }
        return try? EspressUColourFile.decode(data)
    }

    private func persistCustomColours() {
        if let customColours, let data = try? customColours.encoded() {
            defaults.set(data, forKey: Self.customColoursKey)
        } else {
            defaults.removeObject(forKey: Self.customColoursKey)
        }
    }

    private func persistAdjustments() {
        guard let data = try? JSONEncoder().encode(adjustments) else { return }
        defaults.set(data, forKey: Self.adjustmentsKey)
    }

    func effectiveDisplayMode(container: CGSize) -> PadLayout.DisplayMode {
        if UIDevice.current.userInterfaceIdiom == .phone, container.height > container.width { return .native }
        return displayMode
    }

    func resolve(container: CGSize, safeArea: CGRect, pointsPerInch: CGFloat) -> PreviewResolved {
        let displayMode = effectiveDisplayMode(container: container)
        if let file = layoutPreset.layoutFile(displayMode: displayMode) {
            var adjustedFile = file
            for g in PadGroup.allCases {
                let a = adjustment(for: g)
                guard !a.dx.isZero || !a.dy.isZero || a.scale != 1 else { continue }
                var base = adjustedFile.groups[g.rawValue] ?? GroupPlacement(dx: 0, dy: 0, scale: 1)
                base.dx += a.dx; base.dy += a.dy; base.scale *= a.scale
                adjustedFile.groups[g.rawValue] = base.clamped
            }
            let fitted = PadPresetFitter.fit(adjustedFile, container: container, safeArea: safeArea,
                                             pointsPerInch: pointsPerInch)
            let nativeForVideo = PadLayout.resolve(container: container, safeArea: safeArea,
                                                   pointsPerInch: pointsPerInch, displayMode: displayMode)
            return PreviewResolved(video: nativeForVideo.video, controls: fitted.controls, unit: fitted.unit,
                                   notes: fitted.interventions.map(\.detail))
        }

        let base = PadLayout.resolve(container: container, safeArea: safeArea,
                                     pointsPerInch: pointsPerInch, displayMode: displayMode)
        guard !adjustments.isEmpty else {
            return PreviewResolved(video: base.video, controls: base.controls, unit: base.unit, notes: base.notes)
        }
        var file = EspressULayoutFile.capture(name: "native", from: base, safeArea: safeArea,
                                            pointsPerInch: pointsPerInch)
        for g in PadGroup.allCases {
            let a = adjustment(for: g)
            guard !a.dx.isZero || !a.dy.isZero || a.scale != 1 else { continue }
            var v = file.groups[g.rawValue] ?? GroupPlacement(dx: 0, dy: 0, scale: 1)
            v.dx += a.dx; v.dy += a.dy; v.scale *= a.scale
            file.groups[g.rawValue] = v.clamped
        }
        let fitted = PadPresetFitter.fit(file, container: container, safeArea: safeArea, pointsPerInch: pointsPerInch)
        return PreviewResolved(video: base.video, controls: fitted.controls, unit: fitted.unit,
                               notes: base.notes + fitted.interventions.map(\.detail))
    }
}

struct PreviewResolved {
    var video: CGRect
    var controls: [String: PadLayout.Placement]
    var unit: CGFloat
    var notes: [String]
}
