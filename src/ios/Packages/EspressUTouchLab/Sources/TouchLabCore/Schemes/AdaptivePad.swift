import CoreGraphics
import Foundation

public final class AdaptivePad: ControlScheme {
    public static let schemeInfo = SchemeInfo(
        id: "adaptive",
        name: "Adaptive",
        summary: "The GamePad layout, but each group of buttons slowly moves to where your thumbs actually press.")

    public var learningRate: CGFloat = 0.10
    public var maxDrift: CGFloat = 1.2
    public private(set) var learned: [Int: CGPoint] = [:]
    public var onLearned: (([Int: CGPoint]) -> Void)?

    private var pending: [Int: CGPoint] = [:]
    private var lastUnit: CGFloat = 1

    public let aScale: CGFloat

    public init(learned: [Int: CGPoint] = [:], aScale: CGFloat = 1) {
        self.learned = learned
        self.aScale = aScale
        super.init(info: Self.schemeInfo)
    }

    public static func encode(_ learned: [Int: CGPoint]) -> String {
        let plain = Dictionary(uniqueKeysWithValues: learned.map { (String($0.key), [Double($0.value.x), Double($0.value.y)]) })
        guard let data = try? JSONSerialization.data(withJSONObject: plain, options: [.sortedKeys]) else { return "{}" }
        return String(decoding: data, as: UTF8.self)
    }

    public static func decode(_ json: String) -> [Int: CGPoint] {
        guard let obj = try? JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: [Double]] else { return [:] }
        var out: [Int: CGPoint] = [:]
        for (k, v) in obj where v.count == 2 {
            if let key = Int(k), v.allSatisfy({ $0.isFinite }) { out[key] = CGPoint(x: v[0], y: v[1]) }
        }
        return out
    }

    public func resetLearning() {
        learned = [:]
        pending = [:]
        clusterOffsets = [:]
        onLearned?(learned)
    }

    override public func makeControls(_ context: LayoutContext) -> [PadControl] {
        let controls = GamePadArrangement.build(context, aScale: aScale)
        lastUnit = controls.first { $0.role == .face }
            .map { $0.shape.boundingBox.width / PadParts.neighbourScale(forA: aScale) } ?? context.unit
        return controls
    }

    override public func didEnd(control index: Int, start: CGPoint, end: CGPoint, duration: Double) {
        let c = controls[index]
        guard c.cluster >= 0 else { return }
        let target: CGPoint
        switch c.kind {
        case .button:
            guard duration < 0.45, start.distance(to: end) < 0.35 * lastUnit else { return }
            if c.role == .dot { return }
            target = c.shape.center
        case .stick:
            target = c.shape.center
        default:
            return
        }
        let error = start - target
        let step = error * (learningRate / lastUnit)
        pending[c.cluster, default: .zero] = pending[c.cluster, default: .zero] + step
    }

    override public func didBecomeIdle() {
        applyPendingIfIdle()
    }

    @discardableResult
    public func applyPendingIfIdle() -> Bool {
        guard tracks.isEmpty, !pending.isEmpty else { return false }
        var moved = false
        for (cluster, step) in pending {
            var next = (learned[cluster] ?? .zero) + step
            let len = next.length
            if len > maxDrift { next = next * (maxDrift / len) }
            var trial = learned
            trial[cluster] = next
            if LayoutCheck.problems(shifted(by: trial), in: context.safeBounds).isEmpty {
                learned = trial
                moved = true
            }
        }
        pending = [:]
        if moved {
            clusterOffsets = learned.mapValues { $0 * lastUnit }
            onLearned?(learned)
        }
        return moved
    }

    override public func layout(_ context: LayoutContext) {
        super.layout(context)
        clusterOffsets = learned.mapValues { $0 * lastUnit }
    }

    private func shifted(by offsets: [Int: CGPoint]) -> [PadControl] {
        baseControls.map { c in
            guard c.cluster >= 0, let d = offsets[c.cluster] else { return c }
            var m = c
            m.shape = c.shape.offset(by: d * lastUnit)
            return m
        }
    }
}
