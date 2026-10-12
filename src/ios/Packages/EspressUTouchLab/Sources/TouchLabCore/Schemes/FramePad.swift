import CoreGraphics

public final class FramePad: ControlScheme {
    public static let schemeInfo = SchemeInfo(
        id: "frame",
        name: "Frame",
        summary: "Controls fill the space around the game and never cover it. The GamePad screen stays touchable.")

    public enum Mode: Equatable, Sendable { case columns, band, overlay }

    public var minimumUnit: CGFloat = 40
    public private(set) var mode: Mode = .overlay

    public let aScale: CGFloat

    public init(aScale: CGFloat = 1) {
        self.aScale = aScale
        super.init(info: Self.schemeInfo)
    }

    override public func makeControls(_ ctx: LayoutContext) -> [PadControl] {
        let s = ctx.safeBounds
        let maxU = ctx.unit
        let video = ctx.videoRects.reduce(CGRect.null) { $0.union($1) }

        guard !video.isNull else {
            mode = .overlay
            return GamePadArrangement.build(ctx, aScale: aScale)
        }

        let leftCol = CGRect(x: s.minX, y: s.minY, width: max(video.minX - s.minX, 0), height: s.height)
        let rightCol = CGRect(x: video.maxX, y: s.minY, width: max(s.maxX - video.maxX, 0), height: s.height)
        let band = CGRect(x: s.minX, y: video.maxY, width: s.width, height: max(s.maxY - video.maxY, 0))

        let columnUnits = CGSize(width: 4.3, height: 10.45)
        let colU = min(min(leftCol.width, rightCol.width) / columnUnits.width, s.height / columnUnits.height, maxU)
        let bandUnits = CGSize(width: 8.2, height: 5.3)
        let bandU = min(band.width / 2 / bandUnits.width, band.height / bandUnits.height, maxU)

        if colU >= bandU, colU >= minimumUnit {
            mode = .columns
            return column(leftCol, side: .left, u: colU) + column(rightCol, side: .right, u: colU)
        }
        if bandU >= minimumUnit {
            mode = .band
            let left = CGRect(x: band.minX, y: band.minY, width: band.width / 2, height: band.height)
            let right = CGRect(x: band.midX, y: band.minY, width: band.width / 2, height: band.height)
            return bandHalf(left, side: .left, u: bandU) + bandHalf(right, side: .right, u: bandU)
                + [home(CGPoint(x: band.midX, y: band.maxY - 0.55 * bandU), u: bandU, bottomLimit: s.maxY)]
        }
        mode = .overlay
        return GamePadArrangement.build(ctx, aScale: aScale)
    }

    private enum Side { case left, right }

    private func column(_ col: CGRect, side: Side, u: CGFloat) -> [PadControl] {
        let cx = col.midX
        let needed = 10.45 * u
        let spare = max(col.height - needed, 0) / 5
        var y = col.minY + 0.25 * u + spare

        let sh = CGSize(width: min(1.7 * u, (col.width - 0.8 * u) / 2), height: 1.0 * u)
        let outer = CGRect(x: side == .left ? cx - sh.width - 0.15 * u : cx + 0.15 * u, y: y, width: sh.width, height: sh.height)
        let inner = CGRect(x: side == .left ? cx + 0.15 * u : cx - sh.width - 0.15 * u, y: y, width: sh.width, height: sh.height)
        y += 1.0 * u + 0.35 * u + spare

        let stickCentre = CGPoint(x: cx, y: y + PadParts.stickBaseDiameter / 2 * u)
        y += PadParts.stickBaseDiameter * u + 0.45 * u + spare
        let diamond = CGPoint(x: cx, y: y + PadParts.stickBaseDiameter / 2 * u)
        y += PadParts.stickBaseDiameter * u + 0.35 * u + spare
        let systemCentre = CGPoint(x: cx, y: min(y + 0.4 * u, col.maxY - 0.45 * u))

        let a = PadParts.clampedAScale(aScale), nb = PadParts.neighbourScale(forA: a)
        let wall = (col.width / 2 - (PadParts.crossX + 0.25 * (a + nb)) * u) / u
        let reach = max(0.3 / a, min(wall / a, 0.8))
        let faceAt = diamond - CGPoint(x: 0.25 * (a - nb) * u, y: 0)
        var out: [PadControl]
        if side == .left {
            out = [
                PadParts.shoulder(.zl, outer, u: u, group: PadParts.Group.leftShoulders),
                PadParts.shoulder(.l, inner, u: u, group: PadParts.Group.leftShoulders),
                PadParts.stick(.left, at: stickCentre, u: u, click: .stickL),
                PadParts.dpad(diamond, u: u, click: nil),
                PadParts.system(.minus, at: systemCentre, u: u),
            ]
        } else {
            out = [
                PadParts.shoulder(.zr, outer, u: u, group: PadParts.Group.rightShoulders),
                PadParts.shoulder(.r, inner, u: u, group: PadParts.Group.rightShoulders),
                PadParts.stick(.right, at: stickCentre, u: u, click: .stickR),
                PadParts.system(.plus, at: systemCentre + CGPoint(x: -0.6 * u, y: 0), u: u),
                PadParts.system(.home, at: systemCentre + CGPoint(x: 0.6 * u, y: 0), u: u),
            ] + PadParts.faceDiamond(faceAt, u: u, reach: reach, rDot: false, aScale: a)
        }
        return out
    }

