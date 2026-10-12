import CoreGraphics

public enum PadParts {
    public static let crossX: CGFloat = 1.240
    public static let crossY: CGFloat = crossX
    public static let dotDiameter: CGFloat = 0.706
    public static let systemDiameter: CGFloat = 0.773
    public static let shoulderSize = CGSize(width: 1.151, height: 0.874)
    public static let shoulderCorner: CGFloat = 0.235
    public static let stickBaseDiameter: CGFloat = 2 * crossX + 1.0
    public static let knobDiameter: CGFloat = 1.15
    public static var stickTravel: CGFloat { (stickBaseDiameter - knobDiameter) / 2 }
    public static var clusterRadius: CGFloat { crossX + 0.5 }

    public enum Group {
        public static let face = 21
        public static let leftShoulders = 11
        public static let rightShoulders = 12
    }

    public static let aScaleRange: ClosedRange<CGFloat> = 1.0...1.8
    public static let largeANeighbourScale: CGFloat = 0.9

    public static func clampedAScale(_ a: CGFloat) -> CGFloat {
        a.isFinite ? min(max(a, aScaleRange.lowerBound), aScaleRange.upperBound) : 1
    }

    public static func neighbourScale(forA a: CGFloat) -> CGFloat {
        let t = (clampedAScale(a) - aScaleRange.lowerBound) / (aScaleRange.upperBound - aScaleRange.lowerBound)
        return 1 - (1 - largeANeighbourScale) * t
    }

    public static func faceDiamond(_ c: CGPoint, u: CGFloat, cluster: Int = -1, scale k: CGFloat = 1,
                                   reach: CGFloat = 0.45, rDot: Bool = true, aScale: CGFloat = 1) -> [PadControl] {
        let a = clampedAScale(aScale), nb = neighbourScale(forA: a)
        func face(_ b: PadButton, _ dx: CGFloat, _ dy: CGFloat) -> PadControl {
            let grow: CGFloat = b == .a ? a : nb
            return PadControl(.button(b), shape: .circle(center: c + CGPoint(x: dx * u * k, y: dy * u * k), radius: 0.5 * u * k * grow),
                       role: .face, label: b.description, group: Group.face, cluster: cluster,
                       reach: reach * u * grow, chords: true)
        }
        var out = [face(.x, 0, -crossY), face(.y, -crossX, 0), face(.a, crossX, 0), face(.b, 0, crossY)]
        if rDot {
            out.append(PadControl(.button(.stickR), shape: .circle(center: c, radius: dotDiameter / 2 * u * k),
                                  role: .dot, label: "R3", cluster: cluster, reach: 0.08 * u, priority: 2))
        }
        return out
    }

    public static func dpad(_ c: CGPoint, u: CGFloat, cluster: Int = -1, scale k: CGFloat = 1,
                            click: PadButton? = .stickL) -> PadControl {
        PadControl(.dpad(click: click, clickRadius: dotDiameter / 2 * u * k,
                         armOffset: CGPoint(x: crossX * u * k, y: crossY * u * k), armSize: u * k),
                   shape: .circle(center: c, radius: (clusterRadius + 0.3) * u * k),
                   role: .dpad, cluster: cluster)
    }

    public static func stick(_ s: PadStick, at c: CGPoint, u: CGFloat, cluster: Int = -1, scale k: CGFloat = 1,
                             click: PadButton? = nil) -> PadControl {
        PadControl(.stick(s, travel: stickTravel * u * k, click: click),
                   shape: .circle(center: c, radius: stickBaseDiameter / 2 * u * k),
                   role: .stickBase, label: s == .left ? "L" : "R", cluster: cluster, reach: 0.25 * u)
    }

    public static func shoulder(_ b: PadButton, _ rect: CGRect, u: CGFloat, group: Int, cluster: Int = -1) -> PadControl {
        PadControl(.button(b), shape: .roundedRect(rect, cornerRadius: shoulderCorner * u),
                   role: .shoulder, label: b.description, group: group, cluster: cluster, reach: 0.3 * u)
    }

