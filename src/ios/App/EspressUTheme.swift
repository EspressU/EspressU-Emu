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

enum EspressUTheme {
    private static var t: EspressUThemeDefinition { EspressUThemeStore.shared.current }

    static var backgroundTop: Color { Color(light: t.backgroundTopLight, dark: t.backgroundTopDark) }
    static var backgroundBottom: Color { Color(light: t.backgroundBottomLight, dark: t.backgroundBottomDark) }

    static var cremaTopLight: Color { Color(light: t.cremaTopLightLight, dark: t.cremaTopLightDark) }
    static var cremaTopDark: Color { Color(light: t.cremaTopDarkLight, dark: t.cremaTopDarkDark) }

    static var cream: Color { Color(light: t.creamLight, dark: t.creamDark) }
    static var wrapper: Color { Color(light: t.wrapperLight, dark: t.wrapperDark) }

    static var blueberryNavy: Color { Color(light: t.blueberryNavyLight, dark: t.blueberryNavyDark) }

    static var pixelBlue: Color { Color(light: t.pixelBlueLight, dark: t.pixelBlueDark) }

    static var blushPink: Color { Color(light: t.blushPinkLight, dark: t.blushPinkDark) }

    static var brownDarkest: Color { readableInk(light: t.brownDarkestLight, dark: t.brownDarkestDark) }
    static var brownDark: Color { readableInk(light: t.brownDarkLight, dark: t.brownDarkDark) }
    static var brownMid: Color { readableInk(light: t.brownMidLight, dark: t.brownMidDark) }

    static var sparkleCream: Color { Color(light: t.sparkleCreamLight, dark: t.sparkleCreamDark) }

    static var shadow: Color { Color(light: t.shadowLight, dark: t.shadowDark) }

    private static var lightTextGrounds: [String] {
        ["#FFFFFF", "#F2F2F7", t.creamLight, t.wrapperLight,
         espressuDeepen(espressuMixHex(t.creamLight, t.wrapperLight, 0.8), 0.04)]
    }

    private static var darkTextGrounds: [String] {
        ["#000000", "#1C1C1E", "#2C2C2E", t.creamDark, t.wrapperDark]
    }

    private static func readableInk(light: String, dark: String) -> Color {
        Color(light: espressuReadable(light, on: lightTextGrounds, minimum: 4.5, toward: "#000000"),
              dark: espressuReadable(dark, on: darkTextGrounds, minimum: 4.5, toward: "#FFFFFF"))
    }

    static var accentText: Color { readableInk(light: t.pixelBlueLight, dark: t.pixelBlueDark) }

    static var alertText: Color { readableInk(light: t.blushPinkLight, dark: t.blushPinkDark) }

    static func readable(_ light: KeyPath<EspressUThemeDefinition, String>,
                         _ dark: KeyPath<EspressUThemeDefinition, String>) -> Color {
        readableInk(light: t[keyPath: light], dark: t[keyPath: dark])
    }

    static var onAccent: Color {
        Color(light: inkOn(t.pixelBlueLight, cream: t.sparkleCreamLight),
              dark: inkOn(t.pixelBlueDark, cream: t.sparkleCreamDark))
    }

    private static func inkOn(_ fill: String, cream: String) -> String {
        espressuContrast(cream, fill) >= espressuContrast(t.brownDarkestLight, fill) ? cream : t.brownDarkestLight
    }

    static var cautionText: Color { readableInk(light: "#FF9500", dark: "#FF9F0A") }

    static var secondaryText: Color { readableInk(light: "#8A8A8E", dark: "#9C9CA3") }

    static var accentOnDark: Color {
        Color(hex: espressuReadable(t.pixelBlueDark, on: darkPanelGrounds, minimum: 4.5, toward: "#FFFFFF"))
    }

    static var alertOnDark: Color {
        Color(hex: espressuReadable(t.blushPinkDark, on: darkPanelGrounds, minimum: 4.5, toward: "#FFFFFF"))
    }

    private static var darkPanelGrounds: [String] { ["#000000", "#262626", "#404040"] }

    static var onCremaTop: Color {
        Color(light: onCremaTopHex(dark: false), dark: onCremaTopHex(dark: true))
    }

