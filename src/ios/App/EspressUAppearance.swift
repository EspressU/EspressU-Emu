// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

enum EspressUAppearance {
    static let storageKey = "espressu.appearance"

    static var mode: String {
        UserDefaults.standard.string(forKey: storageKey) ?? "system"
    }

    static var colorScheme: ColorScheme? {
        switch mode {
        case "light": return .light
        case "dark": return .dark
        default: return nil
        }
    }

    @MainActor static func applyToWindows() {
        let style: UIUserInterfaceStyle
        switch mode {
        case "light": style = .light
        case "dark": style = .dark
        default: style = .unspecified
        }
        for scene in UIApplication.shared.connectedScenes {
            guard let windowScene = scene as? UIWindowScene else { continue }
            for window in windowScene.windows { window.overrideUserInterfaceStyle = style }
        }
    }
}

extension EspressUThemePresets {
    static let espressU = EspressUThemeDefinition(
        id: "espressu", name: "EspressU", iconId: "original",
        backgroundTopLight: "#F4F7F9", backgroundTopDark: "#12161B",
        backgroundBottomLight: "#E3E9EE", backgroundBottomDark: "#08090B",
        cremaTopLightLight: "#3FC4EE", cremaTopLightDark: "#2AB4E2",
        cremaTopDarkLight: "#0F94C4", cremaTopDarkDark: "#0B7FAA",
        creamLight: "#FFFFFF", creamDark: "#171B21",
        wrapperLight: "#EEF2F5", wrapperDark: "#222831",
        blueberryNavyLight: "#1F3A5A", blueberryNavyDark: "#8FB4DA",
        pixelBlueLight: "#3A3A3C", pixelBlueDark: "#E5E5EA",
        blushPinkLight: "#E5484D", blushPinkDark: "#FF6B70",
        brownDarkestLight: "#1B232B", brownDarkestDark: "#F2F5F8",
        brownDarkLight: "#34404C", brownDarkDark: "#D5DCE3",
        brownMidLight: "#5D6B78", brownMidDark: "#9AA6B2",
        sparkleCreamLight: "#FFFFFF", sparkleCreamDark: "#FFFFFF",
        shadowLight: "#2B3640", shadowDark: "#000000"
    )
}