    public static func system(_ b: PadButton, at c: CGPoint, u: CGFloat, cluster: Int = -1) -> PadControl {
        PadControl(.button(b), shape: .circle(center: c, radius: systemDiameter / 2 * u),
                   role: .system, label: b.description, cluster: cluster, reach: 0.25 * u)
    }
}

public enum GamePadArrangement {
    public struct Clusters {
        public static let dpad = 1
        public static let face = 2
        public static let leftStick = 3
        public static let rightStick = 4
        public static let leftShoulders = 5
        public static let rightShoulders = 6
        public static let system = 7
    }

    public static func build(_ ctx: LayoutContext, aScale: CGFloat = 1) -> [PadControl] {
        var u = ctx.unit
        for _ in 0..<8 {
            for inboard in [false, true] {
                let set = arrangement(ctx, u: u, sticksInboard: inboard, aScale: aScale)
                if LayoutCheck.problems(set, in: ctx.safeBounds).isEmpty {
                    let spacing = fittingSpacing(ctx, u: u, sticksInboard: inboard, aScale: aScale)
                    let shoulder = fittingShoulderOffset(ctx, u: u, sticksInboard: inboard, stickSpacing: spacing, aScale: aScale)
                    if spacing == 0 && shoulder == 0 { return set }
                    return arrangement(ctx, u: u, sticksInboard: inboard,
                                       stickSpacing: spacing, shoulderOffset: shoulder, aScale: aScale)
                }
            }
            u *= 0.92
        }
        return arrangement(ctx, u: u, sticksInboard: true, aScale: aScale)
    }

    static func fittingSpacing(_ ctx: LayoutContext, u: CGFloat, sticksInboard: Bool, aScale: CGFloat = 1) -> CGFloat {
        let requested = ctx.stickSpacing
        guard requested != 0 else { return 0 }
        var spacing = requested
        while abs(spacing) > 0.001 {
            let set = arrangement(ctx, u: u, sticksInboard: sticksInboard, stickSpacing: spacing, aScale: aScale)
            if LayoutCheck.problems(set, in: ctx.safeBounds).isEmpty { return spacing }
            spacing = requested > 0 ? max(0, spacing - 0.25) : min(0, spacing + 0.25)
        }
        return 0
    }

    static func fittingShoulderOffset(_ ctx: LayoutContext, u: CGFloat, sticksInboard: Bool,
                                      stickSpacing: CGFloat, aScale: CGFloat = 1) -> CGFloat {
        let requested = max(0, ctx.shoulderOffset)
        guard requested > 0.001 else { return 0 }
        func fits(_ drop: CGFloat) -> Bool {
            let set = arrangement(ctx, u: u, sticksInboard: sticksInboard,
                                  stickSpacing: stickSpacing, shoulderOffset: drop, aScale: aScale)
            return LayoutCheck.problems(set, in: ctx.safeBounds).isEmpty
        }
        if fits(requested) { return requested }
        var low: CGFloat = 0, high = requested
        for _ in 0..<12 {
            let mid = (low + high) / 2
            if fits(mid) { low = mid } else { high = mid }
        }
        return low
    }

