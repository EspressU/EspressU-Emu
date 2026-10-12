import CoreGraphics

public final class ZonePad: ControlScheme {
    public static let schemeInfo = SchemeInfo(
        id: "zone",
        name: "Zone",
        summary: "The GamePad's own layout with forgiving, gap-free touch zones, slide between buttons and two-button presses.")

    public let aScale: CGFloat

    public init(aScale: CGFloat = 1) {
        self.aScale = aScale
        super.init(info: Self.schemeInfo)
    }

    override public func makeControls(_ context: LayoutContext) -> [PadControl] {
        GamePadArrangement.build(context, aScale: aScale)
    }
}
