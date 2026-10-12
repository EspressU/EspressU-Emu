import CoreGraphics

public struct SchemeInfo: Equatable, Sendable {
    public let id: String
    public let name: String
    public let summary: String
}

public protocol TouchScheme: AnyObject {
    var info: SchemeInfo { get }

    func layout(_ context: LayoutContext)

    func claims(_ point: CGPoint) -> Bool

    func began(_ touch: TouchID, at point: CGPoint, time: Double) -> Contribution?
    func moved(_ touch: TouchID, to point: CGPoint, time: Double) -> Contribution
    func ended(_ touch: TouchID, at point: CGPoint, time: Double)

    var needsTicks: Bool { get }
    func tick(time: Double) -> [TouchID: Contribution]

    func render(pressed: Set<PadButton>, sticks: [PadStick: StickValue]) -> [RenderElement]

    func ambient() -> Contribution?

    var wantsMotion: Bool { get }

    func motion(angle: Double) -> [TouchID: Contribution]
}

public extension TouchScheme {
    var needsTicks: Bool { false }
    func tick(time: Double) -> [TouchID: Contribution] { [:] }
    func ambient() -> Contribution? { nil }
    var wantsMotion: Bool { false }
    func motion(angle: Double) -> [TouchID: Contribution] { [:] }
}

public struct RenderElement: Equatable, Sendable {
    public enum Role: Equatable, Sendable {
        case face
        case dpad
        case shoulder
        case system
        case dot
        case stickBase
        case stickKnob
        case zone
        case area
        case pedal
        case touchscreen
    }

    public var shape: PadShape
    public var role: Role
    public var label: String
    public var lit: Bool
    public var ghost: Bool

    public init(shape: PadShape, role: Role, label: String = "", lit: Bool = false, ghost: Bool = false) {
        self.shape = shape
        self.role = role
        self.label = label
        self.lit = lit
        self.ghost = ghost
    }
}
