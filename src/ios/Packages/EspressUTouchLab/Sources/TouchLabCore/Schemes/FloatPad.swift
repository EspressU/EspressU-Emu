import CoreGraphics

public final class FloatPad: ControlScheme {
    public enum Camera: String, CaseIterable, Sendable {
        case stick, swipe
    }

    public static let schemeInfo = SchemeInfo(
        id: "float",
        name: "Float",
        summary: "Sticks appear wherever your thumbs land. Camera can be a floating stick or a swipe.")

    public var camera: Camera {
        didSet { if camera != oldValue, context.size != .zero { layout(context) } }
    }

    public let aScale: CGFloat

    public init(camera: Camera = .stick, aScale: CGFloat = 1) {
        self.camera = camera
        self.aScale = aScale
        super.init(info: Self.schemeInfo)
    }

    override public func render(pressed: Set<PadButton>, sticks: [PadStick: StickValue]) -> [RenderElement] {
        super.render(pressed: pressed, sticks: sticks).compactMap { e in
            switch e.role {
            case .stickBase:
                guard !e.ghost else { return nil }
                return RenderElement(shape: .circle(center: e.shape.center, radius: knobRadius * 0.3), role: .dot)
            case .stickKnob:
                return e.ghost ? nil : e
            default:
                return e
            }
        }
    }

    override public func makeControls(_ ctx: LayoutContext) -> [PadControl] {
        let s = ctx.safeBounds
        let u = min(ctx.unit, s.height / 8.2)
        let travel = PadParts.stickTravel * u

        let band = CGSize(width: min(2.3 * u, (s.width / 2 - 2.3 * u) / 2 - 0.3 * u), height: 1.15 * u)
        let top = s.minY + 0.2 * u
        let zl = CGRect(x: s.minX + 0.2 * u, y: top, width: band.width, height: band.height)
        let l = zl.offsetBy(dx: band.width + 0.25 * u, dy: 0)
        let zr = CGRect(x: s.maxX - 0.2 * u - band.width, y: top, width: band.width, height: band.height)
        let r = zr.offsetBy(dx: -(band.width + 0.25 * u), dy: 0)
        let belowBands = zl.maxY + 0.35 * u

        let dk: CGFloat = 0.72
        let dpadCentre = CGPoint(x: s.minX + (PadParts.clusterRadius * dk + 0.45) * u,
                                 y: belowBands + (PadParts.clusterRadius * dk + 0.35) * u)

        let fk: CGFloat = 0.92
        let aExtra = 0.5 * (PadParts.clampedAScale(aScale) - 1) * fk * u
        let faceCentre = CGPoint(x: s.maxX - (PadParts.clusterRadius * fk + 0.7) * u - aExtra,
                                 y: s.maxY - (PadParts.clusterRadius * fk + 0.7) * u)

        let zoneTop = belowBands
        let dpadBottom = dpadCentre.y + (PadParts.clusterRadius + 0.3) * dk * u
        let leftZone = CGRect(x: s.minX, y: dpadBottom + 0.2 * u,
                              width: s.width * 0.42, height: s.maxY - dpadBottom - 0.2 * u)
        let rightZone = CGRect(x: s.maxX - s.width * 0.42, y: zoneTop,
                               width: s.width * 0.42, height: s.maxY - zoneTop)

        let leftRest = CGPoint(x: s.minX + (PadParts.stickBaseDiameter / 2 + 0.8) * u,
                               y: max(s.maxY - (PadParts.stickBaseDiameter / 2 + 0.7) * u,
                                      leftZone.minY + PadParts.stickBaseDiameter / 2 * u))
        let rightRest = CGPoint(x: faceCentre.x - (PadParts.clusterRadius * fk + PadParts.stickBaseDiameter / 2 + 0.4) * u,
                                y: faceCentre.y - 1.4 * u)

        let cameraControl: PadControl
        switch camera {
        case .stick:
            cameraControl = PadControl(.floatingStick(.right, travel: travel, rest: rightRest, follow: false, click: .stickR),
                                       shape: .roundedRect(rightZone, cornerRadius: 0), role: .zone, label: "R",
                                       priority: 0)
        case .swipe:
            cameraControl = PadControl(.swipeStick(.right, fullSpeed: 900),
                                       shape: .roundedRect(rightZone, cornerRadius: 0), role: .zone, label: "camera",
                                       priority: 0)
        }

        let sys = s.minY + 0.2 * u + band.height / 2
        return [
            PadParts.shoulder(.zl, zl, u: u, group: PadParts.Group.leftShoulders),
            PadParts.shoulder(.l, l, u: u, group: PadParts.Group.leftShoulders),
            PadParts.shoulder(.zr, zr, u: u, group: PadParts.Group.rightShoulders),
            PadParts.shoulder(.r, r, u: u, group: PadParts.Group.rightShoulders),
            PadParts.system(.minus, at: CGPoint(x: s.midX - 1.3 * u, y: sys), u: u),
            PadParts.system(.home, at: CGPoint(x: s.midX, y: sys), u: u),
            PadParts.system(.plus, at: CGPoint(x: s.midX + 1.3 * u, y: sys), u: u),
            PadParts.dpad(dpadCentre, u: u, scale: dk, click: nil),
            PadControl(.floatingStick(.left, travel: travel, rest: leftRest, follow: false, click: .stickL),
                       shape: .roundedRect(leftZone, cornerRadius: 0), role: .zone, label: "L", priority: 0),
            cameraControl,
        ] + PadParts.faceDiamond(faceCentre, u: u, scale: fk, rDot: false, aScale: aScale)
    }
}
