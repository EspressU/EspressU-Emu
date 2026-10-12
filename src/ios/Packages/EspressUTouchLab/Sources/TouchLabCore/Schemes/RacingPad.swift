import CoreGraphics
import Foundation

public final class RacingPad: ControlScheme {
    public static let schemeInfo = SchemeInfo(
        id: "racing",
        name: "Racing",
        summary: "Made for Mario Kart 8: a big steering area, Accelerate, Brake and Drift pedals where your thumb rests, and an A+R zone for drifting.")

    public struct Options: Equatable, Sendable {
        public var autoAccelerate: Bool
        public var tilt: Bool
        public init(autoAccelerate: Bool = false, tilt: Bool = false) {
            self.autoAccelerate = autoAccelerate
            self.tilt = tilt
        }
    }

    public let options: Options
    public let aScale: CGFloat

    public static let tiltFullLock: Double = 35 * .pi / 180

    private var lastAngle: Double?
    private var centreAngle: Double?
    public private(set) var tiltValue: Double = 0

    public init(options: Options = Options(), aScale: CGFloat = 1) {
        self.options = options
        self.aScale = aScale
        super.init(info: Self.schemeInfo)
    }

    override public func makeControls(_ context: LayoutContext) -> [PadControl] {
        RacingPad.build(context, options: options, aScale: aScale)
    }

    public static func build(_ ctx: LayoutContext, options: Options = Options(), aScale: CGFloat = 1) -> [PadControl] {
        var u = ctx.unit
        for _ in 0..<12 {
            if let set = arrangement(ctx, u: u, options: options, aScale: aScale),
               LayoutCheck.problems(set, in: ctx.safeBounds).isEmpty {
                return set
            }
            u *= 0.92
        }
        return arrangement(ctx, u: u, options: options, aScale: aScale, force: true) ?? []
    }

    static let pedalGroup = 31

    static func arrangement(_ ctx: LayoutContext, u: CGFloat, options: Options, aScale: CGFloat = 1, force: Bool = false) -> [PadControl]? {
        let s = ctx.safeBounds
        guard s.width > 0, s.height > 0 else { return nil }
        let landscape = s.width >= s.height
        let bandHeight = landscape ? s.height : min(s.height, max(8.5 * u, 0.46 * s.height))
        let bandTop = s.maxY - bandHeight
        let gap = 0.05 * u

        let a = PadParts.clampedAScale(aScale)
        let wA = 2.7 * a * u, wB = 1.8 * u
        let hA = 2.3 * a * u, hC = 1.5 * u, hR = 1.3 * u
        let xA = s.maxX - wA, xB = xA - wB
        let yA = s.maxY - hA, yC = yA - hC, yR = yC - hR
        func inset(_ r: CGRect) -> CGRect { r.insetBy(dx: gap / 2, dy: gap / 2) }
        func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect { inset(CGRect(x: x, y: y, width: w, height: h)) }
        let corner = 0.3 * u
        let reach = gap

        func pedal(_ buttons: Set<PadButton>, _ r: CGRect, _ role: RenderElement.Role, _ label: String) -> PadControl {
            PadControl(.pedal(buttons), shape: .roundedRect(r, cornerRadius: corner), role: role, label: label,
                       group: pedalGroup, reach: reach, chords: false)
        }
        var controls: [PadControl] = [
            pedal([.a], rect(xA, yA, wA, hA), .pedal, "A"),
            pedal([.b], rect(xB, yC, wB, hA + hC), .pedal, "B"),
            pedal([.a, .r], rect(xA, yC, wA, hC), .pedal, "A+R"),
            pedal([.r], rect(xB, yR, wB + wA, hR), .pedal, "R"),
        ]

        let steerMax = 0.36 * s.width
        let itemW = 1.7 * u, itemH = 1.25 * u
        let margin = 0.6 * u
        let centreRoom = xB - s.minX - steerMax
        var steerRight = s.minX + steerMax
        var steerTop = bandTop + (landscape ? 0.25 : 0.08) * bandHeight
        let itemRect: CGRect
        if centreRoom >= itemW + 2 * margin {
            let cx = (steerRight + xB) / 2
            itemRect = CGRect(x: cx - itemW / 2, y: yC - 0.2 * u, width: itemW, height: itemH)
        } else {
            itemRect = CGRect(x: s.minX + 0.3 * u, y: bandTop + 0.3 * u, width: itemW, height: itemH)
            steerTop = max(steerTop, itemRect.maxY + 0.35 * u)
            steerRight = min(steerRight, xB - 0.4 * u)
        }
        controls.append(PadControl(.button(.l), shape: .roundedRect(itemRect, cornerRadius: corner), role: .shoulder, label: "L",
                                   reach: 0.2 * u, priority: 2))

        let lockX = max(1.2 * u, min(0.3 * (steerRight - s.minX), 2.2 * u))
        let lockY = 1.2 * lockX
        let steer = CGRect(x: s.minX, y: steerTop, width: steerRight - s.minX, height: s.maxY - steerTop)
        guard steer.width >= 2.8 * u, steer.height >= 3 * u || force else { return force ? controls : nil }
        let spec = SteerSpec(stick: .left, lockX: lockX, lockY: lockY, deadY: 0.45, followPast: 1.3, tilt: options.tilt)
        controls.append(PadControl(.steer(spec), shape: .roundedRect(inset(steer), cornerRadius: corner), role: .area,
                                   label: options.tilt ? "TILT" : "STEER", priority: 0))

        let rowY = bandTop + 0.2 * u + 0.4 * u
        let step = 1.3 * u
        var row: [PadControl] = [
            PadControl(.button(.x), shape: .circle(center: CGPoint(x: s.midX - step, y: rowY), radius: PadParts.systemDiameter / 2 * u),
                       role: .system, label: "X", reach: 0.25 * u, priority: 2),
            PadParts.system(.plus, at: CGPoint(x: s.midX, y: rowY), u: u),
            PadParts.system(.home, at: CGPoint(x: s.midX + step, y: rowY), u: u),
        ]
        if options.tilt {
            row.append(PadControl(.recentre, shape: .circle(center: CGPoint(x: s.midX + 2 * step, y: rowY), radius: PadParts.systemDiameter / 2 * u),
                                  role: .system, label: "C", reach: 0.25 * u, priority: 2))
        }
        for i in row.indices { row[i].priority = 2 }
        controls += row
        return controls
    }

