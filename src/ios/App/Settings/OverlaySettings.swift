import SwiftUI
import UIKit

enum OverlaySettings {
    static let positionKey = "espressu.overlay.position"
    static let defaultPosition = ScreenPosition.disabled

    static let textColorKey = "espressu.overlay.textColor"
    static let defaultTextColor: Int = 0xFFFFFFFF

    static let textScaleKey = "espressu.overlay.textScale"
    static let defaultTextScale = 100

    static let fpsKey = "espressu.overlay.fps"
    static let defaultFps = true

    static let showFrameGenBreakdownKey = "espressu.overlay.showFrameGenBreakdown"
    static let defaultShowFrameGenBreakdown = false

    static let drawcallsKey = "espressu.overlay.drawcalls"
    static let defaultDrawcalls = false

    static let cpuUsageKey = "espressu.overlay.cpuUsage"
    static let defaultCpuUsage = false

    static let cpuPerCoreUsageKey = "espressu.overlay.cpuPerCoreUsage"
    static let defaultCpuPerCoreUsage = false

    static let ramUsageKey = "espressu.overlay.ramUsage"
    static let defaultRamUsage = true

    static let vramUsageKey = "espressu.overlay.vramUsage"
    static let defaultVramUsage = false

    static let debugKey = "espressu.overlay.debug"
    static let defaultDebug = true

    static func applyFrameGenBreakdownToBridge() {
        let enabled = UserDefaults.standard.object(forKey: showFrameGenBreakdownKey) as? Bool
            ?? defaultShowFrameGenBreakdown
        cemu_bridge_set_overlay_show_framegen_breakdown(enabled)
    }
}

func packedColourBinding(_ value: Binding<Int>) -> Binding<Color> {
    Binding {
        let packed = UInt32(truncatingIfNeeded: value.wrappedValue)
        return Color(.sRGB,
                     red: Double((packed >> 16) & 0xFF) / 255,
                     green: Double((packed >> 8) & 0xFF) / 255,
                     blue: Double(packed & 0xFF) / 255,
                     opacity: 1)
    } set: { newValue in
        var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
        guard UIColor(newValue).getRed(&red, green: &green, blue: &blue, alpha: &alpha) else { return }
        func byte(_ component: CGFloat) -> UInt32 { UInt32(max(0, min(1, component)) * 255 + 0.5) }
        value.wrappedValue = Int(0xFF000000 | (byte(red) << 16) | (byte(green) << 8) | byte(blue))
    }
}
