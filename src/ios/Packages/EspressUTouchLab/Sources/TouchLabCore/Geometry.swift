import CoreGraphics
import Foundation

extension CGPoint {
    public static func + (a: CGPoint, b: CGPoint) -> CGPoint { CGPoint(x: a.x + b.x, y: a.y + b.y) }
    public static func - (a: CGPoint, b: CGPoint) -> CGPoint { CGPoint(x: a.x - b.x, y: a.y - b.y) }
    public static func * (a: CGPoint, k: CGFloat) -> CGPoint { CGPoint(x: a.x * k, y: a.y * k) }

    public var length: CGFloat { (x * x + y * y).squareRoot() }

    public func distance(to other: CGPoint) -> CGFloat { (self - other).length }

    public var screenAngle: CGFloat { atan2(-y, x) }

    public func clamped(to rect: CGRect) -> CGPoint {
        CGPoint(x: min(max(x, rect.minX), rect.maxX), y: min(max(y, rect.minY), rect.maxY))
    }
}

extension CGRect {
    public var center: CGPoint { CGPoint(x: midX, y: midY) }

    public init(center: CGPoint, size: CGSize) {
        self.init(x: center.x - size.width / 2, y: center.y - size.height / 2,
                  width: size.width, height: size.height)
    }
}

public enum PadShape: Equatable, Sendable {
    case circle(center: CGPoint, radius: CGFloat)
    case roundedRect(CGRect, cornerRadius: CGFloat)

    public var center: CGPoint {
        switch self {
        case .circle(let c, _): return c
        case .roundedRect(let r, _): return r.center
        }
    }

    public var boundingBox: CGRect {
        switch self {
        case .circle(let c, let r): return CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)
        case .roundedRect(let rect, _): return rect
        }
    }

    public func edgeDistance(to p: CGPoint) -> CGFloat {
        switch self {
        case .circle(let c, let r):
            return p.distance(to: c) - r
        case .roundedRect(let rect, let cr):
            let r = min(cr, min(rect.width, rect.height) / 2)
            let inner = rect.insetBy(dx: r, dy: r)
            let dx = max(inner.minX - p.x, 0, p.x - inner.maxX)
            let dy = max(inner.minY - p.y, 0, p.y - inner.maxY)
            if dx == 0 && dy == 0 {
                let toEdge = min(p.x - rect.minX, rect.maxX - p.x, p.y - rect.minY, rect.maxY - p.y)
                return -toEdge
            }
            return (dx * dx + dy * dy).squareRoot() - r
        }
    }

    public func contains(_ p: CGPoint) -> Bool { edgeDistance(to: p) <= 0 }

    public func offset(by d: CGPoint) -> PadShape {
        switch self {
        case .circle(let c, let r): return .circle(center: c + d, radius: r)
        case .roundedRect(let rect, let cr): return .roundedRect(rect.offsetBy(dx: d.x, dy: d.y), cornerRadius: cr)
        }
    }

    public func overlaps(_ other: PadShape, margin: CGFloat = 0) -> Bool {
        switch (self, other) {
        case let (.circle(c1, r1), .circle(c2, r2)):
            return c1.distance(to: c2) < r1 + r2 - margin
        default:
            if case let .circle(c, r) = self { return other.edgeDistance(to: c) < r - margin }
            if case let .circle(c, r) = other { return self.edgeDistance(to: c) < r - margin }
            return boundingBox.insetBy(dx: margin / 2, dy: margin / 2)
                .intersects(other.boundingBox.insetBy(dx: margin / 2, dy: margin / 2))
        }
    }
}

public struct LayoutContext: Equatable, Sendable {
    public var size: CGSize
    public var safeInsets: Insets
    public var videoRects: [CGRect]
    public var touchscreenRect: CGRect?
    public var scale: CGFloat
    public var stick: StickTuning
    public var stickSpacing: CGFloat
    public var shoulderOffset: CGFloat

    public init(size: CGSize,
                safeInsets: Insets = .zero,
                videoRects: [CGRect] = [],
                touchscreenRect: CGRect? = nil,
                scale: CGFloat = 1,
                stick: StickTuning = StickTuning(),
                stickSpacing: CGFloat = 0,
                shoulderOffset: CGFloat = 0) {
        self.size = size
        self.safeInsets = safeInsets
        self.videoRects = videoRects
        self.touchscreenRect = touchscreenRect
        self.scale = scale
        self.stick = stick
        self.stickSpacing = stickSpacing
        self.shoulderOffset = shoulderOffset
    }

    public var bounds: CGRect { CGRect(origin: .zero, size: size) }

    public var safeBounds: CGRect {
        CGRect(x: safeInsets.left, y: safeInsets.top,
               width: size.width - safeInsets.left - safeInsets.right,
               height: size.height - safeInsets.top - safeInsets.bottom)
    }

    public var unit: CGFloat {
        let shortSide = min(size.width, size.height)
        return min(max(shortSide * 0.115, 46), 72) * scale
    }
}

public struct Insets: Equatable, Sendable {
    public var top, left, bottom, right: CGFloat
    public init(top: CGFloat = 0, left: CGFloat = 0, bottom: CGFloat = 0, right: CGFloat = 0) {
        self.top = top; self.left = left; self.bottom = bottom; self.right = right
    }
    public static let zero = Insets()
}

public enum PadScreenGeometry {
    public static func aspectFit(_ aspect: CGFloat, in rect: CGRect) -> CGRect {
        guard rect.width > 0, rect.height > 0, aspect > 0 else { return rect }
        var w = rect.width, h = w / aspect
        if h > rect.height { h = rect.height; w = h * aspect }
        return CGRect(x: rect.midX - w / 2, y: rect.midY - h / 2, width: w, height: h)
    }
}
