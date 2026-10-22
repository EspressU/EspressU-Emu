// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

enum ControllerLayoutSettings {
    static let scaleKey = "espressu.controls.scale"
    static let opacityKey = "espressu.controls.opacity"
    static let leftOffsetXKey = "espressu.controls.left.dx"
    static let leftOffsetYKey = "espressu.controls.left.dy"
    static let rightOffsetXKey = "espressu.controls.right.dx"
    static let rightOffsetYKey = "espressu.controls.right.dy"
    static let joystickKey = "espressu.controls.joystick"
    static let comfortControlsKey = "espressu.pad.comfortControls"
    static let defaultComfortControls = false
    static let rightStickOffsetXKey = "espressu.controls.rstick.dx"
    static let rightStickOffsetYKey = "espressu.controls.rstick.dy"
    static let leftStickOffsetXKey = "espressu.controls.lstick.dx"
    static let leftStickOffsetYKey = "espressu.controls.lstick.dy"
    static let deadzoneKey = "espressu.controls.stick.deadzone"
    static let stickCurveKey = "espressu.controls.stick.curve"
    static let stickGateKey = "espressu.controls.stick.gate"

    static let individualEditModeKey = "espressu.controls.individualEditMode"
    static let defaultIndividualEditMode = false

    static let hapticsKey = "espressu.pad.haptics"
    static let defaultHaptics = true

    static let autoHideWithControllerKey = "espressu.controls.autoHideWithController"
    static let defaultAutoHideWithController = true

    static let defaultJoystick = false

    static let defaultDeadzone: Double = 0.06
    static let minDeadzone: Double = 0.0
    static let maxDeadzone: Double = 0.30

    static let defaultStickCurve: Double = 1.0
    static let minStickCurve: Double = 1.0
    static let maxStickCurve: Double = 2.5

    static let defaultStickGate = ControllerGeometry.StickGate.round
    static let defaultStickGateRaw = ControllerGeometry.StickGate.round.rawValue
    static let defaultScale: Double = 1.0
    static let defaultOpacity: Double = 0.85
    static let minScale: Double = 0.6
    static let maxScale: Double = 1.6

    static let stickSpacingKey = "espressu.controls.stickSpacing"
    static let defaultStickSpacing: Double = 0
    static let minStickSpacing: Double = -3.0
    static let maxStickSpacing: Double = 1.5
    static let stickSpacingStep: Double = 0.25

    static func stickSpacingLabel(_ value: Double) -> String {
        abs(value) < 0.01 ? "default" : (value < 0 ? "closer" : "wider")
    }

    static let shoulderOffsetKey = "espressu.controls.shoulderOffset"
    static let defaultShoulderOffset: Double = 0
    static let minShoulderOffset: Double = -4.0
    static let maxShoulderOffset: Double = 1.5
    static let shoulderOffsetStep: Double = 0.25

    static func shoulderOffsetRange(touchLab: Bool) -> ClosedRange<Double> {
        (touchLab ? defaultShoulderOffset : minShoulderOffset)...maxShoulderOffset
    }

    static func shoulderOffsetLabel(_ value: Double) -> String {
        abs(value) < 0.01 ? "default" : (value < 0 ? "higher" : "lower")
    }

    static var supportsShoulderOffset: Bool {
        UIDevice.current.userInterfaceIdiom == .pad
    }

    static func effectiveShoulderOffset(_ stored: Double) -> Double {
        supportsShoulderOffset ? stored : defaultShoulderOffset
    }

    static func portraitKey(_ key: String) -> String { key + ".portrait" }

    static func reset() {
        let defaults = UserDefaults.standard
        for key in [scaleKey, opacityKey, stickSpacingKey, shoulderOffsetKey, rightStickOffsetXKey, rightStickOffsetYKey,
                    leftStickOffsetXKey, leftStickOffsetYKey,
                    leftOffsetXKey, leftOffsetYKey,
                    rightOffsetXKey, rightOffsetYKey] {
            defaults.removeObject(forKey: key)
            defaults.removeObject(forKey: portraitKey(key))
        }
        ControllerCustomLayout.shared.resetAll()
    }
}

enum ControllerGeometry {
    static let stickDiameter: CGFloat = 0.706
    static let systemDiameter: CGFloat = 0.773
    static let shoulderSize = CGSize(width: 1.151, height: 0.874)
    static let shoulderCornerRadius: CGFloat = 0.235

    static let centreFromNearEdge: CGFloat = 3.126
    static let centreFromBottom: CGFloat = 2.605

    static func automaticDiameter(in size: CGSize) -> CGFloat {
        let shortSide = min(size.width, size.height)
        return min(max(shortSide * 0.115, 46), 72)
    }

    enum Style: Equatable {
        case dpad
        case face
        case shoulder
        case system
        case stick
        case joystick
    }

    enum Shape {
        case circle(CGFloat)
        case roundedRect(CGSize, CGFloat)
    }

    struct Control: Identifiable {
        let id: String
        let glyph: String
        let offset: CGPoint
        let shape: Shape
        let style: Style
    }

