// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import CoreGraphics
import Foundation

enum GamePadGeometry {

    static let unitMM: CGFloat = 10.625

    static let faceDiameter: CGFloat = 1.000
    static let faceHalfX: CGFloat    = 0.960
    static let faceHalfY: CGFloat    = 0.950

    static let dpadWidth: CGFloat  = 2.280
    static let dpadHeight: CGFloat = 2.240
    static let dpadArm: CGFloat    = 0.760

    static let stickBase: CGFloat = 1.960
    static let stickKnob: CGFloat = 1.120
    static let stickArm: CGFloat = 2.79020
    static let stickArmAngle: CGFloat = 69.8737 * .pi / 180

    static let systemDiameter: CGFloat = 0.680
    static let homeDiameter: CGFloat   = 1.040
    static let menuDiameter: CGFloat   = 0.640

    static let bodyWidth: CGFloat  = 24.000
    static let bodyHeight: CGFloat = 12.560
    static let screenWidth: CGFloat  = 12.92235
    static let screenHeight: CGFloat = 7.26588
    static let screenCentreFromTop: CGFloat = 6.280

    static let clusterFromBodyTop: CGFloat = 5.660
    static let dpadFromBodyLeft: CGFloat   = 3.160
    static let abxyFromBodyRight: CGFloat  = 3.040
    static let homeFromBodyTop: CGFloat    = 11.320
    static let tvFromHome: CGFloat         = 5.08047
    static let powerFromHome: CGFloat      = 6.28047

    static let plusFromABXY  = CGPoint(x: -1.080, y: 2.420)
    static let minusFromABXY = CGPoint(x: -1.080, y: 3.660)

    static let systemElbow = CGPoint(x: 1.414, y: -1.414)

    static let shoulderSize = CGSize(width: 1.151, height: 0.874)
    static let shoulderCorner: CGFloat = 0.235
    static let shoulderSpread: CGFloat = 0.675
    static let shoulderGap: CGFloat    = 0.200
    static var shoulderDY: CGFloat { -(stickBase / 2 + shoulderGap + shoulderSize.height / 2) }

    static var fixedTopExtent: CGFloat { -shoulderDY + shoulderSize.height / 2 }
    static func topExtent(armAngle: CGFloat) -> CGFloat {
        stickArm * sin(armAngle) + fixedTopExtent
    }
    static func outboardExtent(armAngle: CGFloat) -> CGFloat {
        max(dpadWidth / 2, stickArm * cos(armAngle) + max(stickBase / 2,
                                                          shoulderSpread + shoulderSize.width / 2))
    }
    static let bottomExtentHardware: CGFloat = 4.000
    static let bottomExtentElbow: CGFloat    = 1.450

    static func verticalNeed(armAngle: CGFloat, hardwareSystem: Bool) -> CGFloat {
        topExtent(armAngle: armAngle)
            + (hardwareSystem ? bottomExtentHardware : bottomExtentElbow)
            + 2 * PadLayout.edgeMargin
    }

    static func extents(armAngle: CGFloat, hardwareSystem: Bool)
        -> (outboard: CGFloat, inboardL: CGFloat, inboardR: CGFloat, total: CGFloat) {
        let inL = hardwareSystem ? dpadWidth / 2 : systemElbow.x + systemDiameter / 2
        let inR = max(faceHalfX + 0.5, inL)
        let out = outboardExtent(armAngle: armAngle)
        return (out, inL, inR, (out + inL) + (out + inR))
    }

    static func widthFit(armAngle: CGFloat, hardwareSystem: Bool, width: CGFloat) -> CGFloat {
        width / (extents(armAngle: armAngle, hardwareSystem: hardwareSystem).total
                 + 2 * PadLayout.edgeMargin + PadLayout.minimumClusterGap)
    }
}

struct PadLayout {

    enum Mode: String {
        case framed
        case overlay
        case shell
        case stacked
    }

    enum SystemPlacement: String { case hardware, elbow }

