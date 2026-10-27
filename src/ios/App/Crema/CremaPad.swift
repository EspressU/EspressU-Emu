// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.

import SwiftUI

/// Where the GamePad picture is on screen, so touches on it can reach the Wii U touchscreen.
struct CremaScreenState: Equatable {
    var gamepad: CGRect?
}

private struct CremaGamePadFrameKey: PreferenceKey {
    static var defaultValue: CGRect?
    static func reduce(value: inout CGRect?, nextValue: () -> CGRect?) {
        value = nextValue() ?? value
    }
}

extension View {
    /// Marks this view as the GamePad picture.
    func cremaGamePadScreen() -> some View {
        background(
            GeometryReader { proxy in
                Color.clear.preference(key: CremaGamePadFrameKey.self, value: proxy.frame(in: .global))
            }
        )
    }

    /// Keeps `state` up to date with the GamePad picture marked inside this view.
    func trackCremaScreens(_ state: Binding<CremaScreenState>) -> some View {
        onPreferenceChange(CremaGamePadFrameKey.self) { frame in
            let next = CremaScreenState(gamepad: frame)
            if next != state.wrappedValue { state.wrappedValue = next }
        }
    }
}

/// Crema laid over the game picture. Reads the player's settings and hands them to the touch view.
struct CremaPad: View {
    let screens: CremaScreenState
    let enabled: Bool
    let topInset: CGFloat

    @AppStorage(ControllerLayoutSettings.scaleKey) private var scale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.stickSpacingKey) private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey) private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(ControllerLayoutSettings.opacityKey) private var opacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.hapticsKey) private var haptics = ControllerLayoutSettings.defaultHaptics
    @AppStorage(ControllerLayoutSettings.deadzoneKey) private var deadzone = ControllerLayoutSettings.defaultDeadzone

    var body: some View {
        CremaRepresentable(
            settings: CremaSettings(
                scale: scale, stickSpacing: stickSpacing,
                shoulderOffset: ControllerLayoutSettings.effectiveShoulderOffset(shoulderOffset),
                deadzone: deadzone),
            opacity: opacity, haptics: haptics, enabled: enabled, topInset: topInset,
            touchscreenFrame: screens.gamepad)
            .ignoresSafeArea()
    }
}

private struct CremaRepresentable: UIViewRepresentable {
    let settings: CremaSettings
    let opacity: Double
    let haptics: Bool
    let enabled: Bool
    let topInset: CGFloat
    let touchscreenFrame: CGRect?

    func makeUIView(context: Context) -> CremaSurfaceView { CremaSurfaceView() }

    func updateUIView(_ view: CremaSurfaceView, context: Context) {
        view.settings = settings
        view.padOpacity = CGFloat(opacity)
        view.hapticsEnabled = haptics
        view.topInset = topInset
        view.touchscreenFrame = touchscreenFrame
        view.isActive = enabled
    }

    static func dismantleUIView(_ view: CremaSurfaceView, coordinator: ()) {
        view.releaseEverything()
    }
}