    private static let crossX: CGFloat = 1.240
    private static let crossY: CGFloat = 1.155
    private static let systemOffset = CGPoint(x: 2.353, y: -1.025)
    private static let shoulderSpreadX: CGFloat = 1.004
    private static let shoulderY: CGFloat = -2.824

    private static let button = Shape.circle(1.0)
    private static let stick = Shape.circle(stickDiameter)
    private static let system = Shape.circle(systemDiameter)
    private static let shoulder = Shape.roundedRect(shoulderSize, shoulderCornerRadius)

    static let leftCluster: [Control] = [
        Control(id: "up",    glyph: "\u{25B2}", offset: CGPoint(x: 0, y: -crossY), shape: button, style: .dpad),
        Control(id: "left",  glyph: "\u{25C0}", offset: CGPoint(x: -crossX, y: 0), shape: button, style: .dpad),
        Control(id: "right", glyph: "\u{25B6}", offset: CGPoint(x: crossX, y: 0),  shape: button, style: .dpad),
        Control(id: "down",  glyph: "\u{25BC}", offset: CGPoint(x: 0, y: crossY),  shape: button, style: .dpad),
        Control(id: "L3",    glyph: "",         offset: .zero,                     shape: stick,  style: .stick),
        Control(id: "minus", glyph: "\u{2212}", offset: systemOffset,              shape: system, style: .system),
        Control(id: "L",     glyph: "L",  offset: CGPoint(x: shoulderSpreadX, y: shoulderY),  shape: shoulder, style: .shoulder),
        Control(id: "ZL",    glyph: "ZL", offset: CGPoint(x: -shoulderSpreadX, y: shoulderY), shape: shoulder, style: .shoulder)
    ]

    static let leftClusterComfort: [Control] = [
        Control(id: "up",    glyph: "\u{25B2}", offset: CGPoint(x: 0, y: -crossY), shape: button, style: .dpad),
        Control(id: "left",  glyph: "\u{25C0}", offset: CGPoint(x: -crossX, y: 0), shape: button, style: .dpad),
        Control(id: "right", glyph: "\u{25B6}", offset: CGPoint(x: crossX, y: 0),  shape: button, style: .dpad),
        Control(id: "down",  glyph: "\u{25BC}", offset: CGPoint(x: 0, y: crossY),  shape: button, style: .dpad),
        Control(id: "L3",    glyph: "",         offset: .zero,                     shape: stick,  style: .stick)
    ]

    static let leftStickCluster: [Control] = [
        Control(id: "stickL", glyph: "", offset: .zero,
                shape: .circle(stickBaseDiameter), style: .joystick)
    ]

    static let leftStickClusterComfort: [Control] = [
        Control(id: "stickL", glyph: "", offset: .zero,
                shape: .circle(stickBaseDiameter), style: .joystick),
        Control(id: "minus", glyph: "\u{2212}", offset: systemOffset, shape: system, style: .system),
        Control(id: "L",     glyph: "L",  offset: CGPoint(x: shoulderSpreadX, y: shoulderY),  shape: shoulder, style: .shoulder),
        Control(id: "ZL",    glyph: "ZL", offset: CGPoint(x: -shoulderSpreadX, y: shoulderY), shape: shoulder, style: .shoulder)
    ]

    static let leftStickAnchorOffset = CGPoint(x: 5.0, y: -1.6)

    static let stickBaseDiameter: CGFloat = 2 * crossX + 1.0
    static let stickKnobDiameter: CGFloat = 1.15
    static let stickTravelFraction: CGFloat = 1.0

    static func stickTravel(ringDiameter: CGFloat) -> CGFloat {
        ringDiameter / 2 * stickTravelFraction
    }

    /// The stick area is always a circle: full push in every direction.
    enum StickGate: String, CaseIterable, Identifiable {
        case round

        var id: String { rawValue }
        var title: String { "Round" }
        var summary: String { "Full push in every direction." }

        func radiusFraction(atAngle angle: CGFloat) -> CGFloat { 1 }
    }

    static let rightStickCluster: [Control] = [
        Control(id: "stickR", glyph: "", offset: .zero,
                shape: .circle(stickBaseDiameter), style: .joystick)
    ]

    static let rightStickClusterComfort: [Control] = [
        Control(id: "stickR", glyph: "", offset: .zero,
                shape: .circle(stickBaseDiameter), style: .joystick),
        Control(id: "plus", glyph: "\u{FF0B}", offset: CGPoint(x: -systemOffset.x, y: systemOffset.y), shape: system, style: .system),
        Control(id: "R",    glyph: "R",  offset: CGPoint(x: -shoulderSpreadX, y: shoulderY), shape: shoulder, style: .shoulder),
        Control(id: "ZR",   glyph: "ZR", offset: CGPoint(x: shoulderSpreadX, y: shoulderY),  shape: shoulder, style: .shoulder)
    ]

    static let rightStickAnchorOffset = CGPoint(x: -5.0, y: -1.6)

