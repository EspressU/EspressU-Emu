// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
#if os(iOS)
import UIKit
#endif

struct ScreenEmptyState: View {
    let systemImage: String
    let headline: String
    let message: String
    var actionTitle: String?
    var action: (() -> Void)?

    var body: some View {
        if UIStyle.isClassic {
            return AnyView(
                VStack(alignment: .leading, spacing: 4) {
                    Text(headline)
                        .font(.system(size: 13, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                    Text(message)
                        .font(.system(size: 13, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                }
                .fixedSize(horizontal: false, vertical: true)
            )
        }
        return AnyView(modernBody)
    }

    private var modernBody: some View {
        VStack(spacing: 14) {
            ZStack {
                Circle()
                    .fill(EspressUTheme.wrapper)
                Image(systemName: systemImage)
                    .font(.system(size: 26, weight: .semibold))
                    .foregroundColor(LegibleInk.ensure(EspressUTheme.pixelBlue, on: EspressUTheme.wrapper,
                                                       minimum: LegibleInk.glyph))
            }
            .frame(width: 64, height: 64)
            .accessibilityHidden(true)

            Text(headline)
                .font(.system(size: 17, weight: .bold, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityAddTraits(.isHeader)

            Text(message)
                .font(.system(size: 13, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)

            if let actionTitle, let action {
                Button(action: action) {
                    Text(actionTitle)
                }
                .buttonStyle(EspressUPrimaryButtonStyle())
                .padding(.top, 2)
            }
        }
        .frame(maxWidth: 380)
        .frame(maxWidth: .infinity)
        .padding(.vertical, 28)
    }
}

struct ScreenStatusCallout: View {
    enum Tone {
        case info
        case warning

        var symbol: String {
            switch self {
            case .info: return "info.circle.fill"
            case .warning: return "exclamationmark.triangle.fill"
            }
        }

        var accent: Color {
            switch self {
            case .info: return EspressUTheme.pixelBlue
            case .warning: return EspressUTheme.blushPink
            }
        }
    }

    let tone: Tone
    let message: String

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: tone.symbol)
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(tone.accent)
                .accessibilityHidden(true)

            Text(message)
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.vertical, 4)
    }
}

struct ScreenSlotBadge: View {
    let label: String
    let isFilled: Bool

    var body: some View {
        ZStack {
            Circle()
                .fill(isFilled ? EspressUTheme.pixelBlue : EspressUTheme.wrapper)
            if isFilled {
                Text(label)
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .foregroundColor(LegibleInk.on(EspressUTheme.pixelBlue, light: EspressUTheme.sparkleCream))
            } else {
                Text(label)
                    .font(.system(size: 13, weight: .bold, design: .rounded))
                    .foregroundColor(LegibleInk.ensure(EspressUTheme.brownMid, on: EspressUTheme.wrapper))
            }
        }
        .frame(width: 30, height: 30)
        .accessibilityHidden(true)
    }
}

struct ScreenChip: View {
    let text: String
    var isMuted = true

    var body: some View {
        if UIStyle.isClassic {
            return AnyView(
                Text(text)
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            )
        }
        return AnyView(modernBody)
    }

    private var modernBody: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold, design: .rounded))
            .foregroundColor(isMuted
                             ? LegibleInk.ensure(EspressUTheme.brownMid, on: EspressUTheme.wrapper)
                             : LegibleInk.on(EspressUTheme.pixelBlue, light: EspressUTheme.sparkleCream))
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(
                Capsule().fill(isMuted ? EspressUTheme.wrapper : EspressUTheme.pixelBlue)
            )
    }
}

struct ScreenCardButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.975 : 1.0)
            .opacity(configuration.isPressed ? 0.88 : 1.0)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}

struct ScreenRowActionStyle: ButtonStyle {
    var isDestructive = false
    var isProminent = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .screenRowActionChrome(isDestructive: isDestructive,
                                  isProminent: isProminent,
                                  isPressed: configuration.isPressed)
            .animation(.easeOut(duration: 0.1), value: configuration.isPressed)
    }
}

