import CoreGraphics

public enum PadButton: Int, CaseIterable, Hashable, Sendable, CustomStringConvertible {
    case a = 1, b, x, y
    case l, r, zl, zr
    case plus, minus
    case up, down, left, right
    case stickL, stickR
    case home

    public var description: String {
        switch self {
        case .a: return "A"
        case .b: return "B"
        case .x: return "X"
        case .y: return "Y"
        case .l: return "L"
        case .r: return "R"
        case .zl: return "ZL"
        case .zr: return "ZR"
        case .plus: return "+"
        case .minus: return "\u{2212}"
        case .up: return "\u{25B2}"
        case .down: return "\u{25BC}"
        case .left: return "\u{25C0}"
        case .right: return "\u{25B6}"
        case .stickL: return "L3"
        case .stickR: return "R3"
        case .home: return "\u{2302}"
        }
    }
}

public enum PadStick: Int, CaseIterable, Hashable, Sendable {
    case left = 0
    case right = 1
}

public struct StickValue: Equatable, Sendable {
    public var x: Double
    public var y: Double

    public init(x: Double, y: Double) {
        self.x = x
        self.y = y
    }

    public static let zero = StickValue(x: 0, y: 0)

    public var magnitude: Double { (x * x + y * y).squareRoot() }
}

public protocol PadOutput: AnyObject {
    func setButton(_ button: PadButton, pressed: Bool)
    func setStick(_ stick: PadStick, _ value: StickValue)
    func setTouchscreen(_ point: CGPoint?)
    func releaseAll()
}
