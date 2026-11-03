// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

extension Color {
    init(hex: String) {
        var s = hex
        if s.hasPrefix("#") { s.removeFirst() }
        var value: UInt64 = 0
        Scanner(string: s).scanHexInt64(&value)
        let r = Double((value >> 16) & 0xFF) / 255.0
        let g = Double((value >> 8) & 0xFF) / 255.0
        let b = Double(value & 0xFF) / 255.0
        self.init(red: r, green: g, blue: b)
    }

    init(light: String, dark: String) {
        self.init(uiColor: UIColor { traits in
            traits.userInterfaceStyle == .dark ? UIColor(Color(hex: dark)) : UIColor(Color(hex: light))
        })
    }

    init(light: String, lightAlpha: Double, dark: String, darkAlpha: Double) {
        self.init(uiColor: UIColor { traits in
            traits.userInterfaceStyle == .dark
                ? UIColor(Color(hex: dark)).withAlphaComponent(CGFloat(darkAlpha))
                : UIColor(Color(hex: light)).withAlphaComponent(CGFloat(lightAlpha))
        })
    }
}

private func espressuHexChannels(_ hex: String) -> (Double, Double, Double) {
    var s = hex
    if s.hasPrefix("#") { s.removeFirst() }
    var value: UInt64 = 0
    Scanner(string: s).scanHexInt64(&value)
    return (Double((value >> 16) & 0xFF), Double((value >> 8) & 0xFF), Double(value & 0xFF))
}

private func espressuMixHex(_ a: String, _ b: String, _ amount: Double) -> String {
    let k = min(max(amount, 0), 1)
    let (ar, ag, ab) = espressuHexChannels(a)
    let (br, bg, bb) = espressuHexChannels(b)
    let r = UInt64((ar + (br - ar) * k).rounded())
    let g = UInt64((ag + (bg - ag) * k).rounded())
    let b2 = UInt64((ab + (bb - ab) * k).rounded())
    return String(format: "#%02llX%02llX%02llX", r, g, b2)
}

private func espressuLift(_ hex: String, _ amount: Double) -> String {
    espressuMixHex(hex, "#FFFFFF", amount)
}

private func espressuDeepen(_ hex: String, _ amount: Double) -> String {
    espressuMixHex(hex, "#000000", amount)
}

private func espressuLuminance(_ hex: String) -> Double {
    let (r, g, b) = espressuHexChannels(hex)
    func linear(_ v: Double) -> Double {
        let c = v / 255
        return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)
    }
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b)
}

private func espressuContrast(_ a: String, _ b: String) -> Double {
    let la = espressuLuminance(a)
    let lb = espressuLuminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
}

private func espressuWorstContrast(_ hex: String, on grounds: [String]) -> Double {
    grounds.map { espressuContrast(hex, $0) }.min() ?? 21
}

private func espressuReadable(_ hex: String, on grounds: [String], minimum: Double, toward: String) -> String {
    var amount = 0.0
    while amount < 1.0 {
        let candidate = espressuMixHex(hex, toward, amount)
        if espressuWorstContrast(candidate, on: grounds) >= minimum { return candidate }
        amount += 0.05
    }
    return toward
}

/// System-driven palette. Surfaces, text and separators come from UIKit's semantic colours so the
/// app follows light/dark, Increase Contrast and Reduce Transparency automatically. The only
/// customisable colour is the accent, taken from the selected theme preset.
enum EspressUTheme {
    private static var t: EspressUThemeDefinition { EspressUThemeStore.shared.current }

    // MARK: Accent

    static var pixelBlue: Color { Color(light: t.pixelBlueLight, dark: t.pixelBlueDark) }
    static var blueberryNavy: Color { pixelBlue }
    static var cremaTopLight: Color { pixelBlue }
    static var cremaTopDark: Color { pixelBlue }

    private static var lightTextGrounds: [String] { ["#FFFFFF", "#F2F2F7"] }
    private static var darkTextGrounds: [String] { ["#000000", "#1C1C1E", "#2C2C2E"] }

    private static func readableInk(light: String, dark: String) -> Color {
        Color(light: espressuReadable(light, on: lightTextGrounds, minimum: 4.5, toward: "#000000"),
              dark: espressuReadable(dark, on: darkTextGrounds, minimum: 4.5, toward: "#FFFFFF"))
    }

    static func readable(_ light: KeyPath<EspressUThemeDefinition, String>,
                         _ dark: KeyPath<EspressUThemeDefinition, String>) -> Color {
        readableInk(light: t[keyPath: light], dark: t[keyPath: dark])
    }

    /// Accent used for text and glyphs, nudged until it reads at 4.5:1 on system backgrounds.
    static var accentText: Color { readableInk(light: t.pixelBlueLight, dark: t.pixelBlueDark) }
    static var accentOnDark: Color { accentText }
    static var onBackgroundAccent: Color { accentText }