    enum DisplayMode: String, Codable, CaseIterable {
        case fit, native
        var title: String { self == .fit ? "Fit" : "Native" }
    }

    enum Placement {
        case circle(centre: CGPoint, diameter: CGFloat)
        case pill(centre: CGPoint, size: CGSize, corner: CGFloat)
        case cross(centre: CGPoint, size: CGSize, arm: CGFloat)

        var centre: CGPoint {
            switch self {
            case .circle(let c, _), .pill(let c, _, _), .cross(let c, _, _): return c
            }
        }
        var boundingSize: CGSize {
            switch self {
            case .circle(_, let d):   return CGSize(width: d, height: d)
            case .pill(_, let s, _):  return s
            case .cross(_, let s, _): return s
            }
        }
    }

    static let edgeMargin: CGFloat = 0.35
    static let clearance: CGFloat  = 0.35
    static let minimumClusterGap: CGFloat = 0.6
    static let realScreenMM: CGFloat = 137.3
    static let touchFloor: CGFloat = 44
    static let rakeFloor: CGFloat = 45 * .pi / 180

    var mode: Mode
    var unit: CGFloat
    var lifeSizeUnit: CGFloat
    var armAngle: CGFloat
    var systemPlacement: SystemPlacement
    var leftAnchor: CGPoint
    var rightAnchor: CGPoint
    var video: CGRect
    var controls: [String: Placement]
    var notes: [String]

    var lifeSizeFraction: CGFloat { unit / lifeSizeUnit }