    private static func onCremaTopHex(dark: Bool) -> String {
        let top = dark ? t.cremaTopLightDark : t.cremaTopLightLight
        let bottom = dark ? t.cremaTopDarkDark : t.cremaTopDarkLight
        let grounds = [0.25, 0.5, 0.75].map { espressuMixHex(top, bottom, $0) }
        let cream = dark ? t.sparkleCreamDark : t.sparkleCreamLight
        let creamWorst = espressuWorstContrast(cream, on: grounds)
        if creamWorst >= 3.0 { return cream }
        let ink = espressuDeepen(t.cremaTopDarkLight, 0.78)
        return espressuWorstContrast(ink, on: grounds) > creamWorst ? ink : cream
    }

    static var formGround: Color { Color(UIColor.systemGroupedBackground) }

    private static func backgroundSamples(dark: Bool) -> [String] {
        let lightStops = t.backgroundStopsLight
        let darkStops = t.backgroundStopsDark
        if lightStops.count >= 2 && lightStops.count == darkStops.count {
            return dark ? darkStops : lightStops
        }
        let top = dark ? t.backgroundTopDark : t.backgroundTopLight
        let bottom = dark ? t.backgroundBottomDark : t.backgroundBottomLight
        return [0.0, 0.25, 0.5, 0.75, 1.0].map { espressuMixHex(top, bottom, $0) }
    }

    static var onBackground: Color {
        Color(light: onBackgroundHex(dark: false), dark: onBackgroundHex(dark: true))
    }

    static var onBackgroundMuted: Color {
        Color(light: onBackgroundMutedHex(dark: false), dark: onBackgroundMutedHex(dark: true))
    }

    static var onBackgroundAccent: Color {
        Color(light: onBackgroundAccentHex(dark: false), dark: onBackgroundAccentHex(dark: true))
    }

    private static func onBackgroundHex(dark: Bool) -> String {
        let samples = backgroundSamples(dark: dark)
        let ink = t.brownDarkestLight
        let cream = dark ? t.sparkleCreamDark : t.sparkleCreamLight
        let chosen = espressuWorstContrast(ink, on: samples) >= espressuWorstContrast(cream, on: samples) ? ink : cream
        return espressuReadable(chosen, on: samples, minimum: 4.5, toward: onBackgroundPole(chosen))
    }

    private static func onBackgroundMutedHex(dark: Bool) -> String {
        let samples = backgroundSamples(dark: dark)
        let primary = onBackgroundHex(dark: dark)
        let softened = espressuMixHex(primary, samples[samples.count / 2], 0.3)
        return espressuReadable(softened, on: samples, minimum: 4.5, toward: onBackgroundPole(primary))
    }

    private static func onBackgroundAccentHex(dark: Bool) -> String {
        let samples = backgroundSamples(dark: dark)
        let accent = dark ? t.pixelBlueDark : t.pixelBlueLight
        return espressuReadable(accent, on: samples, minimum: 3.0, toward: onBackgroundPole(onBackgroundHex(dark: dark)))
    }

    private static func onBackgroundPole(_ ink: String) -> String {
        espressuLuminance(ink) < 0.18 ? "#000000" : "#FFFFFF"
    }

    static var backgroundGradient: LinearGradient {
        let lightStops = t.backgroundStopsLight
        let darkStops = t.backgroundStopsDark
        if lightStops.count >= 2 && lightStops.count == darkStops.count {
            let colors = zip(lightStops, darkStops).map { Color(light: $0, dark: $1) }
            let locations = t.backgroundStopLocations
            if locations.count == colors.count {
                let stops = zip(colors, locations).map { Gradient.Stop(color: $0, location: $1) }
                return LinearGradient(gradient: Gradient(stops: stops), startPoint: .top, endPoint: .bottom)
            }
            return LinearGradient(colors: colors, startPoint: .top, endPoint: .bottom)
        }
        return LinearGradient(colors: [backgroundTop, backgroundBottom], startPoint: .topLeading, endPoint: .bottomTrailing)
    }

    static var cremaTopGradient: LinearGradient {
        LinearGradient(colors: [cremaTopLight, cremaTopDark], startPoint: .topLeading, endPoint: .bottomTrailing)
    }

    static var surfaceHighlight: Color {
        Color(light: espressuLift(t.wrapperLight, 0.70), dark: espressuLift(t.wrapperDark, 0.34))
    }

    static var surfaceShade: Color {
        Color(light: espressuDeepen(t.wrapperLight, 0.14), dark: espressuDeepen(t.wrapperDark, 0.40))
    }

    static var edgeStroke: LinearGradient {
        if UIStyle.isClassic {
            return LinearGradient(colors: [wrapper, wrapper], startPoint: .top, endPoint: .bottom)
        }
        return LinearGradient(
            gradient: Gradient(stops: [
                Gradient.Stop(color: surfaceHighlight, location: 0.0),
                Gradient.Stop(color: wrapper, location: 0.55),
                Gradient.Stop(color: surfaceShade, location: 1.0)
            ]),
            startPoint: .top, endPoint: .bottom)
    }

