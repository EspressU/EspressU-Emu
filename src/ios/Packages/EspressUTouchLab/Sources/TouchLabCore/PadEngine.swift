import CoreGraphics
import Foundation

public final class PadEngine {
    public private(set) var scheme: TouchScheme
    public let mixer: PadMixer
    public private(set) var context: LayoutContext

    private enum Owner { case scheme, touchscreen }
    private var owners: [TouchID: Owner] = [:]

    public static let ambientID: TouchID = Int.min

    public var ambientEnabled = true {
        didSet { if ambientEnabled != oldValue { syncAmbient() } }
    }

    public var minimumLitDuration: Double = 0.09
    public var clock: () -> Double = { ProcessInfo.processInfo.systemUptime }
    private var litSince: [PadButton: Double] = [:]
    private var litSeen: Set<PadButton> = []

    public init(scheme: TouchScheme, output: PadOutput, context: LayoutContext) {
        self.scheme = scheme
        self.mixer = PadMixer(output: output)
        self.context = context
        scheme.layout(context)
        syncAmbient()
    }

    public func setScheme(_ next: TouchScheme) {
        cancelAll()
        scheme = next
        next.layout(context)
        syncAmbient()
    }

    public func setContext(_ next: LayoutContext) {
        guard next != context else { return }
        if !owners.isEmpty { cancelAll() }
        context = next
        scheme.layout(next)
        syncAmbient()
    }

    public func claims(_ point: CGPoint) -> Bool {
        scheme.claims(point) || (context.touchscreenRect?.contains(point) ?? false)
    }

    public func began(_ touch: TouchID, at point: CGPoint, time: Double) {
        if let c = scheme.began(touch, at: point, time: time) {
            owners[touch] = .scheme
            mixer.update(touch, c)
        } else if let rect = context.touchscreenRect, rect.contains(point) {
            owners[touch] = .touchscreen
            mixer.update(touch, Contribution(touchscreen: normalise(point, in: rect)))
        }
        settled()
    }

    public func moved(_ touch: TouchID, to point: CGPoint, time: Double) {
        switch owners[touch] {
        case .scheme?:
            mixer.update(touch, scheme.moved(touch, to: point, time: time))
        case .touchscreen?:
            if let rect = context.touchscreenRect {
                mixer.update(touch, Contribution(touchscreen: normalise(point, in: rect)))
            }
        case nil:
            break
        }
        settled()
    }

    public func ended(_ touch: TouchID, at point: CGPoint, time: Double) {
        guard let owner = owners.removeValue(forKey: touch) else { return }
        if owner == .scheme { scheme.ended(touch, at: point, time: time) }
        mixer.remove(touch)
        settled()
    }

    public func cancelled(_ touch: TouchID, time: Double) {
        guard let owner = owners.removeValue(forKey: touch) else { return }
        if owner == .scheme { scheme.ended(touch, at: CGPoint(x: CGFloat.nan, y: .nan), time: time) }
        mixer.remove(touch)
        settled()
    }

    public func cancelAll() {
        for (touch, owner) in owners where owner == .scheme {
            scheme.ended(touch, at: CGPoint(x: CGFloat.nan, y: .nan), time: 0)
        }
        owners.removeAll()
        mixer.reset()
        settled()
    }

    public var needsTicks: Bool { scheme.needsTicks }

    public func tick(time: Double) {
        for (touch, c) in scheme.tick(time: time) where owners[touch] == .scheme {
            mixer.update(touch, c)
        }
        settled()
    }

    public func motion(angle: Double) {
        for (touch, c) in scheme.motion(angle: angle) where owners[touch] == .scheme {
            mixer.update(touch, c)
        }
        settled()
    }

    public func syncAmbient() {
        let wanted = ambientEnabled ? scheme.ambient() : nil
        if let wanted {
            if mixer.contribution(for: Self.ambientID) != wanted { mixer.update(Self.ambientID, wanted) }
        } else if mixer.contribution(for: Self.ambientID) != nil {
            mixer.remove(Self.ambientID)
        }
        noteLit()
    }

    private func settled() { syncAmbient() }

    private func noteLit() {
        let now = mixer.pressed
        if now != litSeen {
            let at = clock()
            for b in now.subtracting(litSeen) { litSince[b] = at }
            litSeen = now
        }
    }

    public func litButtons() -> Set<PadButton> {
        var lit = mixer.pressed
        guard !litSince.isEmpty else { return lit }
        let t = clock()
        for (b, since) in litSince {
            if t - since < minimumLitDuration { lit.insert(b) } else if !mixer.pressed.contains(b) { litSince[b] = nil }
        }
        return lit
    }

    public func nextLitExpiry() -> Double? {
        let t = clock()
        return litSince.filter { !mixer.pressed.contains($0.key) && t - $0.value < minimumLitDuration }
            .map { $0.value + minimumLitDuration }.min()
    }

    public func render() -> [RenderElement] {
        var out = scheme.render(pressed: litButtons(), sticks: mixer.sticks)
        if let rect = context.touchscreenRect {
            out.insert(RenderElement(shape: .roundedRect(rect, cornerRadius: 6), role: .touchscreen,
                                     lit: mixer.touchscreen != nil, ghost: true), at: 0)
        }
        return out
    }

    private func normalise(_ p: CGPoint, in rect: CGRect) -> CGPoint {
        let q = p.clamped(to: rect)
        return CGPoint(x: (q.x - rect.minX) / max(rect.width, 1), y: (q.y - rect.minY) / max(rect.height, 1))
    }
}
