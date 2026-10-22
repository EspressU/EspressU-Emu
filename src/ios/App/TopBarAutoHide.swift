// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

enum TopBarAutoHide {
    static let enabledKey = "espressu.topBar.autoHide"
    /// Phones fade the buttons out by default to keep the small screen clear; iPads keep them visible.
    static var defaultEnabled: Bool { UIDevice.current.userInterfaceIdiom == .phone }

    static let hideDelayKey = "espressu.topBar.hideDelaySeconds"
    static let defaultHideDelaySeconds = 4
    static let hideDelayChoices = [2, 4, 8]

    static var hideDelayNanoseconds: UInt64 {
        let stored = UserDefaults.standard.object(forKey: hideDelayKey) as? Int ?? defaultHideDelaySeconds
        let seconds = hideDelayChoices.contains(stored) ? stored : defaultHideDelaySeconds
        return UInt64(seconds) * 1_000_000_000
    }

    static let handleSizeKey = "espressu.topBar.handleSize"
    static let defaultHandleSize = HandleSize.normal

    enum HandleSize: String, CaseIterable, Identifiable {
        case normal
        case large

        var id: String { rawValue }

        var title: String {
            switch self {
            case .normal: return "Normal"
            case .large:  return "Large"
            }
        }

        var width: CGFloat { self == .large ? 260 : 170 }
        var height: CGFloat { self == .large ? 80 : 60 }
    }
}

struct TopBarHidingEffect: ViewModifier {
    let hidden: Bool
    let slideDistance: CGFloat
    let slides: Bool

    func body(content: Content) -> some View {
        content
            .offset(y: hidden && slides ? -(slideDistance + 8) : 0)
            .opacity(hidden ? 0 : 1)
            .allowsHitTesting(!hidden)
            .accessibilityHidden(hidden)
    }
}

struct TopBarRevealHandle: View {
    let onReveal: () -> Void
    @AppStorage(TopBarAutoHide.handleSizeKey) private var handleSizeRaw = TopBarAutoHide.defaultHandleSize.rawValue

    private var size: TopBarAutoHide.HandleSize {
        TopBarAutoHide.HandleSize(rawValue: handleSizeRaw) ?? TopBarAutoHide.defaultHandleSize
    }

    var body: some View {
        Capsule()
            .fill(Color.white.opacity(0.7))
            .shadow(color: Color.black.opacity(0.5), radius: 2, x: 0, y: 0.5)
            .frame(width: size == .large ? 120 : 90, height: size == .large ? 10 : 8)
            .padding(.top, 10)
            .frame(width: size.width, height: size.height, alignment: .top)
            .contentShape(Rectangle())
            .onTapGesture { onReveal() }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Show top bar")
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { onReveal() }
    }
}

struct EspressUBarButtonStyle: ButtonStyle {
    var highlighted = false

    func makeBody(configuration: Configuration) -> some View {
        EspressUSecondaryButtonStyle().makeBody(configuration: configuration)
            .overlay(tint)
            .frame(minWidth: 44, minHeight: 44)
            .contentShape(Rectangle())
    }

    private var tint: some View {
        let shape = RoundedRectangle(cornerRadius: EspressUTheme.Radius.chip, style: .continuous)
        return shape
            .fill(EspressUTheme.pixelBlue.opacity(highlighted ? 0.28 : 0))
            .overlay(shape.strokeBorder(EspressUTheme.pixelBlue.opacity(highlighted ? 0.9 : 0), lineWidth: 1.5))
            .allowsHitTesting(false)
    }
}

extension View {
    func topBarAutoHideEffect(hidden: Bool, slideDistance: CGFloat, slides: Bool) -> some View {
        modifier(TopBarHidingEffect(hidden: hidden, slideDistance: slideDistance, slides: slides))
    }
}
