import Foundation
#if os(iOS)
import UIKit
#endif

struct DeviceCapabilities {

    enum Tier: Int {
        case low = 0, standard = 1, high = 2
        var name: String { self == .low ? "low" : (self == .high ? "high" : "standard") }
    }

    enum ScreenClass: Int {
        case unknown = 0, phoneCompact = 1, phone = 2, pad = 3, padLarge = 4
        var isPhone: Bool { self == .phoneCompact || self == .phone }
    }

    let raw: CemuDeviceCaps
    let machine: String
    let chip: String
    let line: String

    var tier: Tier { Tier(rawValue: Int(raw.tier)) ?? .standard }
    var screenClass: ScreenClass { ScreenClass(rawValue: Int(raw.screenClass)) ?? .unknown }
    var isPhone: Bool { !raw.isPad }
    var isHighEndSoC: Bool { raw.isHighEndSoc }
    var multicoreViable: Bool { raw.multicoreViable }
    var meshShaders: Bool { raw.meshShaders }
    static let oneCoreOnlyText = "One core only: this device has too little memory or too few cores to run three at once."

    var startsAtHighRenderScale: Bool { raw.isHighEndSoc && tier == .high }

    static let current: DeviceCapabilities = {
        #if os(iOS)
        let bounds = UIScreen.main.bounds
        cemu_device_caps_set_screen(
            Double(min(bounds.width, bounds.height)),
            Double(max(bounds.width, bounds.height)),
            Double(UIScreen.main.nativeScale),
            UIDevice.current.userInterfaceIdiom == .pad)
        #endif
        cemu_device_caps_initialize()
        var caps = CemuDeviceCaps()
        cemu_device_caps_get(&caps)
        return DeviceCapabilities(
            raw: caps,
            machine: String(cString: cemu_device_caps_machine()),
            chip: String(cString: cemu_device_caps_chip()),
            line: String(cString: cemu_device_caps_line()))
    }()

    static func bootstrap() {
        cemu_bridge_log_checkpoint(current.line)
    }
}
