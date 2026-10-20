import SwiftUI

/// The drawing behind the "Controller buttons" page: a Wii U Pro Controller made of SwiftUI shapes. It knows nothing
/// about the emulator. Buttons are numbered like CemuBridgePadMapButton. Everything is laid out on a grid of
/// 40 x 30 units, where one unit is a fortieth of the width.
enum PadDiagramKey {
    static let a = 1, b = 2, x = 3, y = 4, l = 5, r = 6, zl = 7, zr = 8
    static let plus = 9, minus = 10, up = 11, down = 12, left = 13, right = 14, stickL = 15, stickR = 16
}

struct ControllerDiagram: View {
    let accent: Color
    let selected: Int?
    let changed: Set<Int>
    let listening: Bool
    let blink: Bool
    let onSelect: (Int) -> Void

    static let aspect: CGFloat = 40.0 / 30.0
    private static let minimumTouch: CGFloat = 44

    var body: some View {
        GeometryReader { proxy in
            let u = proxy.size.width / 40
            ZStack {
                body(u)
                shoulders(u)
                centre(u)
                sticks(u)
                dpad(u)
                faceButtons(u)
            }
            .frame(width: proxy.size.width, height: u * 30)
        }
        .aspectRatio(Self.aspect, contentMode: .fit)
    }

    // MARK: Body