    static func arrangement(_ ctx: LayoutContext, u: CGFloat, sticksInboard: Bool,
                            stickSpacing: CGFloat = 0, shoulderOffset: CGFloat = 0, aScale: CGFloat = 1) -> [PadControl] {
        let s = ctx.safeBounds
        let cy = s.maxY - (PadParts.clusterRadius + 0.75) * u
        let aExtra = 0.5 * (PadParts.clampedAScale(aScale) - 1) * u
        let cl = CGPoint(x: s.minX + (PadParts.clusterRadius + 0.85) * u, y: cy)
        let cr = CGPoint(x: s.maxX - (PadParts.clusterRadius + 0.85) * u - aExtra, y: cy)
        let stickR = PadParts.stickBaseDiameter / 2 * u

        var sl: CGPoint, sr: CGPoint
        if sticksInboard {
            let dx = PadParts.clusterRadius * u + stickR + 0.55 * u
            sl = CGPoint(x: cl.x + dx, y: cy - 0.9 * u)
            sr = CGPoint(x: cr.x - dx, y: cy - 0.9 * u)
        } else {
            let dy = PadParts.clusterRadius * u + stickR + 0.75 * u
            sl = CGPoint(x: cl.x + 0.3 * u, y: cy - dy)
            sr = CGPoint(x: cr.x - 0.3 * u, y: cy - dy)
        }
        sl.x -= stickSpacing * u
        sr.x += stickSpacing * u

        let sh = CGSize(width: 1.9 * u, height: 1.0 * u)
        let top = s.minY + 0.25 * u + shoulderOffset * u
        let zl = CGRect(x: s.minX + 0.25 * u, y: top, width: sh.width, height: sh.height)
        let l = zl.offsetBy(dx: sh.width + 0.3 * u, dy: 0)
        let zr = CGRect(x: s.maxX - 0.25 * u - sh.width, y: top, width: sh.width, height: sh.height)
        let r = zr.offsetBy(dx: -(sh.width + 0.3 * u), dy: 0)

        typealias C = Clusters
        return [
            PadParts.shoulder(.zl, zl, u: u, group: PadParts.Group.leftShoulders, cluster: C.leftShoulders),
            PadParts.shoulder(.l, l, u: u, group: PadParts.Group.leftShoulders, cluster: C.leftShoulders),
            PadParts.shoulder(.zr, zr, u: u, group: PadParts.Group.rightShoulders, cluster: C.rightShoulders),
            PadParts.shoulder(.r, r, u: u, group: PadParts.Group.rightShoulders, cluster: C.rightShoulders),
            PadParts.stick(.left, at: sl, u: u, cluster: C.leftStick),
            PadParts.stick(.right, at: sr, u: u, cluster: C.rightStick),
            PadParts.dpad(cl, u: u, cluster: C.dpad),
            PadParts.system(.minus, at: cl + CGPoint(x: 2.353 * u, y: -1.025 * u), u: u, cluster: C.dpad),
            PadParts.system(.plus, at: cr + CGPoint(x: -2.353 * u, y: -1.025 * u), u: u, cluster: C.face),
            PadParts.system(.home, at: CGPoint(x: s.midX, y: s.maxY - 0.6 * u), u: u, cluster: C.system),
        ] + PadParts.faceDiamond(cr, u: u, cluster: C.face, aScale: aScale)
    }
}

public enum LayoutCheck {
    public static func problems(_ controls: [PadControl], in bounds: CGRect) -> [String] {
        var out: [String] = []
        let drawn = controls.filter { !$0.isZone }
        for c in drawn {
            let box = c.shape.boundingBox
            if box.minX < bounds.minX - 0.5 || box.maxX > bounds.maxX + 0.5 ||
                box.minY < bounds.minY - 0.5 || box.maxY > bounds.maxY + 0.5 {
                out.append("\(name(c)) leaves the safe area")
            }
        }
        for i in drawn.indices {
            for j in drawn.indices where j > i {
                let a = drawn[i], b = drawn[j]
                if a.role == .dot || b.role == .dot { continue }
                if a.shape.overlaps(b.shape) { out.append("\(name(a)) overlaps \(name(b))") }
            }
        }
        return out
    }

    static func name(_ c: PadControl) -> String {
        switch c.kind {
        case .button(let b): return b.description
        case .dpad: return "d-pad"
        case .stick(let s, _, _), .floatingStick(let s, _, _, _, _), .swipeStick(let s, _):
            return s == .left ? "left stick" : "right stick"
        case .pedal(let set): return "pedal " + set.map(\.description).sorted().joined(separator: "+")
        case .steer: return "steering area"
        case .recentre: return "recentre"
        }
    }
}
