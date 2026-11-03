// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.

import CoreGraphics
import UIKit

/// What the player can tune from the layout panel.
struct CremaSettings: Equatable {
    var scale: Double = ControllerLayoutSettings.defaultScale
    var stickSpacing: Double = ControllerLayoutSettings.defaultStickSpacing
    var shoulderOffset: Double = ControllerLayoutSettings.defaultShoulderOffset
    var deadzone: Double = ControllerLayoutSettings.defaultDeadzone
    var elements = CremaElementOverrides()
}

/// Where every Crema control sits for a given screen. Pure geometry: no views, no touches.
///
/// Everything is measured in `unit`, one face button's diameter. Two columns hug the bottom corners: a
/// stick above a D-pad on the left, face buttons above a stick on the right. Shoulder buttons sit above
/// the columns and +, - and HOME run along the bottom centre (or below the columns on narrow screens).
struct CremaLayout {
    struct Ring {
        var side: CremaSide
        var centre: CGPoint
        var radius: CGFloat
        var claimRadius: CGFloat
        var knobRadius: CGFloat
    }

    struct Arm {
        var key: CremaKey
        /// Screen angle in radians: 0 points right and positive turns clockwise.
        var angle: CGFloat
    }

    struct Cluster {
        var centre: CGPoint
        var reach: CGFloat
        var dead: CGFloat
        var arms: [Arm]
        /// Size factor of the drawing, 1 unless the player resized it.
        var scale: CGFloat = 1
    }

    struct FaceButton {
        var key: CremaKey
        var centre: CGPoint
        var radius: CGFloat
    }

    struct Pill {
        var key: CremaKey
        var frame: CGRect
        var isRound: Bool
    }

    var unit: CGFloat = 0
    var rings: [Ring] = []
    var dpad = Cluster(centre: .zero, reach: 0, dead: 0, arms: [])
    var face = Cluster(centre: .zero, reach: 0, dead: 0, arms: [])
    var pills: [Pill] = []
    var faceButtons: [FaceButton] = []
    /// Where every control sits, for the editor.
    var frames: [CremaElement: CGRect] = [:]

    /// The least height the controls need below the picture on a phone held upright.
    static let portraitMinimumHeight: CGFloat = 378

    /// How far a finger may stray from an arm's direction and still press it, in radians. Wider than a
    /// quarter turn, so a finger between two arms presses both.
    private static let armSpread: CGFloat = 0.96