    private func body(_ u: CGFloat) -> some View {
        ZStack {
            ProBodyShape()
                .fill(LinearGradient(colors: [Color(red: 0.25, green: 0.27, blue: 0.31), Color(red: 0.09, green: 0.10, blue: 0.13)],
                                     startPoint: .top, endPoint: .bottom))
                .shadow(color: Color.black.opacity(0.5), radius: u * 1.4, y: u * 0.9)
            ProBodyShape()
                .fill(RadialGradient(colors: [Color.white.opacity(0.07), Color.clear], center: UnitPoint(x: 0.5, y: 0.1),
                                     startRadius: 0, endRadius: u * 26))
            ProBodyShape()
                .fill(LinearGradient(colors: [Color.white.opacity(0.12), Color.clear], startPoint: .top, endPoint: UnitPoint(x: 0.5, y: 0.5)))
            ProBodyShape()
                .stroke(LinearGradient(colors: [Color.white.opacity(0.45), Color.white.opacity(0.05)], startPoint: .top, endPoint: .bottom),
                        lineWidth: max(1, u * 0.12))
            grooves(u)
            lights(u)
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }

    private func grooves(_ u: CGFloat) -> some View {
        ZStack {
            ForEach(0..<3, id: \.self) { index in
                let inset = CGFloat(index) * u * 0.9
                Capsule()
                    .fill(Color.black.opacity(0.28))
                    .frame(width: u * 0.35, height: u * 3.4)
                    .rotationEffect(.degrees(8))
                    .position(x: u * 4.6 + inset, y: u * 24.6)
                Capsule()
                    .fill(Color.black.opacity(0.28))
                    .frame(width: u * 0.35, height: u * 3.4)
                    .rotationEffect(.degrees(-8))
                    .position(x: u * 35.4 - inset, y: u * 24.6)
            }
        }
    }

    private func lights(_ u: CGFloat) -> some View {
        HStack(spacing: u * 0.9) {
            ForEach(0..<4, id: \.self) { index in
                Circle()
                    .fill(index == 0 ? Color(red: 0.36, green: 0.86, blue: 0.56) : Color.white.opacity(0.14))
                    .frame(width: u * 0.7, height: u * 0.7)
            }
        }
        .position(x: u * 20.2, y: u * 24.0)
    }

    // MARK: Groups

    private func shoulders(_ u: CGFloat) -> some View {
        ZStack {
            key(PadDiagramKey.zl, "ZL", .capsule, u, w: 9, h: 2.8, x: 8.2, y: 1.7)
            key(PadDiagramKey.zr, "ZR", .capsule, u, w: 9, h: 2.8, x: 31.8, y: 1.7)
            key(PadDiagramKey.l, "L", .capsule, u, w: 10.5, h: 2.2, x: 8.2, y: 4.3)
            key(PadDiagramKey.r, "R", .capsule, u, w: 10.5, h: 2.2, x: 31.8, y: 4.3)
        }
    }

    private func centre(_ u: CGFloat) -> some View {
        ZStack {
            key(PadDiagramKey.minus, "\u{2212}", .capsule, u, w: 3.6, h: 2.0, x: 16.2, y: 8.4)
            Image(systemName: "house.fill")
                .font(.system(size: u * 1.1, weight: .bold))
                .foregroundColor(Color.white.opacity(0.4))
                .frame(width: u * 2.6, height: u * 2.6)
                .background(Circle().fill(Color.black.opacity(0.4)))
                .position(x: u * 20, y: u * 8.4)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            key(PadDiagramKey.plus, "+", .capsule, u, w: 3.6, h: 2.0, x: 23.8, y: 8.4)
        }
    }

    private func sticks(_ u: CGFloat) -> some View {
        ZStack {
            stick(PadDiagramKey.stickL, "L3", u, x: 8.2, y: 12.0)
            stick(PadDiagramKey.stickR, "R3", u, x: 26.0, y: 20.6)
        }
    }

    private func dpad(_ u: CGFloat) -> some View {
        let cx: CGFloat = 13.6, cy: CGFloat = 19.6, arm: CGFloat = 4.0
        return ZStack {
            Circle()
                .fill(Color.black.opacity(0.3))
                .frame(width: u * 10.6, height: u * 10.6)
                .position(x: u * cx, y: u * cy)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            RoundedRectangle(cornerRadius: u * 0.9, style: .continuous)
                .fill(Color(white: 0.2))
                .frame(width: u * arm, height: u * arm)
                .position(x: u * cx, y: u * cy)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            key(PadDiagramKey.up, "\u{25B2}", .rounded, u, w: arm, h: arm, x: cx, y: cy - arm)
            key(PadDiagramKey.down, "\u{25BC}", .rounded, u, w: arm, h: arm, x: cx, y: cy + arm)
            key(PadDiagramKey.left, "\u{25C0}", .rounded, u, w: arm, h: arm, x: cx - arm, y: cy)
            key(PadDiagramKey.right, "\u{25B6}", .rounded, u, w: arm, h: arm, x: cx + arm, y: cy)
        }
    }

    private func faceButtons(_ u: CGFloat) -> some View {
        let cx: CGFloat = 30.4, cy: CGFloat = 12.2, gap: CGFloat = 3.8, d: CGFloat = 4.2
        return ZStack {
            key(PadDiagramKey.x, "X", .circle, u, w: d, h: d, x: cx, y: cy - gap)
            key(PadDiagramKey.y, "Y", .circle, u, w: d, h: d, x: cx - gap, y: cy)
            key(PadDiagramKey.a, "A", .circle, u, w: d, h: d, x: cx + gap, y: cy)
            key(PadDiagramKey.b, "B", .circle, u, w: d, h: d, x: cx, y: cy + gap)
        }
    }

    // MARK: Keys

    private func key(_ id: Int, _ label: String, _ kind: PadKeyKind, _ u: CGFloat,
                     w: CGFloat, h: CGFloat, x: CGFloat, y: CGFloat) -> some View {
        let isSelected = selected == id
        let face = PadKeyView(label: label, kind: kind, width: u * w, height: u * h, accent: accent,
                              selected: isSelected, changed: changed.contains(id), pulsing: isSelected && listening && blink)
        return touchTarget(face, id: id, label: label, width: u * w, height: u * h)
            .position(x: u * x, y: u * y)
    }

    private func stick(_ id: Int, _ caption: String, _ u: CGFloat, x: CGFloat, y: CGFloat) -> some View {
        let isSelected = selected == id
        let face = PadStickView(caption: caption, diameter: u * 6.4, accent: accent, selected: isSelected,
                                changed: changed.contains(id), pulsing: isSelected && listening && blink)
        return touchTarget(face, id: id, label: caption, width: u * 6.4, height: u * 6.4)
            .position(x: u * x, y: u * y)
    }

    private func touchTarget<Face: View>(_ face: Face, id: Int, label: String, width: CGFloat, height: CGFloat) -> some View {
        let hitWidth = max(width, Self.minimumTouch)
        let hitHeight = max(height, Self.minimumTouch)
        let traits: AccessibilityTraits = selected == id ? [.isButton, .isSelected] : [.isButton]
        return Button {
            onSelect(id)
        } label: {
            face
                .frame(width: hitWidth, height: hitHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("GamePad \(label)")
        .accessibilityAddTraits(traits)
    }
}

enum PadKeyKind { case circle, capsule, rounded }

private struct PadKeyShape: Shape {
    let kind: PadKeyKind

    func path(in rect: CGRect) -> Path {
        switch kind {
        case .circle: return Circle().path(in: rect)
        case .capsule: return Capsule().path(in: rect)
        case .rounded: return RoundedRectangle(cornerRadius: min(rect.width, rect.height) * 0.3, style: .continuous).path(in: rect)
        }
    }
}

/// The outline of the controller on the 40 x 30 grid: a wide body, two grips and a shallow notch between them.
private struct ProBodyShape: Shape {
    func path(in rect: CGRect) -> Path {
        let sx = rect.width / 40
        let sy = rect.height / 30
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: rect.minX + x * sx, y: rect.minY + y * sy) }
        var path = Path()
        path.move(to: p(2.6, 12.5))
        path.addCurve(to: p(10.0, 5.4), control1: p(2.6, 7.6), control2: p(5.8, 5.4))
        path.addLine(to: p(30.0, 5.4))
        path.addCurve(to: p(37.4, 12.5), control1: p(34.2, 5.4), control2: p(37.4, 7.6))
        path.addCurve(to: p(38.6, 26.0), control1: p(37.6, 17.5), control2: p(39.0, 21.5))
        path.addCurve(to: p(34.6, 29.5), control1: p(38.3, 28.8), control2: p(36.6, 29.7))
        path.addCurve(to: p(27.8, 27.0), control1: p(32.4, 29.2), control2: p(31.0, 27.2))
        path.addCurve(to: p(12.2, 27.0), control1: p(24.2, 26.6), control2: p(15.8, 26.6))
        path.addCurve(to: p(5.4, 29.5), control1: p(9.0, 27.2), control2: p(7.6, 29.2))
        path.addCurve(to: p(1.4, 26.0), control1: p(3.4, 29.7), control2: p(1.7, 28.8))
        path.addCurve(to: p(2.6, 12.5), control1: p(1.0, 21.5), control2: p(2.4, 17.5))
        path.closeSubpath()
        return path
    }
}

private struct PadKeyView: View {
    let label: String
    let kind: PadKeyKind
    let width: CGFloat
    let height: CGFloat
    let accent: Color
    let selected: Bool
    let changed: Bool
    let pulsing: Bool

