// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.

import UIKit

/// The one view that draws Crema and reads every finger on it.
///
/// It only accepts touches that land on a control or on the GamePad screen; everything else falls through
/// to the views underneath. Each finger is tracked on its own, so sticks, buttons and the touchscreen work
/// together, and a finger that slides from one button to the next presses the next one.
@MainActor
final class CremaSurfaceView: UIView {
    var settings = CremaSettings() { didSet { if oldValue != settings { setNeedsLayout() } } }
    var topInset: CGFloat = 0 { didSet { if oldValue != topInset { setNeedsLayout() } } }
    var padOpacity: CGFloat = 0.85 { didSet { content.opacity = Float(padOpacity) } }
    var hapticsEnabled = true
    /// The GamePad screen in window coordinates, if one is on screen.
    var touchscreenFrame: CGRect?
    var isActive = true {
        didSet {
            guard oldValue != isActive, !isActive else { return }
            releaseEverything()
        }
    }

    private enum Claim {
        case stick(side: CremaSide, began: TimeInterval, travel: CGFloat)
        case keys(Set<CremaKey>)
        case screen
    }

    private var layout = CremaLayout()
    private var builtFor: (CGSize, UIEdgeInsets, CGFloat, CremaSettings)?
    private var claims: [UITouch: Claim] = [:]
    private var held: Set<CremaKey> = []
    private var pulsing: Set<CremaKey> = []

    private let content = CALayer()
    private var keyLayers: [CremaKey: CAShapeLayer] = [:]
    private var restFills: [CremaKey: CGColor] = [:]
    private var knobLayers: [CremaSide: CAShapeLayer] = [:]
    private lazy var impact = UIImpactFeedbackGenerator(style: .light)