    static func make(size: CGSize, safe: UIEdgeInsets, topInset: CGFloat, settings: CremaSettings) -> CremaLayout {
        guard size.width > 0, size.height > 0 else { return CremaLayout() }
        let portrait = size.height >= size.width
        let short = min(size.width, size.height)

        let margin: CGFloat = 10
        let minX = max(safe.left, 0) + margin
        let maxX = size.width - max(safe.right, 0) - margin
        let bottom = size.height - max(safe.bottom, 10) - 4
        let midX = (minX + maxX) / 2

        // The columns and the shoulder buttons above them need about 8.8 units of height. On a short
        // screen the unit shrinks so they fit below the top bar instead of running into each other.
        let wanted = min(max(short * 0.092, 34), 60)
        let fitting = (bottom - topInset - 6) / 8.8
        let u = min(wanted, max(fitting, 26)) * CGFloat(min(max(settings.scale, 0.4), 2))

        let inset = min(max(2.8 - CGFloat(settings.stickSpacing), 2.3), 7) * u
        let columnInset = min(inset, (maxX - minX) / 2 - 1.8 * u)
        let leftX = minX + columnInset
        let rightX = maxX - columnInset

        let rowFitsBetween = midX - 2.5 * u > leftX + 2.4 * u
        let rowBelow = portrait || !rowFitsBetween
        let columnsBottom = bottom - (rowBelow ? 1.6 * u : 0)

        var layout = CremaLayout()
        layout.unit = u

        let dpadY = columnsBottom - 1.8 * u
        let leftStickY = dpadY - 3.85 * u
        let rightStickY = columnsBottom - 1.7 * u
        let faceY = rightStickY - 3.85 * u

        // The two sides are deliberately staggered so nothing lines up in a perfect column: on the left the
        // stick sits further out and the D-pad further in, on the right the buttons sit further out.
        let stagger = 0.9 * u

        // A control the player moved or resized: shifted by its offset, kept inside the screen. Controls left
        // alone come back exactly as they were.
        let boundsMinX = max(safe.left, 0)
        let boundsMaxX = size.width - max(safe.right, 0)
        let boundsMinY = topInset
        let boundsMaxY = size.height - max(safe.bottom, 0)
        func place(_ element: CremaElement, centre: CGPoint, half: CGSize) -> (centre: CGPoint, scale: CGFloat) {
            let tweak = settings.elements[element]
            let factor = tweak.isDefault ? 1 : CGFloat(tweak.scale)
            var moved = centre
            if !tweak.isDefault {
                let hw = half.width * factor
                let hh = half.height * factor
                moved.x = min(max(centre.x + CGFloat(tweak.x) * u, boundsMinX + hw), max(boundsMaxX - hw, boundsMinX + hw))
                moved.y = min(max(centre.y + CGFloat(tweak.y) * u, boundsMinY + hh), max(boundsMaxY - hh, boundsMinY + hh))
            }
            layout.frames[element] = CGRect(x: moved.x - half.width * factor, y: moved.y - half.height * factor,
                                            width: half.width * factor * 2, height: half.height * factor * 2)
            return (moved, factor)
        }

        func ring(_ side: CremaSide, _ element: CremaElement, at centre: CGPoint) -> Ring {
            let placed = place(element, centre: centre, half: CGSize(width: 1.55 * u, height: 1.55 * u))
            return Ring(side: side, centre: placed.centre, radius: 1.55 * u * placed.scale,
                        claimRadius: 1.9 * u * placed.scale, knobRadius: 0.7 * u * placed.scale)
        }
        layout.rings = [
            ring(.left, .leftStick, at: CGPoint(x: leftX - 0.3 * u, y: leftStickY)),
            ring(.right, .rightStick, at: CGPoint(x: rightX - stagger, y: rightStickY)),
        ]
        let dpadPlaced = place(.dpad, centre: CGPoint(x: leftX + stagger, y: dpadY),
                               half: CGSize(width: 1.75 * u, height: 1.75 * u))
        layout.dpad = Cluster(
            centre: dpadPlaced.centre, reach: 1.85 * u * dpadPlaced.scale, dead: 0.4 * u * dpadPlaced.scale,
            arms: [Arm(key: .up, angle: -.pi / 2), Arm(key: .right, angle: 0),
                   Arm(key: .down, angle: .pi / 2), Arm(key: .left, angle: .pi)],
            scale: dpadPlaced.scale)
        let facePlaced = place(.faceButtons, centre: CGPoint(x: rightX + 0.3 * u, y: faceY),
                               half: CGSize(width: 1.9 * u, height: 1.9 * u))
        layout.face = Cluster(
            centre: facePlaced.centre, reach: 2.05 * u * facePlaced.scale, dead: 0.4 * u * facePlaced.scale,
            arms: [Arm(key: .x, angle: -.pi / 2), Arm(key: .a, angle: 0),
                   Arm(key: .b, angle: .pi / 2), Arm(key: .y, angle: .pi)],
            scale: facePlaced.scale)
        for arm in layout.face.arms {
            let distance = 1.3 * u * facePlaced.scale
            layout.faceButtons.append(FaceButton(
                key: arm.key,
                centre: CGPoint(x: layout.face.centre.x + cos(arm.angle) * distance,
                                y: layout.face.centre.y + sin(arm.angle) * distance),
                radius: 0.58 * u * facePlaced.scale))
        }

        let shoulderW = 2.3 * u
        let shoulderH = 0.95 * u
        let columnsTop = min(leftStickY - 1.55 * u, faceY - 1.9 * u)
        let shoulderWish = columnsTop - 0.3 * u - shoulderH + CGFloat(min(max(settings.shoulderOffset, -4), 1.5)) * u
        let lowest = columnsTop - shoulderH - 0.2 * u
        let shoulderY = min(max(shoulderWish, topInset + 6), max(lowest, topInset + 6))

        let shoulderGap = 0.35 * u
        func pill(_ key: CremaKey, _ element: CremaElement, frame: CGRect, isRound: Bool) -> Pill {
            let placed = place(element, centre: CGPoint(x: frame.midX, y: frame.midY),
                               half: CGSize(width: frame.width / 2, height: frame.height / 2))
            let w = frame.width * placed.scale
            let h = frame.height * placed.scale
            return Pill(key: key, frame: CGRect(x: placed.centre.x - w / 2, y: placed.centre.y - h / 2, width: w, height: h),
                        isRound: isRound)
        }
        func shoulder(_ key: CremaKey, _ element: CremaElement, x: CGFloat) -> Pill {
            pill(key, element, frame: CGRect(x: x, y: shoulderY, width: shoulderW, height: shoulderH), isRound: false)
        }
        layout.pills = [
            shoulder(.zl, .zl, x: leftX - shoulderGap / 2 - shoulderW),
            shoulder(.l, .l, x: leftX + shoulderGap / 2),
            shoulder(.r, .r, x: rightX - shoulderGap / 2 - shoulderW),
            shoulder(.zr, .zr, x: rightX + shoulderGap / 2),
        ]

        let rowY = bottom - 0.7 * u
        let round = 1.0 * u
        func system(_ key: CremaKey, _ element: CremaElement, x: CGFloat) -> Pill {
            pill(key, element, frame: CGRect(x: x - round / 2, y: rowY - round / 2, width: round, height: round), isRound: true)
        }
        layout.pills += [
            system(.minus, .minus, x: midX - 1.9 * u),
            system(.home, .home, x: midX),
            system(.plus, .plus, x: midX + 1.9 * u),
        ]
        return layout
    }

    // MARK: Hit testing

    func ring(at point: CGPoint) -> Ring? {
        rings.first { hypot(point.x - $0.centre.x, point.y - $0.centre.y) <= $0.claimRadius }
    }

    /// The keys a finger at `point` holds down. Empty when it touches nothing.
    func keys(at point: CGPoint) -> Set<CremaKey> {
        var held: Set<CremaKey> = []
        for cluster in [dpad, face] {
            let dx = point.x - cluster.centre.x
            let dy = point.y - cluster.centre.y
            let distance = hypot(dx, dy)
            guard distance <= cluster.reach, distance >= cluster.dead else { continue }
            let angle = atan2(dy, dx)
            for arm in cluster.arms where Self.angularGap(angle, arm.angle) <= Self.armSpread {
                held.insert(arm.key)
            }
        }
        let slack = -0.2 * unit
        if let pill = pills.first(where: { $0.frame.insetBy(dx: slack, dy: slack).contains(point) }) {
            held.insert(pill.key)
        }
        return held
    }

    private static func angularGap(_ a: CGFloat, _ b: CGFloat) -> CGFloat {
        var gap = abs(a - b).truncatingRemainder(dividingBy: 2 * .pi)
        if gap > .pi { gap = 2 * .pi - gap }
        return gap
    }
}
