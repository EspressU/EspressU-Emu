#if canImport(UIKit)
import SwiftUI
import TouchLabCore

public enum TouchLabScreen: Hashable, Sendable {
    case tv, gamepad
}

public struct TouchLabScreenFramesKey: PreferenceKey {
    public static var defaultValue: [TouchLabScreen: CGRect] = [:]
    public static func reduce(value: inout [TouchLabScreen: CGRect], nextValue: () -> [TouchLabScreen: CGRect]) {
        value.merge(nextValue()) { _, new in new }
    }
}

public extension View {
    func touchLabScreenFrame(_ screen: TouchLabScreen) -> some View {
        background(
            GeometryReader { proxy in
                Color.clear.preference(key: TouchLabScreenFramesKey.self, value: [screen: proxy.frame(in: .global)])
            }
        )
    }
}

public struct TouchLabScreens: Equatable {
    public var frames: [TouchLabScreen: CGRect]
    public var imageIsAspectFit: Bool

    public init(frames: [TouchLabScreen: CGRect], imageIsAspectFit: Bool = true) {
        self.frames = frames
        self.imageIsAspectFit = imageIsAspectFit
    }

    public var videoRects: [CGRect] {
        [frames[.tv], frames[.gamepad]].compactMap { $0 }.filter { $0.width > 1 && $0.height > 1 }.map {
            imageIsAspectFit ? PadScreenGeometry.aspectFit(16.0 / 9.0, in: $0) : $0
        }
    }

    public var touchscreenRect: CGRect? {
        frames[.gamepad].flatMap { $0.width > 1 && $0.height > 1 ? $0 : nil }
    }
}
#endif