    /// Foreground for content placed on a filled accent control.
    static var onAccent: Color {
        func ink(_ fill: String) -> String { espressuContrast("#FFFFFF", fill) >= 3 ? "#FFFFFF" : "#000000" }
        return Color(light: ink(t.pixelBlueLight), dark: ink(t.pixelBlueDark))
    }
    static var onCremaTop: Color { onAccent }
    static var sparkleCream: Color { Color.white }

    // MARK: Semantic colours

    static var blushPink: Color { Color(UIColor.systemRed) }
    static var alertText: Color { Color(UIColor.systemRed) }
    static var alertOnDark: Color { Color(UIColor.systemRed) }
    static var cautionText: Color { Color(UIColor.systemOrange) }

    // MARK: Text

    static var brownDarkest: Color { Color(UIColor.label) }
    static var brownDark: Color { Color(UIColor.label) }
    static var brownMid: Color { Color(UIColor.secondaryLabel) }
    static var secondaryText: Color { Color(UIColor.secondaryLabel) }
    static var onBackground: Color { Color(UIColor.label) }
    static var onBackgroundMuted: Color { Color(UIColor.secondaryLabel) }

    // MARK: Surfaces

    /// Light mode uses a softer grey ground with off-white cards instead of the near-white system defaults; dark mode keeps the system values.
    private static func adaptive(light: UIColor, dark: UIColor) -> Color {
        Color(uiColor: UIColor { $0.userInterfaceStyle == .dark ? dark : light })
    }
    static var formGround: Color { adaptive(light: UIColor(red: 0.855, green: 0.855, blue: 0.875, alpha: 1), dark: .systemGroupedBackground) }
    static var backgroundTop: Color { formGround }
    static var backgroundBottom: Color { formGround }
    static var cream: Color { adaptive(light: UIColor(red: 0.945, green: 0.945, blue: 0.957, alpha: 1), dark: .secondarySystemGroupedBackground) }
    static var creamPressed: Color { adaptive(light: UIColor(red: 0.89, green: 0.89, blue: 0.91, alpha: 1), dark: .tertiarySystemGroupedBackground) }
    /// Neutral fills are translucent overlays rather than opaque greys, so they sit on glass and backdrops.
    static var wrapper: Color {
        Color(uiColor: UIColor { $0.userInterfaceStyle == .dark ? UIColor.white.withAlphaComponent(0.12) : UIColor.black.withAlphaComponent(0.08) })
    }
    static var surfaceRaised: Color { adaptive(light: UIColor(red: 0.92, green: 0.92, blue: 0.935, alpha: 1), dark: .tertiarySystemGroupedBackground) }
    static var surfaceSunken: Color { formGround }
    static var surfaceHighlight: Color { .clear }
    static var surfaceShade: Color { .clear }
    static var hairline: Color { Color(UIColor.separator) }
    static var shadow: Color { .black }
    static var scrim: Color { Color.black.opacity(0.4) }

    static var hairlineWidth: CGFloat {
        let scale = UITraitCollection.current.displayScale
        return scale > 0 ? 1.0 / scale : 0.5
    }

    // Gradients are kept as flat fills so existing call sites keep compiling.
    static var backgroundGradient: LinearGradient { flat(formGround) }
    static var cremaTopGradient: LinearGradient { flat(pixelBlue) }
    static var cremaTopGradientPressed: LinearGradient { flat(pixelBlue.opacity(0.8)) }
    static var edgeStroke: LinearGradient { flat(hairline) }
    static var controlEdgeStroke: LinearGradient { flat(.clear) }
    static var surfaceSheen: LinearGradient { flat(.clear) }
    static var controlSheen: LinearGradient { flat(.clear) }

    private static func flat(_ color: Color) -> LinearGradient {
        LinearGradient(colors: [color, color], startPoint: .top, endPoint: .bottom)
    }

    enum Elevation {
        case flush, resting, raised, floating, overlay

        /// Apple keeps grouped content flat; only floating chrome casts a soft shadow.
        var shadow: (radius: CGFloat, y: CGFloat, opacity: Double) {
            switch self {
            case .flush, .resting, .raised: return (0, 0, 0)
            case .floating: return (12, 4, 0.12)
            case .overlay:  return (24, 8, 0.18)
            }
        }
    }

    enum Font {
        static var display: SwiftUI.Font { .largeTitle.weight(.bold) }
        static var title: SwiftUI.Font { .title2.weight(.bold) }
        static var sectionTitle: SwiftUI.Font { .headline }
        static var rowLabel: SwiftUI.Font { .body }
        static var rowValue: SwiftUI.Font { .body }
        static var caption: SwiftUI.Font { .footnote }
        static var subCaption: SwiftUI.Font { .caption }
        static var micro: SwiftUI.Font { .caption2 }
        static var primaryButton: SwiftUI.Font { .body.weight(.semibold) }
        static var secondaryButton: SwiftUI.Font { .subheadline.weight(.semibold) }
        static var monoTag: SwiftUI.Font { .system(.caption, design: .monospaced).weight(.semibold) }
    }