    static func resolve(container: CGSize,
                        safeArea: CGRect,
                        pointsPerInch: CGFloat,
                        userScale: CGFloat = 1.0,
                        hasExternalDisplay: Bool = false,
                        displayMode: DisplayMode = .fit) -> PadLayout {

        let G = GamePadGeometry.self
        let sx = safeArea.minX, sy = safeArea.minY
        let sw = safeArea.width, sh = safeArea.height
        let portrait = sh > sw
        var notes: [String] = []

        let lifeUnit = G.unitMM * pointsPerInch / 25.4
        let wanted = lifeUnit * userScale

        var video = CGRect.zero
        var availableHeight = sh
        if portrait, displayMode == .native {
            var vw = sw, vh = sw * 9 / 16
            if vh > sh * 0.62 { vh = sh * 0.62; vw = vh * 16 / 9 }
            video = CGRect(x: sx + (sw - vw) / 2, y: sy, width: vw, height: vh)
            availableHeight = sy + sh - video.maxY
        }

        var armAngle = G.stickArmAngle
        var unit: CGFloat
        var hardwareSystem: Bool

        let hardwareUnit = min(wanted, G.widthFit(armAngle: armAngle, hardwareSystem: true, width: sw))
        if hardwareUnit <= availableHeight / G.verticalNeed(armAngle: armAngle, hardwareSystem: true) + 1e-9 {
            hardwareSystem = true
            unit = hardwareUnit
        } else {
            hardwareSystem = false
            unit = min(wanted,
                       availableHeight / G.verticalNeed(armAngle: armAngle, hardwareSystem: false),
                       G.widthFit(armAngle: armAngle, hardwareSystem: false, width: sw))
            if unit < touchFloor {
                let k = (availableHeight / touchFloor - G.bottomExtentElbow
                         - 2 * edgeMargin - G.fixedTopExtent) / G.stickArm
                armAngle = max(rakeFloor, asin(min(max(k, -1), 1)))
                unit = min(wanted,
                           availableHeight / G.verticalNeed(armAngle: armAngle, hardwareSystem: false),
                           G.widthFit(armAngle: armAngle, hardwareSystem: false, width: sw))
                notes.append(String(format: "raked to %.0f deg to fit the height",
                                    armAngle * 180 / .pi))
                if unit < touchFloor {
                    notes.append(String(format: "%.0f pt buttons, under the 44 pt floor - the container is too small", unit))
                }
            }
        }
        if wanted > unit + 0.01 {
            notes.append(String(format: "%.0f%% of life-size to fit", 100 * unit / lifeUnit))
        }

        let bottomExtent = hardwareSystem ? G.bottomExtentHardware : G.bottomExtentElbow
        let e = G.extents(armAngle: armAngle, hardwareSystem: hardwareSystem)
        var lx = sx + (edgeMargin + e.outboard) * unit
        var rx = sx + sw - (edgeMargin + e.outboard) * unit
        let gap0 = lx + (e.inboardL + clearance) * unit
        let gap1 = rx - (e.inboardR + clearance) * unit
        let gapWidth = max(0, gap1 - gap0)
        let gapCentreX = (gap0 + gap1) / 2

        var mode: Mode
        if hasExternalDisplay {
            mode = .shell
        } else if portrait {
            mode = displayMode == .fit ? .overlay : .stacked
            if mode == .stacked {
                notes.append("portrait, Native: picture along the top, clusters on the bottom corners")
            }
        } else {
            mode = displayMode == .fit ? .overlay : .framed
        }

        var bodyTop: CGFloat? = nil
        var cy: CGFloat
        if (mode == .framed || mode == .shell) && G.bodyHeight * unit <= sh {
            let top = sy + (sh - G.bodyHeight * unit) / 2
            bodyTop = top
            cy = top + G.clusterFromBodyTop * unit
        } else if mode == .framed || mode == .shell {
            let need = G.verticalNeed(armAngle: armAngle, hardwareSystem: hardwareSystem)
            cy = sy + (sh - need * unit) / 2 + (G.topExtent(armAngle: armAngle) + edgeMargin) * unit
        } else {
            cy = sy + sh - (bottomExtent + edgeMargin) * unit
        }

        switch mode {
        case .stacked:
            cy = sy + sh - (bottomExtent + edgeMargin) * unit
            lx = sx + (edgeMargin + e.outboard) * unit
            rx = sx + sw - (edgeMargin + e.outboard) * unit
        case .overlay:
            let vh = min(sh, sw * 9 / 16), vw = vh * 16 / 9
            video = CGRect(x: sx + (sw - vw) / 2, y: sy + (sh - vh) / 2, width: vw, height: vh)
        case .framed, .shell:
            let vw = min(gapWidth, (sh - 2 * clearance * unit) * 16 / 9), vh = vw * 9 / 16
            var vcy = bodyTop.map { $0 + G.screenCentreFromTop * unit } ?? (sy + sh / 2)
            vcy = min(max(vcy, sy + vh / 2 + clearance * unit), sy + sh - vh / 2 - clearance * unit)
            video = CGRect(x: gapCentreX - vw / 2, y: vcy - vh / 2, width: vw, height: vh)
        }

        var c: [String: Placement] = [:]
        let armX = G.stickArm * cos(armAngle) * unit
        let armY = G.stickArm * sin(armAngle) * unit

        for (suffix, anchorX, sign) in [("L", lx, CGFloat(-1)), ("R", rx, CGFloat(1))] {
            let s = CGPoint(x: anchorX + sign * armX, y: cy - armY)
            c["stick\(suffix)"] = .circle(centre: s, diameter: G.stickBase * unit)
            c["knob\(suffix)"]  = .circle(centre: s, diameter: G.stickKnob * unit)
            c[suffix] = .pill(centre: CGPoint(x: s.x + sign * G.shoulderSpread * unit,
                                              y: s.y + G.shoulderDY * unit),
                              size: CGSize(width: G.shoulderSize.width * unit,
                                           height: G.shoulderSize.height * unit),
                              corner: G.shoulderCorner * unit)
            c["Z\(suffix)"] = .pill(centre: CGPoint(x: s.x - sign * G.shoulderSpread * unit,
                                                    y: s.y + G.shoulderDY * unit),
                                    size: CGSize(width: G.shoulderSize.width * unit,
                                                 height: G.shoulderSize.height * unit),
                                    corner: G.shoulderCorner * unit)
        }
        c["dpad"] = .cross(centre: CGPoint(x: lx, y: cy),
                           size: CGSize(width: G.dpadWidth * unit, height: G.dpadHeight * unit),
                           arm: G.dpadArm * unit)
        c["L3"] = .circle(centre: CGPoint(x: lx, y: cy), diameter: 0.706 * unit)
        c["R3"] = .circle(centre: CGPoint(x: rx, y: cy), diameter: 0.706 * unit)
        for (id, dx, dy) in [("X", CGFloat(0), -G.faceHalfY), ("Y", -G.faceHalfX, 0),
                             ("A", G.faceHalfX, 0), ("B", 0, G.faceHalfY)] {
            c[id] = .circle(centre: CGPoint(x: rx + dx * unit, y: cy + dy * unit),
                            diameter: G.faceDiameter * unit)
        }
        if hardwareSystem {
            c["plus"]  = .circle(centre: CGPoint(x: rx + G.plusFromABXY.x * unit,
                                                 y: cy + G.plusFromABXY.y * unit),
                                 diameter: G.systemDiameter * unit)
            c["minus"] = .circle(centre: CGPoint(x: rx + G.minusFromABXY.x * unit,
                                                 y: cy + G.minusFromABXY.y * unit),
                                 diameter: G.systemDiameter * unit)
        } else {
            c["plus"]  = .circle(centre: CGPoint(x: rx - G.systemElbow.x * unit,
                                                 y: cy + G.systemElbow.y * unit),
                                 diameter: G.systemDiameter * unit)
            c["minus"] = .circle(centre: CGPoint(x: lx + G.systemElbow.x * unit,
                                                 y: cy + G.systemElbow.y * unit),
                                 diameter: G.systemDiameter * unit)
            notes.append("+/- moved to the elbow: no room for their real place under A/B/X/Y")
        }

        let hr = G.homeDiameter / 2 * unit
        func collides(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) -> Bool {
            for (id, p) in c where !id.hasPrefix("knob") {
                let s = p.boundingSize
                if abs(x - p.centre.x) < (r + s.width / 2) - 0.5,
                   abs(y - p.centre.y) < (r + s.height / 2) - 0.5 { return true }
            }
            return false
        }
        var placed = false
        if let top = bodyTop, mode == .framed || mode == .shell {
            let hy = max(top + G.homeFromBodyTop * unit, video.maxY + clearance * unit + hr)
            if hy + hr <= sy + sh - edgeMargin * unit, !collides(gapCentreX, hy, hr) {
                c["HOME"]  = .circle(centre: CGPoint(x: gapCentreX, y: hy),
                                     diameter: G.homeDiameter * unit)
                c["TV"]    = .circle(centre: CGPoint(x: gapCentreX + G.tvFromHome * unit, y: hy),
                                     diameter: G.menuDiameter * unit)
                c["POWER"] = .circle(centre: CGPoint(x: gapCentreX + G.powerFromHome * unit, y: hy),
                                     diameter: G.menuDiameter * unit)
                placed = true
            }
        }
        if !placed {
            let hx = mode == .overlay ? sx + sw / 2 : gapCentreX
            let hy = sy + sh - (edgeMargin + G.homeDiameter / 2) * unit
            if !collides(hx, hy, hr) {
                c["HOME"] = .circle(centre: CGPoint(x: hx, y: hy), diameter: G.homeDiameter * unit)
            } else {
                notes.append("HOME joins TV/POWER in the pause menu: nothing on the pad clears")
            }
            notes.append("TV/POWER belong to the pause menu at this size, not to the pad")
        }

        return PadLayout(mode: mode, unit: unit, lifeSizeUnit: lifeUnit, armAngle: armAngle,
                         systemPlacement: hardwareSystem ? .hardware : .elbow,
                         leftAnchor: CGPoint(x: lx, y: cy), rightAnchor: CGPoint(x: rx, y: cy),
                         video: video, controls: c, notes: notes)
    }

