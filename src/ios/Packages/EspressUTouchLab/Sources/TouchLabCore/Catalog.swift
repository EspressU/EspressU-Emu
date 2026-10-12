import CoreGraphics

public enum SchemeCatalog {
    public static let all: [SchemeInfo] = [ZonePad.schemeInfo, FloatPad.schemeInfo,
                                           AdaptivePad.schemeInfo, FramePad.schemeInfo,
                                           RacingPad.schemeInfo]

    public static func make(_ id: String) -> TouchScheme { make(id, aScale: 1) }

    public static func make(_ id: String, aScale: CGFloat) -> TouchScheme {
        switch id {
        case FloatPad.schemeInfo.id: return FloatPad(aScale: aScale)
        case AdaptivePad.schemeInfo.id: return AdaptivePad(aScale: aScale)
        case FramePad.schemeInfo.id: return FramePad(aScale: aScale)
        case RacingPad.schemeInfo.id: return RacingPad(aScale: aScale)
        default: return ZonePad(aScale: aScale)
        }
    }
}

public struct TargetDevice: Sendable {
    public let name: String
    public let size: CGSize
    public let insets: Insets

    public init(name: String, size: CGSize, insets: Insets) {
        self.name = name
        self.size = size
        self.insets = insets
    }

    public static let all: [TargetDevice] = [
        TargetDevice(name: "iPhone SE 1st gen", size: CGSize(width: 568, height: 320), insets: .zero),
        TargetDevice(name: "iPhone SE", size: CGSize(width: 667, height: 375), insets: .zero),
        TargetDevice(name: "iPhone 16", size: CGSize(width: 852, height: 393),
                     insets: Insets(left: 59, bottom: 21, right: 59)),
        TargetDevice(name: "iPhone 16 Pro Max", size: CGSize(width: 956, height: 440),
                     insets: Insets(left: 62, bottom: 21, right: 62)),
        TargetDevice(name: "iPhone 8 Plus", size: CGSize(width: 736, height: 414), insets: .zero),
        TargetDevice(name: "iPad 9th gen", size: CGSize(width: 1080, height: 810), insets: .zero),
        TargetDevice(name: "iPad mini", size: CGSize(width: 1133, height: 744),
                     insets: Insets(top: 24, bottom: 20)),
        TargetDevice(name: "iPad Air 13", size: CGSize(width: 1366, height: 1024),
                     insets: Insets(top: 24, bottom: 20)),
        TargetDevice(name: "iPad Pro 11 (A12Z)", size: CGSize(width: 1194, height: 834),
                     insets: Insets(top: 24, bottom: 20)),
        TargetDevice(name: "iPad Pro 13", size: CGSize(width: 1376, height: 1032),
                     insets: Insets(top: 24, bottom: 20)),
        TargetDevice(name: "iPad Pro 11 portrait", size: CGSize(width: 834, height: 1194),
                     insets: Insets(top: 24, bottom: 20)),
    ]

    public enum Display: String, CaseIterable, Sendable {
        case stacked
        case single
    }

    public func video(_ display: Display) -> (rects: [CGRect], gamepad: CGRect) {
        let safe = CGRect(x: insets.left, y: insets.top,
                          width: size.width - insets.left - insets.right,
                          height: size.height - insets.top - insets.bottom)
        let portrait = size.height > size.width
        switch display {
        case .single:
            var w = safe.width, h = w * 9 / 16
            if h > safe.height { h = safe.height; w = h * 16 / 9 }
            let r = CGRect(x: safe.midX - w / 2, y: portrait ? safe.minY : safe.midY - h / 2, width: w, height: h)
            return ([r], r)
        case .stacked:
            var h = safe.height / 2, w = h * 16 / 9
            if w > safe.width { w = safe.width; h = w * 9 / 16 }
            let x = safe.midX - w / 2
            let y0 = portrait ? safe.minY : safe.midY - h
            let tv = CGRect(x: x, y: y0, width: w, height: h)
            let pad = CGRect(x: x, y: y0 + h, width: w, height: h)
            return ([tv, pad], pad)
        }
    }

    public func context(_ display: Display) -> LayoutContext {
        let v = video(display)
        return LayoutContext(size: size, safeInsets: insets, videoRects: v.rects, touchscreenRect: v.gamepad)
    }
}