    override public var wantsMotion: Bool { options.tilt }

    public func recentre() {
        centreAngle = lastAngle
        applyTilt()
    }

    override public func recentreRequested() { recentre() }

    override public func motion(angle: Double) -> [TouchID: Contribution] {
        guard options.tilt else { return [:] }
        lastAngle = angle
        if centreAngle == nil { centreAngle = angle }
        applyTilt()
        var out: [TouchID: Contribution] = [:]
        for (id, t) in tracks {
            if case .steer = controls[t.control].kind {
                out[id] = contribution(for: &tracks[id]!, at: t.last, time: t.lastTime)
            }
        }
        return out
    }

    private func applyTilt() {
        guard options.tilt, let angle = lastAngle, let centre = centreAngle else {
            tiltValue = 0
            steerOverrideX = options.tilt ? 0 : nil
            return
        }
        var d = angle - centre
        while d > .pi { d -= 2 * .pi }
        while d < -.pi { d += 2 * .pi }
        let dead = context.stick.deadzone
        let raw = abs(d) / Self.tiltFullLock
        var v = 0.0
        if raw > dead {
            let live = min((raw - dead) / max(1 - dead, 0.0001), 1)
            v = pow(live, context.stick.curve)
        }
        tiltValue = d < 0 ? -v : v
        steerOverrideX = tiltValue
    }

    override public func ambient() -> Contribution? {
        var buttons: Set<PadButton> = []
        if options.autoAccelerate {
            let braking = tracks.values.contains { $0.buttons.contains(.b) }
            if !braking { buttons.insert(.a) }
        }
        var stick: PadStick?
        var value = StickValue.zero
        if options.tilt {
            let steering = tracks.values.contains { if case .steer = controls[$0.control].kind { return true }; return false }
            if !steering { stick = .left; value = StickValue(x: tiltValue, y: 0) }
        }
        if buttons.isEmpty && stick == nil { return nil }
        return Contribution(buttons: buttons, stick: stick, stickValue: value)
    }

    override public func layout(_ context: LayoutContext) {
        super.layout(context)
        applyTilt()
    }
}