    private static let restFill = UIColor.black.withAlphaComponent(0.34).cgColor
    private static let heldFill = UIColor.white.withAlphaComponent(0.5).cgColor
    private static let outline = UIColor.white.withAlphaComponent(0.72).cgColor
    private static let knobFill = UIColor.white.withAlphaComponent(0.42).cgColor

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true
        content.opacity = Float(padOpacity)
        layer.addSublayer(content)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("Crema is created in code") }

    // MARK: Layout and drawing

    override func layoutSubviews() {
        super.layoutSubviews()
        let key = (bounds.size, safeAreaInsets, topInset, settings)
        if let built = builtFor, built.0 == key.0, built.1 == key.1, built.2 == key.2, built.3 == key.3 { return }
        builtFor = key
        rebuild()
    }

    override func safeAreaInsetsDidChange() {
        super.safeAreaInsetsDidChange()
        setNeedsLayout()
    }

    private func rebuild() {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        content.sublayers?.forEach { $0.removeFromSuperlayer() }
        keyLayers.removeAll()
        restFills.removeAll()
        knobLayers.removeAll()
        content.frame = bounds

        layout = CremaLayout.make(size: bounds.size, safe: safeAreaInsets, topInset: topInset, settings: settings)
        let u = layout.unit

        for ring in layout.rings {
            let base = shape(CGPath(ellipseIn: circleRect(ring.centre, ring.radius), transform: nil),
                             fill: UIColor.black.withAlphaComponent(0.22).cgColor)
            content.addSublayer(base)
            addSheen(over: base.path)
            let inner = CAShapeLayer()
            inner.path = CGPath(ellipseIn: circleRect(ring.centre, ring.radius * 0.7), transform: nil)
            inner.fillColor = UIColor.clear.cgColor
            inner.strokeColor = UIColor.white.withAlphaComponent(0.22).cgColor
            inner.lineWidth = 1
            content.addSublayer(inner)
            let knob = shape(CGPath(ellipseIn: circleRect(.zero, 0.7 * u), transform: nil), fill: Self.knobFill)
            knob.position = ring.centre
            content.addSublayer(knob)
            knobLayers[ring.side] = knob
        }

        addCross(layout.dpad, u: u)

        for arm in layout.face.arms {
            let centre = point(from: layout.face.centre, angle: arm.angle, distance: 1.3 * u)
            addKey(arm.key, path: CGPath(ellipseIn: circleRect(centre, 0.58 * u), transform: nil),
                   glyph: arm.key.label, glyphSize: 0.6 * u, glyphAt: centre)
        }

        let marks: [CremaKey: String] = [.minus: "\u{2212}", .plus: "+"]
        for pill in layout.pills {
            let path = pill.isRound
                ? CGPath(ellipseIn: pill.frame, transform: nil)
                : CGPath(roundedRect: pill.frame, cornerWidth: 0.32 * u, cornerHeight: 0.32 * u, transform: nil)
            addKey(pill.key, path: path, glyph: marks[pill.key] ?? pill.key.label,
                   glyphSize: pill.isRound ? 0.55 * u : 0.4 * u, glyphAt: CGPoint(x: pill.frame.midX, y: pill.frame.midY))
        }
        CATransaction.commit()
        refreshVisuals()
    }

    private static func glyphFont(_ size: CGFloat) -> UIFont {
        let base = UIFont.systemFont(ofSize: size, weight: .semibold)
        guard let rounded = base.fontDescriptor.withDesign(.rounded) else { return base }
        return UIFont(descriptor: rounded, size: size)
    }

    /// A faint light-from-above gradient that gives flat shapes some depth.
    private func addSheen(over path: CGPath?) {
        guard let path else { return }
        let box = path.boundingBoxOfPath
        let sheen = CAGradientLayer()
        sheen.frame = box
        sheen.colors = [UIColor.white.withAlphaComponent(0.16).cgColor, UIColor.white.withAlphaComponent(0.0).cgColor]
        sheen.startPoint = CGPoint(x: 0.5, y: 0)
        sheen.endPoint = CGPoint(x: 0.5, y: 1)
        let mask = CAShapeLayer()
        var shift = CGAffineTransform(translationX: -box.minX, y: -box.minY)
        mask.path = path.copy(using: &shift)
        sheen.mask = mask
        content.addSublayer(sheen)
    }

    /// The D-pad: one connected cross. Each arm lights up on its own when pressed.
    private func addCross(_ cluster: CremaLayout.Cluster, u: CGFloat) {
        let half = 1.75 * u
        let width = 1.0 * u
        let corner = 0.32 * u
        let c = cluster.centre
        let bar = CGPath(roundedRect: CGRect(x: c.x - half, y: c.y - width / 2, width: half * 2, height: width),
                         cornerWidth: corner, cornerHeight: corner, transform: nil)
        let post = CGPath(roundedRect: CGRect(x: c.x - width / 2, y: c.y - half, width: width, height: half * 2),
                          cornerWidth: corner, cornerHeight: corner, transform: nil)
        let cross = bar.union(post)
        let base = shape(cross)
        content.addSublayer(base)
        addSheen(over: cross)

        let arrows: [CremaKey: String] = [.up: "\u{25B2}", .right: "\u{25B6}", .down: "\u{25BC}", .left: "\u{25C0}"]
        for arm in cluster.arms {
            // The part of the cross that belongs to this arm, from the middle out to its tip.
            let strip = CGRect(x: width / 2, y: -width / 2, width: half - width / 2, height: width)
            var turn = CGAffineTransform(rotationAngle: arm.angle)
            var move = CGAffineTransform(translationX: c.x, y: c.y)
            let armPath = CGPath(rect: strip, transform: &turn).copy(using: &move) ?? cross
            let light = CAShapeLayer()
            light.path = armPath
            light.fillColor = UIColor.clear.cgColor
            let clip = CAShapeLayer()
            clip.path = cross
            light.mask = clip
            content.addSublayer(light)
            keyLayers[arm.key] = light
            restFills[arm.key] = UIColor.clear.cgColor

            let tip = point(from: c, angle: arm.angle, distance: 1.12 * u)
            addGlyph(arrows[arm.key] ?? "", size: 0.4 * u, at: tip)
        }
    }

    private func circleRect(_ centre: CGPoint, _ radius: CGFloat) -> CGRect {
        CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2)
    }

    private func point(from origin: CGPoint, angle: CGFloat, distance: CGFloat) -> CGPoint {
        CGPoint(x: origin.x + cos(angle) * distance, y: origin.y + sin(angle) * distance)
    }

    private func shape(_ path: CGPath, fill: CGColor? = nil) -> CAShapeLayer {
        let layer = CAShapeLayer()
        layer.path = path
        layer.fillColor = fill ?? Self.restFill
        layer.strokeColor = Self.outline
        layer.lineWidth = 1.2
        return layer
    }

    private func addKey(_ key: CremaKey, path: CGPath, glyph: String, glyphSize: CGFloat, glyphAt: CGPoint) {
        let button = shape(path)
        content.addSublayer(button)
        addSheen(over: path)
        keyLayers[key] = button

        if key == .home {
            addHouse(size: glyphSize, at: glyphAt)
            return
        }
        addGlyph(glyph, size: glyphSize, at: glyphAt)
    }

    private func addGlyph(_ glyph: String, size glyphSize: CGFloat, at glyphAt: CGPoint) {
        guard !glyph.isEmpty else { return }
        let text = CATextLayer()
        text.string = glyph
        text.font = Self.glyphFont(glyphSize)
        text.fontSize = glyphSize
        text.foregroundColor = UIColor.white.withAlphaComponent(0.9).cgColor
        text.alignmentMode = .center
        text.contentsScale = traitCollection.displayScale
        let height = glyphSize * 1.3
        text.frame = CGRect(x: glyphAt.x - glyphSize * 2, y: glyphAt.y - height / 2, width: glyphSize * 4, height: height)
        content.addSublayer(text)
    }

    /// HOME is drawn as a house symbol, centred in its button.
    private func addHouse(size: CGFloat, at centre: CGPoint) {
        let config = UIImage.SymbolConfiguration(pointSize: size * 0.8, weight: .semibold)
        guard let symbol = UIImage(systemName: "house.fill", withConfiguration: config) else { return }
        let scale = traitCollection.displayScale
        let format = UIGraphicsImageRendererFormat()
        format.scale = scale
        format.opaque = false
        let icon = UIGraphicsImageRenderer(size: symbol.size, format: format).image { _ in
            symbol.withTintColor(.white, renderingMode: .alwaysOriginal).draw(in: CGRect(origin: .zero, size: symbol.size))
        }
        let layer = CALayer()
        layer.contents = icon.cgImage
        layer.contentsScale = scale
        layer.bounds = CGRect(origin: .zero, size: symbol.size)
        layer.position = centre
        content.addSublayer(layer)
    }

    private func refreshVisuals() {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (key, layer) in keyLayers {
            layer.fillColor = held.contains(key) ? Self.heldFill : (restFills[key] ?? Self.restFill)
        }
        CATransaction.commit()
    }

    private func moveKnob(_ side: CremaSide, to offset: CGPoint) {
        guard let ring = layout.rings.first(where: { $0.side == side }), let knob = knobLayers[side] else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let inside = CremaStickMath.knob(offset: offset, radius: ring.radius)
        knob.position = CGPoint(x: ring.centre.x + inside.x, y: ring.centre.y + inside.y)
        CATransaction.commit()
    }

    // MARK: Hit testing

    private var touchscreenLocal: CGRect? {
        guard let frame = touchscreenFrame, frame.width > 1, frame.height > 1 else { return nil }
        return convert(frame, from: nil)
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard isActive, !isHidden, self.point(inside: point, with: event) else { return nil }
        if layout.ring(at: point) != nil || !layout.keys(at: point).isEmpty { return self }
        if touchscreenLocal?.contains(point) == true { return self }
        return nil
    }

    // MARK: Touches

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches { begin(touch) }
        settle()
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches { move(touch) }
        settle()
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches { finish(touch, cancelled: false) }
        settle()
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches { finish(touch, cancelled: true) }
        settle()
    }

    private func claimedSticks() -> Set<CremaSide> {
        var sides: Set<CremaSide> = []
        for case .stick(let side, _, _) in claims.values { sides.insert(side) }
        return sides
    }

    private func begin(_ touch: UITouch) {
        let p = touch.location(in: self)
        if let ring = layout.ring(at: p), !claimedSticks().contains(ring.side) {
            claims[touch] = .stick(side: ring.side, began: touch.timestamp, travel: 0)
            move(touch)
            return
        }
        let keys = layout.keys(at: p)
        if !keys.isEmpty {
            claims[touch] = .keys(keys)
            return
        }
        if touchscreenLocal?.contains(p) == true, !claims.values.contains(where: { if case .screen = $0 { return true } else { return false } }) {
            claims[touch] = .screen
            sendTouchscreen(p)
        }
    }

    private func move(_ touch: UITouch) {
        guard let claim = claims[touch] else { return }
        let p = touch.location(in: self)
        switch claim {
        case .stick(let side, let began, let travel):
            guard let ring = layout.rings.first(where: { $0.side == side }) else { return }
            let offset = CGPoint(x: p.x - ring.centre.x, y: p.y - ring.centre.y)
            claims[touch] = .stick(side: side, began: began, travel: max(travel, hypot(offset.x, offset.y)))
            moveKnob(side, to: offset)
            CremaOutput.shared.setStick(side, axis: CremaStickMath.axis(offset: offset, radius: ring.radius,
                                                                        deadzone: settings.deadzone))
        case .keys:
            claims[touch] = .keys(layout.keys(at: p))
        case .screen:
            sendTouchscreen(p)
        }
    }

    private func finish(_ touch: UITouch, cancelled: Bool) {
        guard let claim = claims.removeValue(forKey: touch) else { return }
        switch claim {
        case .stick(let side, let began, let travel):
            moveKnob(side, to: .zero)
            CremaOutput.shared.setStick(side, axis: .zero)
            if let ring = layout.rings.first(where: { $0.side == side }),
               !cancelled, touch.timestamp - began < 0.22, travel < ring.radius * 0.3 {
                pulse(side.stickKey)
            }
        case .keys:
            break
        case .screen:
            CremaOutput.shared.setTouchscreen(nil)
        }
    }

    /// A quick tap on a stick clicks it (L3 or R3).
    private func pulse(_ key: CremaKey) {
        pulsing.insert(key)
        Task { [weak self] in
            try? await Task.sleep(nanoseconds: 90_000_000)
            guard let self else { return }
            self.pulsing.remove(key)
            self.settle()
        }
    }

    private func sendTouchscreen(_ p: CGPoint) {
        guard let rect = touchscreenLocal else { return }
        let x = min(max(p.x - rect.minX, 0), rect.width)
        let y = min(max(p.y - rect.minY, 0), rect.height)
        let scale = CGFloat(DisplayRouter.shared.padSurfaceScale)
        CremaOutput.shared.setTouchscreen(CGPoint(x: x * scale, y: y * scale))
    }

    /// Works out which keys should be down from every finger and sends only what changed.
    private func settle() {
        var wanted = pulsing
        for case .keys(let keys) in claims.values { wanted.formUnion(keys) }
        let released = held.subtracting(wanted)
        let pressed = wanted.subtracting(held)
        guard !released.isEmpty || !pressed.isEmpty else { return }
        for key in released { CremaOutput.shared.setKey(key, pressed: false) }
        for key in pressed { CremaOutput.shared.setKey(key, pressed: true) }
        if !pressed.isEmpty, hapticsEnabled {
            impact.impactOccurred(intensity: 0.7)
        }
        held = wanted
        refreshVisuals()
    }

    /// Lets go of every finger, key and stick. Used when the controls stop taking input or leave the screen.
    func releaseEverything() {
        claims.removeAll()
        pulsing.removeAll()
        held.removeAll()
        for side in [CremaSide.left, .right] { moveKnob(side, to: .zero) }
        CremaOutput.shared.releaseAll()
        refreshVisuals()
    }
}
