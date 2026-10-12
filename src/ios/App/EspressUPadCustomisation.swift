// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import CoreGraphics
import Foundation

struct EspressURGBA: Codable, Equatable {
    var r: Double, g: Double, b: Double, a: Double

    init(r: Double, g: Double, b: Double, a: Double = 1) { self.r = r; self.g = g; self.b = b; self.a = a }

    init(_ hex: String, _ alpha: Double = 1) {
        var s = hex.hasPrefix("#") ? String(hex.dropFirst()) : hex
        if s.count == 3 { s = s.map { "\($0)\($0)" }.joined() }
        let v = UInt32(s, radix: 16) ?? 0
        self.init(r: Double((v >> 16) & 0xFF) / 255,
                  g: Double((v >> 8) & 0xFF) / 255,
                  b: Double(v & 0xFF) / 255, a: alpha)
    }

    var hex: String { String(format: "#%02X%02X%02X", Int(r * 255), Int(g * 255), Int(b * 255)) }

    enum CodingKeys: String, CodingKey { case hex, alpha }
    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        self.init(try c.decode(String.self, forKey: .hex),
                  try c.decodeIfPresent(Double.self, forKey: .alpha) ?? 1)
    }
    func encode(to e: Encoder) throws {
        var c = e.container(keyedBy: CodingKeys.self)
        try c.encode(hex, forKey: .hex)
        if a != 1 { try c.encode(a, forKey: .alpha) }
    }
}

enum PadGroup: String, CaseIterable, Codable, Identifiable {
    case shoulderL, shoulderR, stickL, stickR, dpad, face, start, select, home

    var id: String { rawValue }

    var controlIDs: [String] {
        switch self {
        case .shoulderL: return ["L", "ZL"]
        case .shoulderR: return ["R", "ZR"]
        case .stickL:    return ["stickL", "knobL"]
        case .stickR:    return ["stickR", "knobR"]
        case .dpad:      return ["dpad", "up", "down", "left", "right", "L3"]
        case .face:      return ["X", "Y", "A", "B", "R3"]
        case .start:     return ["plus"]
        case .select:    return ["minus"]
        case .home:      return ["HOME", "TV", "POWER"]
        }
    }

    var anchorControl: String {
        switch self {
        case .shoulderL: return "L"
        case .shoulderR: return "R"
        case .stickL:    return "stickL"
        case .stickR:    return "stickR"
        case .dpad:      return "dpad"
        case .face:      return "R3"
        case .start:     return "plus"
        case .select:    return "minus"
        case .home:      return "HOME"
        }
    }

    var title: String {
        switch self {
        case .shoulderL: return "L and ZL"
        case .shoulderR: return "R and ZR"
        case .stickL:    return "Left stick"
        case .stickR:    return "Right stick"
        case .dpad:      return "D-pad"
        case .face:      return "A B X Y"
        case .start:     return "Start"
        case .select:    return "Select"
        case .home:      return "HOME"
        }
    }

    var caption: String? {
        switch self {
        case .start:  return "START"
        case .select: return "SELECT"
        case .home:   return "HOME"
        default:      return nil
        }
    }

    enum Anchor: String, Codable { case bottomLeading, bottomTrailing, bottomCentre }

    var anchor: Anchor {
        switch self {
        case .shoulderL, .stickL, .dpad:            return .bottomLeading
        case .shoulderR, .stickR, .face, .start, .select: return .bottomTrailing
        case .home:                                 return .bottomCentre
        }
    }

    func anchorPoint(in safeArea: CGRect) -> CGPoint {
        switch anchor {
        case .bottomLeading:  return CGPoint(x: safeArea.minX, y: safeArea.maxY)
        case .bottomTrailing: return CGPoint(x: safeArea.maxX, y: safeArea.maxY)
        case .bottomCentre:   return CGPoint(x: safeArea.midX, y: safeArea.maxY)
        }
    }

    var inboardSign: CGFloat { anchor == .bottomTrailing ? -1 : 1 }

    static func group(containing controlID: String) -> PadGroup? {
        allCases.first { $0.controlIDs.contains(controlID) }
    }
}

struct GroupPlacement: Codable, Equatable {
    var dx: Double
    var dy: Double
    var scale: Double = 1.0

    static let minScale = 0.4
    static let maxScale = 2.5
    var clamped: GroupPlacement { GroupPlacement(dx: dx, dy: dy, scale: min(max(scale, Self.minScale), Self.maxScale)) }
}

struct EspressULayoutFile: Codable, Equatable {
    static let fileExtension = "espresslyt"
    static let currentVersion = 1

