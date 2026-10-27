// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.

import CoreGraphics
import Foundation

/// Every GamePad input Crema can press.
enum CremaKey: CaseIterable, Hashable {
    case a, b, x, y
    case l, r, zl, zr
    case plus, minus
    case up, down, left, right
    case stickL, stickR
    case home

    var label: String {
        switch self {
        case .a: return "A"
        case .b: return "B"
        case .x: return "X"
        case .y: return "Y"
        case .l: return "L"
        case .r: return "R"
        case .zl: return "ZL"
        case .zr: return "ZR"
        case .plus: return "plus"
        case .minus: return "minus"
        case .up: return "up"
        case .down: return "down"
        case .left: return "left"
        case .right: return "right"
        case .stickL: return "L3"
        case .stickR: return "R3"
        case .home: return "HOME"
        }
    }

    var bridgeButton: CemuBridgeButton {
        switch self {
        case .a: return CEMU_BRIDGE_BUTTON_A
        case .b: return CEMU_BRIDGE_BUTTON_B
        case .x: return CEMU_BRIDGE_BUTTON_X
        case .y: return CEMU_BRIDGE_BUTTON_Y
        case .l: return CEMU_BRIDGE_BUTTON_L
        case .r: return CEMU_BRIDGE_BUTTON_R
        case .zl: return CEMU_BRIDGE_BUTTON_ZL
        case .zr: return CEMU_BRIDGE_BUTTON_ZR
        case .plus: return CEMU_BRIDGE_BUTTON_PLUS
        case .minus: return CEMU_BRIDGE_BUTTON_MINUS
        case .up: return CEMU_BRIDGE_BUTTON_UP
        case .down: return CEMU_BRIDGE_BUTTON_DOWN
        case .left: return CEMU_BRIDGE_BUTTON_LEFT
        case .right: return CEMU_BRIDGE_BUTTON_RIGHT
        case .stickL: return CEMU_BRIDGE_BUTTON_STICK_L
        case .stickR: return CEMU_BRIDGE_BUTTON_STICK_R
        case .home: return CEMU_BRIDGE_BUTTON_HOME
        }
    }
}

enum CremaSide {
    case left, right

    var stickKey: CremaKey { self == .left ? .stickL : .stickR }
    var bridgeStick: CemuBridgeStick { self == .left ? CEMU_BRIDGE_STICK_LEFT : CEMU_BRIDGE_STICK_RIGHT }
}

/// Turns a finger offset on a stick ring into an axis value: a dead centre, then a linear ramp to full
/// deflection at the rim. The result is a unit disc with y pointing up, which is what the bridge expects.
enum CremaStickMath {
    static func axis(offset: CGPoint, radius: CGFloat, deadzone: Double) -> CGPoint {
        guard radius > 0 else { return .zero }
        let length = hypot(offset.x, offset.y)
        let raw = Double(length / radius)
        let dead = min(max(deadzone, 0), 0.9)
        guard raw > dead else { return .zero }
        let strength = min((raw - dead) / (1 - dead), 1)
        let ux = Double(offset.x / length)
        let uy = Double(offset.y / length)
        return CGPoint(x: strength * ux, y: -strength * uy)
    }

    /// Where the knob is drawn: the finger position, held inside the ring.
    static func knob(offset: CGPoint, radius: CGFloat) -> CGPoint {
        let length = hypot(offset.x, offset.y)
        guard length > radius, length > 0 else { return offset }
        let k = radius / length
        return CGPoint(x: offset.x * k, y: offset.y * k)
    }
}

/// Sends Crema's input to the emulator core.
@MainActor
final class CremaOutput {
    static let shared = CremaOutput()
    private init() {}

    func setKey(_ key: CremaKey, pressed: Bool) {
        PadDiagnostics.shared.recordInput(key.label, pressed)
        if key == .home {
            HomeMenuRouter.shared.padHome(pressed: pressed)
            return
        }
        cemu_bridge_set_button_state(key.bridgeButton, pressed)
    }

    func setStick(_ side: CremaSide, axis: CGPoint) {
        PadDiagnostics.shared.recordStick(side == .left ? 0 : 1, axis)
        cemu_bridge_set_stick_axis(side.bridgeStick, Float(axis.x), Float(axis.y))
    }

    /// `point` is in pixels of the GamePad surface, or nil when the finger lifts.
    func setTouchscreen(_ point: CGPoint?) {
        if let point {
            cemu_bridge_set_pad_touch(Double(point.x), Double(point.y), true)
        } else {
            cemu_bridge_set_pad_touch(0, 0, false)
        }
    }

    func releaseAll() {
        cemu_bridge_release_all_buttons()
        cemu_bridge_set_stick_axis(CEMU_BRIDGE_STICK_LEFT, 0, 0)
        cemu_bridge_set_stick_axis(CEMU_BRIDGE_STICK_RIGHT, 0, 0)
        cemu_bridge_set_pad_touch(0, 0, false)
    }
}