    var body: some View {
        let shape = PadKeyShape(kind: kind)
        let colors: [Color] = selected ? [accent, accent.opacity(0.78)] : [Color(white: 0.42), Color(white: 0.21)]
        let size = min(width, height)
        return ZStack(alignment: .topTrailing) {
            shape.fill(LinearGradient(colors: colors, startPoint: .top, endPoint: .bottom))
            shape.stroke(Color.white.opacity(selected ? 0.95 : 0.2), lineWidth: selected ? 2 : 1)
            shape.fill(LinearGradient(colors: [Color.white.opacity(0.22), Color.clear], startPoint: .top, endPoint: .center))
            Text(label)
                .font(.system(size: max(9, size * 0.42), weight: .heavy, design: .rounded))
                .foregroundColor(Color.white.opacity(0.94))
                .frame(width: width, height: height)
            if changed {
                ChangedDot().offset(x: size * 0.12, y: -size * 0.12)
            }
        }
        .frame(width: width, height: height)
        .shadow(color: selected ? accent.opacity(0.85) : Color.black.opacity(0.4), radius: selected ? size * 0.35 : size * 0.1, y: selected ? 0 : size * 0.06)
        .scaleEffect(selected ? 1.07 : 1)
        .opacity(pulsing ? 0.6 : 1)
        .animation(.spring(response: 0.3, dampingFraction: 0.65), value: selected)
    }
}

private struct PadStickView: View {
    let caption: String
    let diameter: CGFloat
    let accent: Color
    let selected: Bool
    let changed: Bool
    let pulsing: Bool

    var body: some View {
        let cap = diameter * 0.7
        let colors: [Color] = selected ? [accent, accent.opacity(0.75)] : [Color(white: 0.42), Color(white: 0.2)]
        return ZStack(alignment: .topTrailing) {
            Circle().fill(Color.black.opacity(0.6))
            Circle().stroke(Color.white.opacity(0.1), lineWidth: 1)
            Circle()
                .fill(LinearGradient(colors: colors, startPoint: .top, endPoint: .bottom))
                .frame(width: cap, height: cap)
                .overlay(Circle().stroke(Color.white.opacity(selected ? 0.95 : 0.22), lineWidth: selected ? 2 : 1))
                .overlay(Circle().fill(RadialGradient(colors: [Color.white.opacity(0.22), Color.clear], center: UnitPoint(x: 0.35, y: 0.28),
                                                      startRadius: 0, endRadius: cap * 0.55)))
                .overlay(Circle().fill(Color.black.opacity(0.18)).frame(width: cap * 0.5, height: cap * 0.5))
                .frame(width: diameter, height: diameter)
            Text(caption)
                .font(.system(size: max(8, diameter * 0.15), weight: .bold, design: .rounded))
                .foregroundColor(Color.white.opacity(0.7))
                .frame(width: diameter, height: diameter)
            if changed {
                ChangedDot().offset(x: -diameter * 0.04, y: diameter * 0.04)
            }
        }
        .frame(width: diameter, height: diameter)
        .shadow(color: selected ? accent.opacity(0.85) : Color.clear, radius: diameter * 0.12)
        .scaleEffect(selected ? 1.04 : 1)
        .opacity(pulsing ? 0.6 : 1)
        .animation(.spring(response: 0.3, dampingFraction: 0.7), value: selected)
    }
}

private struct ChangedDot: View {
    var body: some View {
        Circle()
            .fill(Color(red: 0.36, green: 0.86, blue: 0.56))
            .frame(width: 9, height: 9)
            .overlay(Circle().stroke(Color.black.opacity(0.45), lineWidth: 1))
    }
}
