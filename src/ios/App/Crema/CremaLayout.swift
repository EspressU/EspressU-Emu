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
        layout.rings = [
            Ring(side: .left, centre: CGPoint(x: leftX - 0.3 * u, y: leftStickY), radius: 1.55 * u, claimRadius: 1.9 * u),
            Ring(side: .right, centre: CGPoint(x: rightX - stagger, y: rightStickY), radius: 1.55 * u, claimRadius: 1.9 * u),
        ]
        layout.dpad = Cluster(
            centre: CGPoint(x: leftX + stagger, y: dpadY), reach: 1.85 * u, dead: 0.4 * u,
            arms: [Arm(key: .up, angle: -.pi / 2), Arm(key: .right, angle: 0),
                   Arm(key: .down, angle: .pi / 2), Arm(key: .left, angle: .pi)])
        layout.face = Cluster(
            centre: CGPoint(x: rightX + 0.3 * u, y: faceY), reach: 2.05 * u, dead: 0.4 * u,
            arms: [Arm(key: .x, angle: -.pi / 2), Arm(key: .a, angle: 0),
                   Arm(key: .b, angle: .pi / 2), Arm(key: .y, angle: .pi)])

        let shoulderW = 2.3 * u
        let shoulderH = 0.95 * u
        let columnsTop = min(leftStickY - 1.55 * u, faceY - 1.9 * u)
        let shoulderWish = columnsTop - 0.3 * u - shoulderH + CGFloat(min(max(settings.shoulderOffset, -4), 1.5)) * u
        let lowest = columnsTop - shoulderH - 0.2 * u
        let shoulderY = min(max(shoulderWish, topInset + 6), max(lowest, topInset + 6))

        let shoulderGap = 0.35 * u
        func shoulder(_ key: CremaKey, x: CGFloat) -> Pill {
            Pill(key: key, frame: CGRect(x: x, y: shoulderY, width: shoulderW, height: shoulderH), isRound: false)
        }
        layout.pills = [
            shoulder(.zl, x: leftX - shoulderGap / 2 - shoulderW),
            shoulder(.l, x: leftX + shoulderGap / 2),
            shoulder(.r, x: rightX - shoulderGap / 2 - shoulderW),
            shoulder(.zr, x: rightX + shoulderGap / 2),
        ]

        let rowY = bottom - 0.7 * u
        let round = 1.0 * u
        func system(_ key: CremaKey, x: CGFloat) -> Pill {
            Pill(key: key, frame: CGRect(x: x - round / 2, y: rowY - round / 2, width: round, height: round), isRound: true)
        }
        layout.pills += [
            system(.minus, x: midX - 1.9 * u),
            system(.home, x: midX),
            system(.plus, x: midX + 1.9 * u),
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