    var version: Int = currentVersion
    var name: String
    var authoredWidth: Double
    var authoredHeight: Double
    var authoredPointsPerInch: Double
    var unit: Double
    var lifeUnit: Double
    var groups: [String: GroupPlacement]

    var lifeSizeFraction: Double { lifeUnit > 0 ? unit / lifeUnit : 1 }

    func encoded() throws -> Data {
        let e = JSONEncoder(); e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }
    static func decode(_ data: Data) throws -> EspressULayoutFile {
        let f = try JSONDecoder().decode(EspressULayoutFile.self, from: data)
        guard f.version <= currentVersion else { throw CocoaError(.fileReadCorruptFile) }
        return f
    }

    static func capture(name: String, from layout: PadLayout, safeArea: CGRect,
                        pointsPerInch: CGFloat, scales: [PadGroup: Double] = [:]) -> EspressULayoutFile {
        var groups: [String: GroupPlacement] = [:]
        for g in PadGroup.allCases {
            guard let c = layout.controls[g.anchorControl]?.centre else { continue }
            let a = g.anchorPoint(in: safeArea)
            groups[g.rawValue] = GroupPlacement(
                dx: Double((c.x - a.x) * g.inboardSign / layout.unit),
                dy: Double((a.y - c.y) / layout.unit),
                scale: scales[g] ?? 1.0)
        }
        return EspressULayoutFile(name: name,
                                authoredWidth: Double(safeArea.width),
                                authoredHeight: Double(safeArea.height),
                                authoredPointsPerInch: Double(pointsPerInch),
                                unit: Double(layout.unit),
                                lifeUnit: Double(layout.lifeSizeUnit),
                                groups: groups)
    }
}

struct EspressUColourFile: Codable, Equatable {
    static let fileExtension = "espressclr"
    static let currentVersion = 1

    var version: Int = currentVersion
    var name: String
    var fills: [String: EspressURGBA]
    var glyphs: [String: EspressURGBA]
    var outline: EspressURGBA
    var pressedAlphaBoost: Double = 0.12
    var shell: EspressURGBA?

    func fill(_ controlID: String) -> EspressURGBA {
        fills[controlID] ?? fills[PadGroup.group(containing: controlID)?.rawValue ?? ""]
            ?? fills["default"] ?? EspressURGBA("#CDCDCD")
    }
    func glyph(_ controlID: String) -> EspressURGBA {
        glyphs[controlID] ?? glyphs[PadGroup.group(containing: controlID)?.rawValue ?? ""]
            ?? glyphs["default"] ?? EspressURGBA("#333333")
    }
    func alpha(_ controlID: String, pressed: Bool) -> Double {
        min(1, fill(controlID).a + (pressed ? pressedAlphaBoost : 0))
    }

    func encoded() throws -> Data {
        let e = JSONEncoder(); e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }
    static func decode(_ data: Data) throws -> EspressUColourFile {
        let f = try JSONDecoder().decode(EspressUColourFile.self, from: data)
        guard f.version <= currentVersion else { throw CocoaError(.fileReadCorruptFile) }
        return f
    }
}

enum PadPresetFitter {

    struct Intervention: Equatable {
        enum Kind: String { case scaledToFit, raisedToTouchFloor, movedInsideSafeArea, separatedOverlap, fellBackToNative }
        var kind: Kind
        var group: PadGroup?
        var detail: String
    }

    struct Fitted {
        var unit: CGFloat
        var centres: [PadGroup: CGPoint]
        var controls: [String: PadLayout.Placement]
        var interventions: [Intervention]
        var isFaithful: Bool { interventions.isEmpty }
    }