extension View {
    func screenRowActionChrome(isDestructive: Bool = false,
                              isProminent: Bool = false,
                              isPressed: Bool = false) -> some View {
        let foreground: Color = isProminent
            ? LegibleInk.on(EspressUTheme.pixelBlue, light: EspressUTheme.sparkleCream)
            : LegibleInk.ensure(isDestructive ? EspressUTheme.blushPink : EspressUTheme.pixelBlue,
                                on: EspressUTheme.wrapper)

        return self
            .font(.system(size: 13, weight: .semibold, design: .rounded))
            .foregroundColor(foreground)
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
            .background(
                Capsule()
                    .fill(isProminent ? EspressUTheme.pixelBlue : EspressUTheme.wrapper)
                    .opacity(isPressed ? 0.65 : 1.0)
            )
            .contentShape(Capsule())
    }
}

enum LegibleInk {
    static let text: CGFloat = 4.5
    static let glyph: CGFloat = 3.0

    static func on(_ fill: Color, light: Color = .white, dark: Color = Color(white: 0.12)) -> Color {
        let fill = UIColor(fill), light = UIColor(light), dark = UIColor(dark)
        return Color(uiColor: UIColor { traits in
            let bg = opaque(fill.resolvedColor(with: traits))
            let l = light.resolvedColor(with: traits), d = dark.resolvedColor(with: traits)
            return contrast(l, bg) >= contrast(d, bg) ? l : d
        })
    }

    static func ensure(_ ink: Color, on background: Color, minimum: CGFloat = text) -> Color {
        let ink = UIColor(ink), background = UIColor(background)
        return Color(uiColor: UIColor { traits in
            adjusted(ink.resolvedColor(with: traits),
                     on: opaque(background.resolvedColor(with: traits)), minimum: minimum)
        })
    }

    static func contrast(_ a: UIColor, _ b: UIColor) -> CGFloat {
        let la = luminance(a), lb = luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    private static func adjusted(_ ink: UIColor, on bg: UIColor, minimum: CGFloat) -> UIColor {
        if contrast(ink, bg) >= minimum { return ink }
        var best: (step: Int, colour: UIColor)?
        for target in [UIColor.black, UIColor.white] {
            for step in 1...20 {
                let candidate = mix(ink, target, CGFloat(step) / 20)
                if contrast(candidate, bg) >= minimum {
                    if step < (best?.step ?? .max) { best = (step, candidate) }
                    break
                }
            }
        }
        return best?.colour ?? (contrast(.white, bg) >= contrast(.black, bg) ? .white : .black)
    }

    private static func components(_ c: UIColor) -> (r: CGFloat, g: CGFloat, b: CGFloat, a: CGFloat) {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 1
        if !c.getRed(&r, green: &g, blue: &b, alpha: &a) {
            var w: CGFloat = 0
            if c.getWhite(&w, alpha: &a) { r = w; g = w; b = w }
        }
        func unit(_ v: CGFloat) -> CGFloat { min(max(v, 0), 1) }
        return (unit(r), unit(g), unit(b), unit(a))
    }

    private static func opaque(_ c: UIColor) -> UIColor {
        let x = components(c)
        guard x.a < 1 else { return c }
        let grey = 0.5 * (1 - x.a)
        return UIColor(red: x.r * x.a + grey, green: x.g * x.a + grey, blue: x.b * x.a + grey, alpha: 1)
    }

    private static func mix(_ a: UIColor, _ b: UIColor, _ k: CGFloat) -> UIColor {
        let x = components(a), y = components(b)
        return UIColor(red: x.r + (y.r - x.r) * k, green: x.g + (y.g - x.g) * k,
                       blue: x.b + (y.b - x.b) * k, alpha: x.a)
    }

    private static func luminance(_ c: UIColor) -> CGFloat {
        let x = components(c)
        func linear(_ v: CGFloat) -> Double {
            let v = Double(v)
            return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4)
        }
        return CGFloat(0.2126 * linear(x.r) + 0.7152 * linear(x.g) + 0.0722 * linear(x.b))
    }
}

enum ScreenHaptics {
    static func selectionChanged() {
        #if os(iOS)
        UISelectionFeedbackGenerator().selectionChanged()
        #endif
    }

    static func rejected() {
        #if os(iOS)
        UINotificationFeedbackGenerator().notificationOccurred(.warning)
        #endif
    }
}