    static var controlEdgeStroke: LinearGradient {
        if UIStyle.isClassic {
            return LinearGradient(colors: [wrapper, wrapper], startPoint: .top, endPoint: .bottom)
        }
        return LinearGradient(
            colors: [
                Color(light: espressuLift(t.cremaTopLightLight, 0.42), dark: espressuLift(t.cremaTopLightDark, 0.34)),
                Color(light: espressuDeepen(t.cremaTopDarkLight, 0.20), dark: espressuDeepen(t.cremaTopDarkDark, 0.26))
            ],
            startPoint: .top, endPoint: .bottom)
    }

    static var cremaTopGradientPressed: LinearGradient {
        LinearGradient(
            colors: [
                Color(light: espressuDeepen(t.cremaTopLightLight, 0.10), dark: espressuDeepen(t.cremaTopLightDark, 0.10)),
                Color(light: espressuDeepen(t.cremaTopDarkLight, 0.10), dark: espressuDeepen(t.cremaTopDarkDark, 0.10))
            ],
            startPoint: .topLeading, endPoint: .bottomTrailing)
    }

    static var creamPressed: Color {
        Color(light: espressuDeepen(t.creamLight, 0.07), dark: espressuLift(t.creamDark, 0.10))
    }

    static var surfaceRaised: Color {
        Color(light: espressuMixHex(t.creamLight, t.wrapperLight, 0.45),
              dark: espressuMixHex(t.creamDark, t.wrapperDark, 0.55))
    }

    static var surfaceSunken: Color {
        Color(light: espressuDeepen(espressuMixHex(t.creamLight, t.wrapperLight, 0.8), 0.04),
              dark: espressuDeepen(t.creamDark, 0.35))
    }

    static var hairline: Color {
        Color(light: espressuMixHex(t.wrapperLight, t.brownMidLight, 0.22),
              dark: espressuMixHex(t.wrapperDark, t.brownMidDark, 0.18))
    }

    static var hairlineWidth: CGFloat {
        let scale = UITraitCollection.current.displayScale
        return scale > 0 ? 1.0 / scale : 0.5
    }

    static var scrim: Color {
        Color(light: t.shadowLight, lightAlpha: 0.26, dark: "#000000", darkAlpha: 0.48)
    }

    static var sheenHighlight: Color {
        Color(light: "#FFFFFF", lightAlpha: 0.32, dark: "#FFFFFF", darkAlpha: 0.075)
    }

    static var sheenOcclusion: Color {
        Color(light: "#000000", lightAlpha: 0.035, dark: "#000000", darkAlpha: 0.11)
    }

    static var surfaceSheen: LinearGradient {
        if UIStyle.glassDisabled {
            return LinearGradient(colors: [.clear, .clear], startPoint: .top, endPoint: .bottom)
        }
        return LinearGradient(
            gradient: Gradient(stops: [
                Gradient.Stop(color: sheenHighlight, location: 0.0),
                Gradient.Stop(color: .clear, location: 0.40),
                Gradient.Stop(color: .clear, location: 0.70),
                Gradient.Stop(color: sheenOcclusion, location: 1.0)
            ]),
            startPoint: .top, endPoint: .bottom)
    }

    static var controlSheen: LinearGradient {
        if UIStyle.glassDisabled {
            return LinearGradient(colors: [.clear, .clear], startPoint: .top, endPoint: .bottom)
        }
        return LinearGradient(
            gradient: Gradient(stops: [
                Gradient.Stop(color: sheenHighlight, location: 0.0),
                Gradient.Stop(color: .clear, location: 0.30),
                Gradient.Stop(color: .clear, location: 0.78),
                Gradient.Stop(color: sheenOcclusion, location: 1.0)
            ]),
            startPoint: .top, endPoint: .bottom)
    }

    enum Elevation {
        case flush
        case resting
        case raised
        case floating
        case overlay

        var contact: (radius: CGFloat, y: CGFloat, opacity: Double) {
            switch self {
            case .flush:    return (0, 0, 0)
            case .resting:  return (1.5, 1, 0.13)
            case .raised:   return (2, 1, 0.15)
            case .floating: return (3, 2, 0.17)
            case .overlay:  return (4, 2, 0.19)
            }
        }