    static func fit(_ file: EspressULayoutFile,
                    container: CGSize,
                    safeArea: CGRect,
                    pointsPerInch: CGFloat) -> Fitted {

        let native = PadLayout.resolve(container: container, safeArea: safeArea,
                                       pointsPerInch: pointsPerInch)
        var log: [Intervention] = []

        var unit = native.lifeSizeUnit * CGFloat(file.lifeSizeFraction)

        func centres(at u: CGFloat) -> [PadGroup: CGPoint] {
            var out: [PadGroup: CGPoint] = [:]
            for g in PadGroup.allCases {
                guard let p = file.groups[g.rawValue] else { continue }
                let a = g.anchorPoint(in: safeArea)
                out[g] = CGPoint(x: a.x + CGFloat(p.dx) * u * g.inboardSign,
                                 y: a.y - CGFloat(p.dy) * u)
            }
            return out
        }

        var heightLimit = CGFloat.greatestFiniteMagnitude
        for g in PadGroup.allCases {
            guard let p = file.groups[g.rawValue] else { continue }
            let halfHeightInD = groupExtentInD(g, native: native, scale: p.scale).height / 2
            let neededInD = CGFloat(p.dy) + halfHeightInD
            guard neededInD > 0 else { continue }
            heightLimit = min(heightLimit, safeArea.height / neededInD)
        }
        if heightLimit < unit {
            log.append(.init(kind: .scaledToFit, group: nil,
                             detail: String(format: "%.1f -> %.1f pt: the tallest reach needs more height than this screen has",
                                            unit, heightLimit)))
            unit = heightLimit
        }

        if unit < PadLayout.touchFloor {
            log.append(.init(kind: .raisedToTouchFloor, group: nil,
                             detail: String(format: "%.1f -> %.0f pt, the 44 pt minimum", unit, PadLayout.touchFloor)))
            unit = PadLayout.touchFloor
        }

        @discardableResult
        func clampToSafeArea(_ p: inout [PadGroup: CGPoint], at u: CGFloat) -> Set<PadGroup> {
            var moved: Set<PadGroup> = []
            for g in PadGroup.allCases {
                guard var c = p[g], let b = groupBounds(g, native: native, unit: u,
                                                        scale: file.groups[g.rawValue]?.scale ?? 1) else { continue }
                let before = c
                let xRange = (safeArea.minX - b.minX, safeArea.maxX - b.maxX)
                let yRange = (safeArea.minY - b.minY, safeArea.maxY - b.maxY)
                c.x = xRange.0 <= xRange.1 ? min(max(c.x, xRange.0), xRange.1) : (xRange.0 + xRange.1) / 2
                c.y = yRange.0 <= yRange.1 ? min(max(c.y, yRange.0), yRange.1) : (yRange.0 + yRange.1) / 2
                if abs(c.x - before.x) > 0.5 || abs(c.y - before.y) > 0.5 { p[g] = c; moved.insert(g) }
            }
            return moved
        }

        var placed = centres(at: unit)
        clampToSafeArea(&placed, at: unit)
        var shrinkSteps = 0, fellBack: Set<PadGroup> = []
        while let clash = firstOverlap(placed, file: file, native: native, unit: unit) {
            let next = unit * 0.97
            if next < PadLayout.touchFloor * 0.9 || (fellBack.contains(clash.0) && fellBack.contains(clash.1)) {
                for g in [clash.0, clash.1] where !fellBack.contains(g) {
                    if let n = native.controls[g.anchorControl]?.centre {
                        placed[g] = n
                        fellBack.insert(g)
                        log.append(.init(kind: .fellBackToNative, group: g,
                                         detail: "\(g.title) could not be made to fit; using this device's own position"))
                    }
                }
                clampToSafeArea(&placed, at: unit)
                if fellBack.count >= PadGroup.allCases.count { break }
                continue
            }
            shrinkSteps += 1
            unit = next
            placed = centres(at: unit)
            for g in fellBack {
                if let n = native.controls[g.anchorControl]?.centre { placed[g] = n }
            }
            clampToSafeArea(&placed, at: unit)
        }
        if shrinkSteps > 0 {
            log.append(.init(kind: .separatedOverlap, group: nil,
                             detail: String(format: "shrunk %d%% to stop groups overlapping", Int((1 - pow(0.97, Double(shrinkSteps))) * 100))))
        }
        let raw = centres(at: unit)
        for g in PadGroup.allCases {
            guard let c = placed[g], let r = raw[g], abs(c.x - r.x) > 0.5 || abs(c.y - r.y) > 0.5 else { continue }
            log.append(.init(kind: .movedInsideSafeArea, group: g,
                             detail: String(format: "%@ moved %.0f, %.0f pt back on screen",
                                            g.title, c.x - r.x, c.y - r.y)))
        }

        let controls = buildControls(placed, unit: unit, file: file, native: native)
        return Fitted(unit: unit, centres: placed, controls: controls, interventions: log)
    }

    static func buildControls(_ placed: [PadGroup: CGPoint], unit: CGFloat,
                                      file: EspressULayoutFile, native: PadLayout) -> [String: PadLayout.Placement] {
        var controls: [String: PadLayout.Placement] = [:]
        for g in PadGroup.allCases {
            guard let centre = placed[g], let anchor = native.controls[g.anchorControl]?.centre else { continue }
            let s = CGFloat(file.groups[g.rawValue]?.scale ?? 1) * unit / native.unit
            for id in g.controlIDs {
                guard let p = native.controls[id] else { continue }
                let rel = CGPoint(x: (p.centre.x - anchor.x) * s, y: (p.centre.y - anchor.y) * s)
                let at = CGPoint(x: centre.x + rel.x, y: centre.y + rel.y)
                switch p {
                case .circle(_, let d):
                    controls[id] = .circle(centre: at, diameter: d * s)
                case .pill(_, let sz, let r):
                    controls[id] = .pill(centre: at, size: CGSize(width: sz.width * s, height: sz.height * s), corner: r * s)
                case .cross(_, let sz, let arm):
                    controls[id] = .cross(centre: at, size: CGSize(width: sz.width * s, height: sz.height * s), arm: arm * s)
                }
            }
        }
        return controls
    }