    static let dpadDeadZone: CGFloat = 0.18

    static func dpadDirections(at point: CGPoint, centre: CGPoint, size: CGSize) -> Set<String> {
        let dx = (point.x - centre.x) / (size.width / 2)
        let dy = (point.y - centre.y) / (size.height / 2)
        guard dx * dx + dy * dy > dpadDeadZone * dpadDeadZone else { return [] }
        let sector = Int(((atan2(dy, dx) + 2 * .pi + .pi / 8)
            .truncatingRemainder(dividingBy: 2 * .pi)) / (.pi / 4))
        switch sector {
        case 0: return ["right"]
        case 1: return ["right", "down"]
        case 2: return ["down"]
        case 3: return ["down", "left"]
        case 4: return ["left"]
        case 5: return ["left", "up"]
        case 6: return ["up"]
        default: return ["up", "right"]
        }
    }

    static func hitRadius(visualDiameter d: CGFloat, nearestNeighbourDistance n: CGFloat) -> CGFloat {
        min(max(d / 2, 22), n / 2)
    }
}

enum DeviceMetrics {

    enum Source: String { case calibrated, model, panel, derived }

    struct Measurement {
        let pointsPerInch: CGFloat
        let source: Source
        let identifier: String
        let detail: String
    }

    static let calibrationKey = "espressu.controls.pointsPerInch"
    /// ISO/IEC 7810 ID-1: every bank card, driving licence and library card on earth is
    static let referenceCardWidthMM: CGFloat = 85.60

