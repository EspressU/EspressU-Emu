// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import TouchLabCore
import TouchLabUI

enum TouchLabSettings {
    /// The on-screen controls have a single layout. Older builds let players pick between several styles;
    /// whatever was stored under the old style keys is simply ignored now.
    static let schemeID = ZonePad.schemeInfo.id
}

@MainActor
final class CemuBridgePadOutput: @preconcurrency PadOutput {
    static let shared = CemuBridgePadOutput()

    var gamepadViewSize: CGSize = .zero

    func setButton(_ button: PadButton, pressed: Bool) {
        PadDiagnostics.shared.recordInput(Self.label(button), pressed)
        if button == .home {
            HomeMenuRouter.shared.padHome(pressed: pressed)
            return
        }
        cemu_bridge_set_button_state(Self.bridgeButton(button), pressed)
    }

    func setStick(_ stick: PadStick, _ value: StickValue) {
        PadDiagnostics.shared.recordStick(stick.rawValue, CGPoint(x: value.x, y: value.y))
        cemu_bridge_set_stick_axis(stick == .left ? CEMU_BRIDGE_STICK_LEFT : CEMU_BRIDGE_STICK_RIGHT,
                                   Float(value.x), Float(value.y))
    }

    private var lastTouchPixel: (x: Double, y: Double)?

    func setTouchscreen(_ point: CGPoint?) {
        guard let point, gamepadViewSize.width > 0, gamepadViewSize.height > 0 else {
            if let last = lastTouchPixel {
                cemu_bridge_set_pad_touch(last.x, last.y, false)
                lastTouchPixel = nil
            } else {
                cemu_bridge_set_pad_touch(0, 0, false)
            }
            return
        }
        let scale = DisplayRouter.shared.padSurfaceScale
        let x = Double(point.x * gamepadViewSize.width) * scale
        let y = Double(point.y * gamepadViewSize.height) * scale
        lastTouchPixel = (x, y)
        cemu_bridge_set_pad_touch(x, y, true)
    }

    func releaseAll() {
        cemu_bridge_release_all_buttons()
        lastTouchPixel = nil
        cemu_bridge_set_pad_touch(0, 0, false)
    }

    static func bridgeButton(_ b: PadButton) -> CemuBridgeButton {
        switch b {
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

    static func label(_ b: PadButton) -> String {
        switch b {
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
}

struct TouchLabPadOverlay: View {
    let gameID: String?
    let screens: TouchLabScreenState
    let enabled: Bool
    let topInset: CGFloat

    @AppStorage(ControllerLayoutSettings.scaleKey) private var scale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.stickSpacingKey) private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey) private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(ControllerLayoutSettings.opacityKey) private var opacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.hapticsKey) private var haptics = ControllerLayoutSettings.defaultHaptics
    @AppStorage(ControllerLayoutSettings.deadzoneKey) private var deadzone = ControllerLayoutSettings.defaultDeadzone

    var body: some View {
        TouchPad(schemeID: TouchLabSettings.schemeID,
                 output: CemuBridgePadOutput.shared,
                 touchscreenRect: screens.screens.touchscreenRect,
                 videoRects: screens.screens.videoRects,
                 scale: scale,
                 stickSpacing: stickSpacing,
                 shoulderOffset: CGFloat(ControllerLayoutSettings.effectiveShoulderOffset(shoulderOffset)),
                 opacity: opacity,
                 haptics: haptics,
                 revision: revision,
                 enabled: enabled,
                 stickTuning: StickTuning(deadzone: deadzone, curve: ControllerLayoutSettings.minStickCurve,
                                          gate: .round),
                 rectSpace: .window,
                 extraInsets: Insets(top: topInset),
                 makeScheme: makeScheme)
            .ignoresSafeArea()
            .onAppear {
                syncGamepadSize()
            }
            .onValueChange(of: screens) { _ in syncGamepadSize() }
    }

    private var revision: Int {
        var h = Hasher()
        h.combine(gameID)
        return h.finalize()
    }

    private func syncGamepadSize() {
        CemuBridgePadOutput.shared.gamepadViewSize = screens.screens.touchscreenRect?.size ?? .zero
    }

    private func makeScheme(_ id: String) -> TouchScheme {
        ZonePad(aScale: 1)
    }
}

struct TouchLabScreenState: Equatable {
    fileprivate var screens = TouchLabScreens(frames: [:])
    init() {}
}

extension View {
    func touchLabTVScreen() -> some View { touchLabScreenFrame(.tv) }

    func touchLabGamePadScreen() -> some View { touchLabScreenFrame(.gamepad) }

    func trackTouchLabScreens(_ screens: Binding<TouchLabScreenState>,
                              imageIsAspectFit: @escaping () -> Bool) -> some View {
        onPreferenceChange(TouchLabScreenFramesKey.self) { frames in
            var next = TouchLabScreenState()
            next.screens = TouchLabScreens(frames: frames, imageIsAspectFit: imageIsAspectFit())
            if next != screens.wrappedValue { screens.wrappedValue = next }
        }
    }
}