    private static func groupExtentInD(_ g: PadGroup, native: PadLayout, scale: Double) -> CGSize {
        let full = groupExtent(g, native: native, unit: native.unit, scale: scale)
        return CGSize(width: full.width / native.unit, height: full.height / native.unit)
    }

    private static func groupBounds(_ g: PadGroup, native: PadLayout, unit: CGFloat, scale: Double)
        -> (minX: CGFloat, maxX: CGFloat, minY: CGFloat, maxY: CGFloat)? {
        guard let anchor = native.controls[g.anchorControl]?.centre else { return nil }
        let s = CGFloat(scale) * unit / native.unit
        var minX = CGFloat.greatestFiniteMagnitude, maxX = -CGFloat.greatestFiniteMagnitude
        var minY = CGFloat.greatestFiniteMagnitude, maxY = -CGFloat.greatestFiniteMagnitude
        for id in g.controlIDs {
            guard let p = native.controls[id] else { continue }
            let sz = p.boundingSize
            let cx = (p.centre.x - anchor.x) * s, cy = (p.centre.y - anchor.y) * s
            minX = min(minX, cx - sz.width * s / 2);  maxX = max(maxX, cx + sz.width * s / 2)
            minY = min(minY, cy - sz.height * s / 2); maxY = max(maxY, cy + sz.height * s / 2)
        }
        guard minX <= maxX else { return nil }
        return (minX, maxX, minY, maxY)
    }

    private static func groupExtent(_ g: PadGroup, native: PadLayout, unit: CGFloat, scale: Double) -> CGSize {
        guard let b = groupBounds(g, native: native, unit: unit, scale: scale) else { return .zero }
        return CGSize(width: b.maxX - b.minX, height: b.maxY - b.minY)
    }

    private enum Primitive { case circle(CGPoint, CGFloat); case rect(CGPoint, CGFloat, CGFloat) }

    private static func primitives(for p: PadLayout.Placement) -> [Primitive] {
        switch p {
        case .circle(let c, let d): return [.circle(c, d / 2)]
        case .pill(let c, let s, _): return [.rect(c, s.width / 2, s.height / 2)]
        case .cross(let c, let s, let arm):
            return [.rect(c, s.width / 2, arm / 2), .rect(c, arm / 2, s.height / 2)]
        }
    }

    private static func separation(_ a: Primitive, _ b: Primitive) -> CGFloat {
        switch (a, b) {
        case (.circle(let ca, let ra), .circle(let cb, let rb)):
            return hypot(ca.x - cb.x, ca.y - cb.y) - (ra + rb)
        case (.rect(let ca, let hwA, let hhA), .rect(let cb, let hwB, let hhB)):
            return max(abs(ca.x - cb.x) - (hwA + hwB), abs(ca.y - cb.y) - (hhA + hhB))
        case (.circle(let cc, let r), .rect(let rc, let hw, let hh)),
             (.rect(let rc, let hw, let hh), .circle(let cc, let r)):
            let dx = max(0, abs(cc.x - rc.x) - hw), dy = max(0, abs(cc.y - rc.y) - hh)
            return hypot(dx, dy) - r
        }
    }

    private static func firstOverlap(_ placed: [PadGroup: CGPoint], file: EspressULayoutFile,
                                     native: PadLayout, unit: CGFloat) -> (PadGroup, PadGroup)? {
        let controls = buildControls(placed, unit: unit, file: file, native: native)
        let gs = PadGroup.allCases.filter { placed[$0] != nil }
        for i in 0..<gs.count {
            for j in (i + 1)..<gs.count {
                let a = gs[i], b = gs[j]
                for idA in a.controlIDs {
                    guard let pa = controls[idA] else { continue }
                    for idB in b.controlIDs {
                        guard let pb = controls[idB] else { continue }
                        for sa in primitives(for: pa) {
                            for sb in primitives(for: pb) where separation(sa, sb) < -0.5 {
                                return (a, b)
                            }
                        }
                    }
                }
            }
        }
        return nil
    }
}
