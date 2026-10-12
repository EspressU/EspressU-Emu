import CoreGraphics

public typealias TouchID = Int

public struct Contribution: Equatable, Sendable {
    public var buttons: Set<PadButton>
    public var stick: PadStick?
    public var stickValue: StickValue
    public var touchscreen: CGPoint?

    public init(buttons: Set<PadButton> = [],
                stick: PadStick? = nil,
                stickValue: StickValue = .zero,
                touchscreen: CGPoint? = nil) {
        self.buttons = buttons
        self.stick = stick
        self.stickValue = stickValue
        self.touchscreen = touchscreen
    }

    public static let none = Contribution()

    public static func press(_ buttons: PadButton...) -> Contribution {
        Contribution(buttons: Set(buttons))
    }
}

public final class PadMixer {
    public let output: PadOutput

    public private(set) var pressed: Set<PadButton> = []
    public private(set) var sticks: [PadStick: StickValue] = [:]
    public private(set) var touchscreen: CGPoint?

    private var contributions: [TouchID: Contribution] = [:]
    private var stickOwners: [PadStick: [TouchID]] = [:]
    private var touchscreenOwner: TouchID?

    public init(output: PadOutput) {
        self.output = output
    }

    public var liveTouches: Int { contributions.count }

    public func contribution(for touch: TouchID) -> Contribution? {
        contributions[touch]
    }

    public func update(_ touch: TouchID, _ contribution: Contribution) {
        let previous = contributions[touch]
        contributions[touch] = contribution

        if let stick = contribution.stick, previous?.stick != stick {
            if let old = previous?.stick { stickOwners[old]?.removeAll { $0 == touch } }
            stickOwners[stick, default: []].removeAll { $0 == touch }
            stickOwners[stick, default: []].append(touch)
        } else if contribution.stick == nil, let old = previous?.stick {
            stickOwners[old]?.removeAll { $0 == touch }
        }

        if contribution.touchscreen != nil, touchscreenOwner == nil {
            touchscreenOwner = touch
        }
        flush()
    }

    public func remove(_ touch: TouchID) {
        guard let previous = contributions.removeValue(forKey: touch) else { return }
        if let stick = previous.stick { stickOwners[stick]?.removeAll { $0 == touch } }
        if touchscreenOwner == touch { touchscreenOwner = nil }
        flush()
    }

    public func reset() {
        contributions.removeAll()
        stickOwners.removeAll()
        touchscreenOwner = nil
        let hadAnything = !pressed.isEmpty || sticks.values.contains { $0 != .zero } || touchscreen != nil
        pressed = []
        sticks = [:]
        touchscreen = nil
        if hadAnything { output.releaseAll() }
        output.setTouchscreen(nil)
    }

    private func flush() {
        var nextPressed = Set<PadButton>()
        for c in contributions.values { nextPressed.formUnion(c.buttons) }

        for b in PadButton.allCases {
            let was = pressed.contains(b), now = nextPressed.contains(b)
            if was != now { output.setButton(b, pressed: now) }
        }
        pressed = nextPressed

        for stick in PadStick.allCases {
            let owner = stickOwners[stick]?.last
            let value = owner.flatMap { contributions[$0]?.stickValue } ?? .zero
            if (sticks[stick] ?? .zero) != value {
                sticks[stick] = value
                output.setStick(stick, value)
            }
        }

        let screen = touchscreenOwner.flatMap { contributions[$0]?.touchscreen }
        if screen != touchscreen {
            touchscreen = screen
            output.setTouchscreen(screen)
        }
    }
}