    private static let padStandard: CGFloat = 264
    private static let padMini: CGFloat = 326
    private static let phoneOLED: CGFloat = 460
    private static let phoneMini: CGFloat = 476
    private static let phoneLCD: CGFloat = 326
    private static let phonePlus: CGFloat = 401

    private static let modelTable: [String: CGFloat] = [
        "iPad8,11": padStandard, "iPad8,12": padStandard,
        "iPad8,9":  padStandard, "iPad8,10": padStandard,
        "iPad5,3": padStandard,  "iPad5,4": padStandard,
        "iPad6,3": padStandard,  "iPad6,4": padStandard,
        "iPad6,11": padStandard, "iPad6,12": padStandard,
        "iPad7,5": padStandard,  "iPad7,6": padStandard,
        "iPad4,1": padStandard,  "iPad4,2": padStandard,  "iPad4,3": padStandard,
        "iPad4,4": padMini, "iPad4,5": padMini, "iPad4,6": padMini,
        "iPad4,7": padMini, "iPad4,8": padMini, "iPad4,9": padMini,
        "iPad11,1": padMini, "iPad11,2": padMini,
        "iPad14,1": padMini, "iPad14,2": padMini,
        "iPad16,1": padMini, "iPad16,2": padMini,
        "iPad5,1": padMini,  "iPad5,2": padMini,
        "iPhone13,1": phoneMini, "iPhone14,4": phoneMini,
        "iPhone7,1": phonePlus, "iPhone8,2": phonePlus, "iPhone9,2": phonePlus, "iPhone9,4": phonePlus,
        "iPhone10,2": phonePlus, "iPhone10,5": phonePlus,
        "iPhone14,6": phoneLCD, "iPhone12,8": phoneLCD,
        "iPhone11,8": phoneLCD, "iPhone12,1": phoneLCD,
    ]

    private static let panelTable: [String: CGFloat] = [
        "2732x2048": padStandard,
        "2388x1668": padStandard,
        "2420x1668": padStandard,
        "2752x2064": padStandard,
        "2360x1640": padStandard,
        "2160x1620": padStandard,
        "2224x1668": padStandard,
        "2266x1488": padMini,
        "2796x1290": phoneOLED, "2868x1320": phoneOLED, "2778x1284": phoneOLED,
        "2556x1179": phoneOLED, "2622x1206": phoneOLED, "2532x1170": phoneOLED,
        "2436x1125": phoneOLED, "2688x1242": phoneOLED, "2736x1260": phoneOLED,
        "2340x1080": phoneMini,
        "1792x828": phoneLCD, "1334x750": phoneLCD,
        "1920x1080": phonePlus,
    ]

