// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.

import SwiftUI

/// The sheet shown while the player adjusts the on-screen controls. The controls behind it update live.
struct CremaLayoutPanel: View {
    let onDone: () -> Void

    @AppStorage(ControllerLayoutSettings.scaleKey) private var scale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey) private var opacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.stickSpacingKey) private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey) private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(ControllerLayoutSettings.hapticsKey) private var haptics = ControllerLayoutSettings.defaultHaptics
    @AppStorage(ControllerLayoutSettings.autoHideWithControllerKey) private var autoHide = ControllerLayoutSettings.defaultAutoHideWithController
    @AppStorage(ControllerLayoutSettings.deadzoneKey) private var deadzone = ControllerLayoutSettings.defaultDeadzone

    var body: some View {
        LayoutPanelCard(rows: { rows }, footer: {
            LayoutPanelFooter(
                resetMessage: "Size, opacity, spacing, shoulder height, haptics, controller hiding and deadzone go back to how EspressU ships, in every game.",
                onReset: reset,
                onDone: onDone)
        })
    }

    @ViewBuilder private var rows: some View {
        PanelCaption(text: "Tap a stick to click it. Slide across buttons to press them one after another. These settings apply to every game.")

        PanelSliderRow("Control size", leadingIcon: "minus.magnifyingglass", trailingIcon: "plus.magnifyingglass",
                       value: $scale,
                       range: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale)

        PanelSliderRow("Opacity", leadingIcon: "circle.lefthalf.filled", trailingIcon: "circle.fill",
                       value: $opacity, range: 0.2...1.0)

        PanelSliderRow("Spacing between the two sides",
                       leadingIcon: "arrow.right.and.line.vertical.and.arrow.left",
                       trailingIcon: "arrow.left.and.line.vertical.and.arrow.right",
                       value: $stickSpacing,
                       range: ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                       step: ControllerLayoutSettings.stickSpacingStep,
                       spokenValue: ControllerLayoutSettings.stickSpacingLabel(stickSpacing))

        if ControllerLayoutSettings.supportsShoulderOffset {
            PanelSliderRow("Shoulder button height", title: "L/R", leadingIcon: "arrow.up.and.down",
                           value: $shoulderOffset,
                           range: ControllerLayoutSettings.minShoulderOffset...ControllerLayoutSettings.maxShoulderOffset,
                           step: ControllerLayoutSettings.shoulderOffsetStep,
                           spokenValue: ControllerLayoutSettings.shoulderOffsetLabel(shoulderOffset))
        }

        PanelSliderRow("Stick deadzone", title: "Deadzone",
                       value: $deadzone,
                       range: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone,
                       spokenValue: deadzone <= 0.0005 ? "off" : "\(Int((deadzone * 100).rounded())) percent",
                       readout: deadzone <= 0.0005 ? "off" : "\(Int((deadzone * 100).rounded()))%")

        if UIDevice.current.userInterfaceIdiom == .phone {
            PanelToggle(title: "Haptic feedback", isOn: $haptics)
        }

        PanelToggle(title: "Hide on-screen controls when a controller is connected", isOn: $autoHide)
    }

    private func reset() {
        ControllerLayoutSettings.reset()
        haptics = ControllerLayoutSettings.defaultHaptics
        autoHide = ControllerLayoutSettings.defaultAutoHideWithController
        deadzone = ControllerLayoutSettings.defaultDeadzone
    }
}