    private func bandHalf(_ half: CGRect, side: Side, u: CGFloat) -> [PadControl] {
        let spareY = max(half.height - 5.3 * u, 0) / 3
        let top = half.minY + 0.2 * u + spareY
        let sh = CGSize(width: 1.9 * u, height: 1.0 * u)
        let rowY = top + 1.0 * u + 0.4 * u + spareY + PadParts.stickBaseDiameter / 2 * u
        let spareX = max(half.width - 8.2 * u, 0) / 3
        let outerX = side == .left ? half.minX + 0.35 * u + spareX : half.maxX - 0.35 * u - spareX
        let dir: CGFloat = side == .left ? 1 : -1
        let outerCentre = CGPoint(x: outerX + dir * PadParts.stickBaseDiameter / 2 * u, y: rowY)
        let innerCentre = CGPoint(x: outerCentre.x + dir * (PadParts.stickBaseDiameter + 0.55) * u + dir * spareX, y: rowY)

        let zOuter = CGRect(x: side == .left ? half.minX + 0.35 * u : half.maxX - 0.35 * u - sh.width,
                            y: top, width: sh.width, height: sh.height)
        let zInner = zOuter.offsetBy(dx: dir * (sh.width + 0.3 * u), dy: 0)
        let sysCentre = CGPoint(x: zInner.midX + dir * (sh.width / 2 + 0.9 * u), y: zInner.midY)

        if side == .left {
            return [
                PadParts.shoulder(.zl, zOuter, u: u, group: PadParts.Group.leftShoulders),
                PadParts.shoulder(.l, zInner, u: u, group: PadParts.Group.leftShoulders),
                PadParts.system(.minus, at: sysCentre, u: u),
                PadParts.stick(.left, at: outerCentre, u: u, click: .stickL),
                PadParts.dpad(innerCentre, u: u, click: nil),
            ]
        } else {
            return [
                PadParts.shoulder(.zr, zOuter, u: u, group: PadParts.Group.rightShoulders),
                PadParts.shoulder(.r, zInner, u: u, group: PadParts.Group.rightShoulders),
                PadParts.system(.plus, at: sysCentre, u: u),
                PadParts.stick(.right, at: innerCentre, u: u, click: .stickR),
            ] + PadParts.faceDiamond(outerCentre - CGPoint(x: 0.5 * (PadParts.clampedAScale(aScale) - 1) * u, y: 0), u: u, rDot: false, aScale: aScale)
        }
    }

    private func home(_ c: CGPoint, u: CGFloat, bottomLimit: CGFloat) -> PadControl {
        PadParts.system(.home, at: CGPoint(x: c.x, y: min(c.y, bottomLimit - PadParts.systemDiameter / 2 * u)), u: u)
    }
}