    static func measurement(identifier: String,
                            nativePixels: CGSize,
                            scale: CGFloat,
                            isPad: Bool,
                            calibrated: CGFloat?) -> Measurement {
        if let c = calibrated, isPlausible(c) {
            return Measurement(pointsPerInch: c, source: .calibrated, identifier: identifier,
                               detail: String(format: "measured against a bank card: %.1f pt/in", c))
        }
        let safeScale = scale > 0 ? scale : 2
        if let native = modelTable[identifier] {
            return Measurement(pointsPerInch: native / safeScale, source: .model,
                               identifier: identifier,
                               detail: String(format: "%@ is a known model: %.0f ppi at %.0fx", identifier, native, safeScale))
        }
        let long = max(nativePixels.width, nativePixels.height)
        let short = min(nativePixels.width, nativePixels.height)
        let key = "\(Int(long))x\(Int(short))"
        if let native = panelTable[key] {
            return Measurement(pointsPerInch: native / safeScale, source: .panel,
                               identifier: identifier,
                               detail: "\(identifier) is unknown, but its \(key) panel is: \(Int(native)) ppi")
        }
        let native: CGFloat
        let why: String
        if isPad {
            if short <= 1536 { native = padMini; why = "iPad, short side \(Int(short)) px - mini class" }
            else { native = padStandard; why = "iPad, short side \(Int(short)) px - standard class" }
        } else if safeScale >= 2.9 {
            if short <= 1080 { native = phoneMini; why = "3x phone, \(Int(short)) px wide - mini class" }
            else { native = phoneOLED; why = "3x phone, \(Int(short)) px wide" }
        } else if safeScale > 2.5 {
            native = phonePlus; why = "phone at \(safeScale)x, \(Int(short)) px wide - Plus class"
        } else {
            native = phoneLCD; why = "2x phone"
        }
        return Measurement(pointsPerInch: native / safeScale, source: .derived, identifier: identifier,
                           detail: "\(identifier) unrecognised - derived from \(why); offer calibration")
    }

    static func isPlausible(_ pointsPerInch: CGFloat) -> Bool { pointsPerInch > 90 && pointsPerInch < 240 }

    static func calibration(fromCardWidthInPoints width: CGFloat) -> CGFloat {
        width / (referenceCardWidthMM / 25.4)
    }
}

#if canImport(UIKit)
import UIKit

extension DeviceMetrics {
    static func current(defaults: UserDefaults = .standard) -> Measurement {
        var size = 0
        sysctlbyname("hw.machine", nil, &size, nil, 0)
        var machine = [CChar](repeating: 0, count: max(size, 1))
        sysctlbyname("hw.machine", &machine, &size, nil, 0)
        let identifier = String(cString: machine)

        let screen = UIScreen.main
        let stored = defaults.object(forKey: calibrationKey) as? Double
        return measurement(identifier: identifier,
                           nativePixels: screen.nativeBounds.size,
                           scale: screen.nativeScale,
                           isPad: UIDevice.current.userInterfaceIdiom == .pad,
                           calibrated: stored.map { CGFloat($0) })
    }

    static func storeCalibration(cardWidthInPoints width: CGFloat, defaults: UserDefaults = .standard) {
        let ppi = calibration(fromCardWidthInPoints: width)
        guard isPlausible(ppi) else { return }
        defaults.set(Double(ppi), forKey: calibrationKey)
    }

    static func clearCalibration(defaults: UserDefaults = .standard) {
        defaults.removeObject(forKey: calibrationKey)
    }
}

#endif