    enum Space {
        static let hair: CGFloat = 2
        static let tight: CGFloat = 4
        static let snug: CGFloat = 8
        static let regular: CGFloat = 12
        static let comfy: CGFloat = 16
        static let loose: CGFloat = 24
        static let section: CGFloat = 32
    }

    enum Radius {
        static let chip: CGFloat = 10
        static let control: CGFloat = 12
        static let card: CGFloat = 20
        static let sheet: CGFloat = 28
    }

    enum Motion {
        static var pressDown: SwiftUI.Animation { .easeOut(duration: 0.1) }
        static var pressRelease: SwiftUI.Animation { .easeOut(duration: 0.2) }
        static var state: SwiftUI.Animation { .snappy(duration: 0.25) }
        static var layout: SwiftUI.Animation { .smooth(duration: 0.35) }

        static let pressScale: CGFloat = 1
        static let compactPressScale: CGFloat = 1

        static func press(isPressed: Bool, reduceMotion: Bool) -> SwiftUI.Animation {
            isPressed ? pressDown : pressRelease
        }
    }
}

enum EspressUHaptics {
    static let isEnabled = true

    static func tap() {
        guard isEnabled else { return }
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
    }

    static func select() {
        guard isEnabled else { return }
        UISelectionFeedbackGenerator().selectionChanged()
    }
}

private struct EspressUElevationModifier: ViewModifier {
    let level: EspressUTheme.Elevation

    func body(content: Content) -> some View {
        let s = level.shadow
        content.shadow(color: Color.black.opacity(s.opacity), radius: s.radius, x: 0, y: s.y)
    }
}

extension View {
    @ViewBuilder func espressuOpaqueNavigationBar(_ fill: Color = EspressUTheme.cream) -> some View {
        // System navigation bars (scroll-edge appearance, Liquid Glass on iOS 26) are used as-is.
        self
    }

    func espressuElevation(_ level: EspressUTheme.Elevation) -> some View {
        modifier(EspressUElevationModifier(level: level))
    }

    func espressuScreenBackground() -> some View {
        background(EspressUTheme.formGround.ignoresSafeArea())
    }

    func espressuRowLabel() -> some View {
        font(EspressUTheme.Font.rowLabel).foregroundColor(.primary)
    }

    func espressuRowValue() -> some View {
        font(EspressUTheme.Font.rowValue).foregroundColor(.secondary)
    }

    func espressuSectionTitle() -> some View {
        font(EspressUTheme.Font.sectionTitle).foregroundColor(.primary)
    }

    func espressuCaption() -> some View {
        font(EspressUTheme.Font.caption).foregroundColor(.secondary)
    }

    func espressuSubCaption() -> some View {
        font(EspressUTheme.Font.subCaption).foregroundColor(.secondary)
    }
}

struct EspressUCard<Content: View>: View {
    var cornerRadius: CGFloat = EspressUTheme.Radius.card
    var fill: Color = EspressUTheme.cream
    @ViewBuilder var content: Content

    var body: some View {
        content
            .background(fill, in: RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
    }
}

/// Prominent button: tinted Liquid Glass on iOS 26, a filled accent shape before that.
struct EspressUPrimaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        let shape = Capsule()
        let label = configuration.label
            .font(EspressUTheme.Font.primaryButton)
            .foregroundColor(EspressUTheme.onAccent)
            .padding(.horizontal, 18)
            .padding(.vertical, 12)
        Group {
            if #available(iOS 26.0, *) {
                label.glassEffect(.regular.tint(EspressUTheme.pixelBlue).interactive(), in: shape)
            } else {
                label.background(EspressUTheme.pixelBlue, in: shape)
                    .opacity(configuration.isPressed ? 0.7 : 1)
            }
        }
        .opacity(isEnabled ? 1 : 0.4)
    }
}

/// Secondary button: clear Liquid Glass on iOS 26, a material capsule before that.
struct EspressUSecondaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        let shape = Capsule()
        let label = configuration.label
            .font(EspressUTheme.Font.secondaryButton)
            .foregroundColor(.primary)
            .padding(.horizontal, 16)
            .padding(.vertical, 9)
        Group {
            if #available(iOS 26.0, *) {
                label.glassEffect(.regular.interactive(), in: shape)
            } else {
                label.background(.regularMaterial, in: shape)
                    .opacity(configuration.isPressed ? 0.6 : 1)
            }
        }
        .opacity(isEnabled ? 1 : 0.4)
    }
}

/// Thin alias so every sheet and screen shares one navigation container.
struct EspressUNavigation<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        NavigationStack { content }
    }
}