    static func stickShift(spacing: Double, containerWidth: CGFloat, unit: CGFloat,
                           left: [Control], right: [Control]) -> CGFloat {
        let requested = CGFloat(min(max(spacing, ControllerLayoutSettings.minStickSpacing),
                                    ControllerLayoutSettings.maxStickSpacing))
        guard requested < 0, unit > 0 else { return requested }
        let fromEdge = centreFromNearEdge + leftStickAnchorOffset.x
        let centreGap = containerWidth / unit - 2 * fromEdge
        let needed = bounds(of: left).maxX - bounds(of: right).minX + 0.5
        return max(requested, -max(0, (centreGap - needed) / 2))
    }

    static func shoulderShift(offset: Double, centreY: CGFloat, containerHeight: CGFloat, unit: CGFloat,
                              controls: [Control], topInset: CGFloat = 0) -> CGFloat {
        let requested = CGFloat(min(max(offset, ControllerLayoutSettings.minShoulderOffset),
                                    ControllerLayoutSettings.maxShoulderOffset))
        guard requested != 0, unit > 0 else { return 0 }
        let shoulders = controls.filter { $0.style == .shoulder }
        guard !shoulders.isEmpty else { return 0 }

        func extent(_ c: Control) -> (minX: CGFloat, maxX: CGFloat, minY: CGFloat, maxY: CGFloat) {
            let size: CGSize
            switch c.shape {
            case .circle(let diameter):    size = CGSize(width: diameter, height: diameter)
            case .roundedRect(let box, _): size = box
            }
            return (c.offset.x - size.width / 2, c.offset.x + size.width / 2,
                    c.offset.y - size.height / 2, c.offset.y + size.height / 2)
        }
        let shoulderBox = shoulders.map(extent)
        let top = shoulderBox.map { $0.minY }.min() ?? 0
        let bottom = shoulderBox.map { $0.maxY }.max() ?? 0
        let left = shoulderBox.map { $0.minX }.min() ?? 0
        let right = shoulderBox.map { $0.maxX }.max() ?? 0

        let up = -((centreY - topInset) / unit + top - 0.25)
        let gap: CGFloat = 0.2
        var down = (containerHeight - centreY) / unit - bottom
        for other in controls where other.style != .shoulder {
            let e = extent(other)
            guard e.maxX > left - gap, e.minX < right + gap, e.minY >= top else { continue }
            down = min(down, e.minY - gap - bottom)
        }
        return min(max(requested, min(up, 0)), max(down, 0))
    }

    static let stickClickThreshold: CGFloat = 0.14

    static let rightCluster: [Control] = [
        Control(id: "X", glyph: "X", offset: CGPoint(x: 0, y: -crossY), shape: button, style: .face),
        Control(id: "Y", glyph: "Y", offset: CGPoint(x: -crossX, y: 0), shape: button, style: .face),
        Control(id: "A", glyph: "A", offset: CGPoint(x: crossX, y: 0),  shape: button, style: .face),
        Control(id: "B", glyph: "B", offset: CGPoint(x: 0, y: crossY),  shape: button, style: .face),
        Control(id: "R3",   glyph: "",        offset: .zero, shape: stick, style: .stick),
        Control(id: "plus", glyph: "\u{FF0B}", offset: CGPoint(x: -systemOffset.x, y: systemOffset.y), shape: system, style: .system),
        Control(id: "R",    glyph: "R",  offset: CGPoint(x: -shoulderSpreadX, y: shoulderY), shape: shoulder, style: .shoulder),
        Control(id: "ZR",   glyph: "ZR", offset: CGPoint(x: shoulderSpreadX, y: shoulderY),  shape: shoulder, style: .shoulder)
    ]

    static let rightClusterComfort: [Control] = [
        Control(id: "X", glyph: "X", offset: CGPoint(x: 0, y: -crossY), shape: button, style: .face),
        Control(id: "Y", glyph: "Y", offset: CGPoint(x: -crossX, y: 0), shape: button, style: .face),
        Control(id: "A", glyph: "A", offset: CGPoint(x: crossX, y: 0),  shape: button, style: .face),
        Control(id: "B", glyph: "B", offset: CGPoint(x: 0, y: crossY),  shape: button, style: .face),
        Control(id: "R3", glyph: "", offset: .zero, shape: stick, style: .stick)
    ]

    static func size(of control: Control) -> CGSize {
        switch control.shape {
        case .circle(let diameter):    return CGSize(width: diameter, height: diameter)
        case .roundedRect(let box, _): return box
        }
    }

    static func bounds(of cluster: [Control]) -> CGRect {
        var minX = CGFloat.greatestFiniteMagnitude
        var minY = CGFloat.greatestFiniteMagnitude
        var maxX = -CGFloat.greatestFiniteMagnitude
        var maxY = -CGFloat.greatestFiniteMagnitude

        for control in cluster {
            let size: CGSize
            switch control.shape {
            case .circle(let diameter):    size = CGSize(width: diameter, height: diameter)
            case .roundedRect(let box, _): size = box
            }
            minX = min(minX, control.offset.x - size.width / 2)
            maxX = max(maxX, control.offset.x + size.width / 2)
            minY = min(minY, control.offset.y - size.height / 2)
            maxY = max(maxY, control.offset.y + size.height / 2)
        }

        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }
}