        var ambient: (radius: CGFloat, y: CGFloat, opacity: Double) {
            switch self {
            case .flush:    return (0, 0, 0)
            case .resting:  return (9, 3, 0.10)
            case .raised:   return (15, 6, 0.12)
            case .floating: return (24, 11, 0.13)
            case .overlay:  return (36, 18, 0.17)
            }
        }
    }

    enum Font {
        static var display: SwiftUI.Font { .system(.title, design: .rounded).weight(.bold) }
        static var title: SwiftUI.Font { .system(.title2, design: .rounded).weight(.bold) }
        static var sectionTitle: SwiftUI.Font { .system(.headline, design: .rounded) }
        static var rowLabel: SwiftUI.Font { .system(.subheadline, design: .rounded).weight(.semibold) }
        static var rowValue: SwiftUI.Font { .system(.subheadline, design: .rounded) }
        static var caption: SwiftUI.Font { .system(.footnote, design: .rounded) }
        static var subCaption: SwiftUI.Font { .system(.caption) }
        static var micro: SwiftUI.Font { .system(.caption2) }
        static var primaryButton: SwiftUI.Font { .system(size: 14, weight: .bold, design: .rounded) }
        static var secondaryButton: SwiftUI.Font { .system(.footnote, design: .rounded).weight(.semibold) }
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
        static let chip: CGFloat = 12
        static let control: CGFloat = 14
        static let card: CGFloat = 18
        static let sheet: CGFloat = 28
    }

    enum Motion {
        static var pressDown: SwiftUI.Animation { .easeOut(duration: 0.07) }
        static var pressRelease: SwiftUI.Animation { .spring(response: 0.34, dampingFraction: 0.60, blendDuration: 0) }
        static var state: SwiftUI.Animation { .spring(response: 0.30, dampingFraction: 0.86, blendDuration: 0) }
        static var layout: SwiftUI.Animation { .spring(response: 0.42, dampingFraction: 0.90, blendDuration: 0) }

        static let pressScale: CGFloat = 0.97
        static let compactPressScale: CGFloat = 0.955

        static func press(isPressed: Bool, reduceMotion: Bool) -> SwiftUI.Animation {
            if reduceMotion { return .easeOut(duration: 0.12) }
            return isPressed ? pressDown : pressRelease
        }
    }
}

enum EspressUHaptics {
    static let isEnabled = true

    static func tap() {
        guard isEnabled else { return }
        EspressUHapticEngine.shared.tap()
    }

    static func select() {
        guard isEnabled else { return }
        EspressUHapticEngine.shared.select()
    }
}

private final class EspressUHapticEngine {
    static let shared = EspressUHapticEngine()

    private let impact = UIImpactFeedbackGenerator(style: .soft)
    private let selection = UISelectionFeedbackGenerator()

    private init() {
        impact.prepare()
        selection.prepare()
    }

    func tap() {
        impact.impactOccurred()
        impact.prepare()
    }

    func select() {
        selection.selectionChanged()
        selection.prepare()
    }
}

private struct EspressUElevationModifier: ViewModifier {
    let level: EspressUTheme.Elevation

    func body(content: Content) -> some View {
        let contact = level.contact
        let ambient = level.ambient
        if UIStyle.isClassic {
            return AnyView(content.shadow(color: EspressUTheme.shadow.opacity(0.18), radius: 10, x: 0, y: 4))
        }
        return AnyView(content
            .shadow(color: EspressUTheme.shadow.opacity(contact.opacity), radius: contact.radius, x: 0, y: contact.y)
            .shadow(color: EspressUTheme.shadow.opacity(ambient.opacity), radius: ambient.radius, x: 0, y: ambient.y))
    }
}

extension View {
    @ViewBuilder func espressuOpaqueNavigationBar(_ fill: Color = EspressUTheme.cream) -> some View {
        if #available(iOS 16.0, *) {
            self.toolbarBackground(fill, for: .navigationBar)
                .toolbarBackground(.visible, for: .navigationBar)
        } else {
            self
        }
    }

    func espressuElevation(_ level: EspressUTheme.Elevation) -> some View {
        modifier(EspressUElevationModifier(level: level))
    }

    func espressuScreenBackground() -> some View {
        background(EspressUTheme.backgroundGradient.ignoresSafeArea())
    }

    func espressuRowLabel() -> some View {
        font(EspressUTheme.Font.rowLabel).foregroundColor(EspressUTheme.brownDarkest)
    }

    func espressuRowValue() -> some View {
        font(EspressUTheme.Font.rowValue).foregroundColor(EspressUTheme.brownMid)
    }

    func espressuSectionTitle() -> some View {
        font(EspressUTheme.Font.sectionTitle).foregroundColor(EspressUTheme.brownDark)
    }

    func espressuCaption() -> some View {
        font(EspressUTheme.Font.caption).foregroundColor(EspressUTheme.brownMid)
    }

    func espressuSubCaption() -> some View {
        font(EspressUTheme.Font.subCaption).foregroundColor(EspressUTheme.secondaryText)
    }
}

