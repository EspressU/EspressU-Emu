import CoreGraphics
import Foundation

public struct StickTuning: Equatable, Sendable {
    public enum Gate: String, CaseIterable, Sendable {
        case octagon, round
    }

    public var deadzone: Double
    public var curve: Double
    public var gate: Gate

    public init(deadzone: Double = 0.06, curve: Double = 1.0, gate: Gate = .octagon) {
        self.deadzone = deadzone
        self.curve = curve
        self.gate = gate
    }
}

public enum StickMath {
    public static func gateFraction(_ gate: StickTuning.Gate, angle: CGFloat) -> CGFloat {
        switch gate {
        case .round:
            return 1
        case .octagon:
            let wedge = CGFloat.pi / 4
            var offset = angle.truncatingRemainder(dividingBy: wedge)
            if offset < 0 { offset += wedge }
            return cos(wedge / 2) / cos(offset - wedge / 2)
        }
    }

    public static func value(offset: CGPoint, travel: CGFloat, tuning: StickTuning) -> StickValue {
        guard travel > 0 else { return .zero }
        let raw = offset.length / travel
        guard raw > tuning.deadzone else { return .zero }
        let angle = offset.screenAngle
        let limit = gateFraction(tuning.gate, angle: angle)
        let live = min((Double(raw) - tuning.deadzone) / max(1 - tuning.deadzone, 0.0001), 1)
        let shaped = min(pow(live, tuning.curve), Double(limit))
        return StickValue(x: shaped * Double(cos(angle)), y: shaped * Double(sin(angle)))
    }

    public static func knobOffset(offset: CGPoint, travel: CGFloat, gate: StickTuning.Gate) -> CGPoint {
        let len = offset.length
        guard len > 0 else { return .zero }
        let maxLen = travel * gateFraction(gate, angle: offset.screenAngle)
        return len <= maxLen ? offset : offset * (maxLen / len)
    }
}

public enum DPadMath {
    public static let cardinalHalfWidth: CGFloat = 27.5 * .pi / 180

    public static func directions(angle: CGFloat, cardinalHalfWidth: CGFloat = cardinalHalfWidth) -> Set<PadButton> {
        let quarter = CGFloat.pi / 2
        let k = (angle / quarter).rounded()
        let off = abs(angle - k * quarter)
        let cardinal: PadButton
        switch (Int(k) % 4 + 4) % 4 {
        case 0: cardinal = .right
        case 1: cardinal = .up
        case 2: cardinal = .left
        default: cardinal = .down
        }
        if off <= cardinalHalfWidth { return [cardinal] }
        let side: CGFloat = angle > k * quarter ? 1 : -1
        let neighbourIndex = ((Int(k) + Int(side)) % 4 + 4) % 4
        let neighbour: PadButton = [.right, .up, .left, .down][neighbourIndex]
        return [cardinal, neighbour]
    }
}