struct EspressUCard<Content: View>: View {
    var cornerRadius: CGFloat = 18
    var fill: Color = EspressUTheme.cream
    @ViewBuilder var content: Content

    private var shape: RoundedRectangle {
        RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
    }

    var body: some View {
        content
            .background(fill.overlay(EspressUTheme.surfaceSheen))
            .clipShape(shape)
            .overlay(shape.strokeBorder(EspressUTheme.edgeStroke, lineWidth: 1))
            .espressuElevation(.resting)
    }
}

struct EspressUPrimaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        StyleBody(configuration: configuration)
    }

    private struct StyleBody: View {
        let configuration: ButtonStyleConfiguration
        @Environment(\.accessibilityReduceMotion) private var reduceMotion
        @Environment(\.isEnabled) private var isEnabled

        private var shape: RoundedRectangle {
            RoundedRectangle(cornerRadius: EspressUTheme.Radius.control, style: .continuous)
        }

        var body: some View {
            let pressed = configuration.isPressed
            if UIStyle.isClassic {
                return AnyView(configuration.label
                    .font(.system(size: 14, weight: .bold, design: .rounded))
                    .foregroundColor(EspressUTheme.onCremaTop)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(EspressUTheme.cremaTopGradient)
                    .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                    .shadow(color: EspressUTheme.shadow.opacity(0.25), radius: pressed ? 2 : 6, x: 0, y: pressed ? 1 : 3)
                    .scaleEffect(pressed ? 0.97 : 1.0)
                    .animation(.easeOut(duration: 0.12), value: pressed))
            }
            let fill = (pressed ? EspressUTheme.cremaTopGradientPressed : EspressUTheme.cremaTopGradient)
                .overlay(EspressUTheme.controlSheen)
            return AnyView(configuration.label
                .font(EspressUTheme.Font.primaryButton)
                .foregroundColor(EspressUTheme.onCremaTop)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(fill)
                .clipShape(shape)
                .overlay(shape.strokeBorder(EspressUTheme.controlEdgeStroke, lineWidth: 1))
                .espressuElevation(pressed ? .flush : .raised)
                .opacity(isEnabled ? 1 : 0.45)
                .scaleEffect(pressed ? EspressUTheme.Motion.pressScale : 1)
                .animation(EspressUTheme.Motion.press(isPressed: pressed, reduceMotion: reduceMotion), value: pressed)
                .onValueChange(of: pressed) { nowPressed in
                    if nowPressed { EspressUHaptics.tap() }
                })
        }
    }
}

struct EspressUSecondaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        StyleBody(configuration: configuration)
    }

    private struct StyleBody: View {
        let configuration: ButtonStyleConfiguration
        @Environment(\.accessibilityReduceMotion) private var reduceMotion
        @Environment(\.isEnabled) private var isEnabled

        private var shape: RoundedRectangle {
            RoundedRectangle(cornerRadius: EspressUTheme.Radius.chip, style: .continuous)
        }

        var body: some View {
            let pressed = configuration.isPressed
            if UIStyle.isClassic {
                return AnyView(configuration.label
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDark)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(EspressUTheme.cream.opacity(pressed ? 0.7 : 1.0))
                    .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .stroke(EspressUTheme.wrapper, lineWidth: 1)))
            }
            let fill = (pressed ? EspressUTheme.creamPressed : EspressUTheme.cream)
                .overlay(EspressUTheme.controlSheen)
            return AnyView(configuration.label
                .font(EspressUTheme.Font.secondaryButton)
                .foregroundColor(EspressUTheme.brownDark)
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(fill)
                .clipShape(shape)
                .overlay(shape.strokeBorder(EspressUTheme.edgeStroke, lineWidth: 1))
                .espressuElevation(pressed ? .flush : .resting)
                .opacity(isEnabled ? 1 : 0.45)
                .scaleEffect(pressed ? EspressUTheme.Motion.compactPressScale : 1)
                .animation(EspressUTheme.Motion.press(isPressed: pressed, reduceMotion: reduceMotion), value: pressed)
                .onValueChange(of: pressed) { nowPressed in
                    if nowPressed { EspressUHaptics.tap() }
                })
        }
    }
}
