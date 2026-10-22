// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct TouchLabLayoutPanel: View {
    let gameID: String?
    let onDone: () -> Void

    @AppStorage(ControllerLayoutSettings.scaleKey) private var controlScale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey) private var controlOpacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.stickSpacingKey) private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey) private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(ControllerLayoutSettings.hapticsKey) private var haptics = ControllerLayoutSettings.defaultHaptics
    @AppStorage(ControllerLayoutSettings.autoHideWithControllerKey) private var autoHide = ControllerLayoutSettings.defaultAutoHideWithController
    @AppStorage(ControllerLayoutSettings.deadzoneKey) private var stickDeadzone = ControllerLayoutSettings.defaultDeadzone

    var body: some View {
        LayoutPanelCard(rows: { rows }, footer: {
            LayoutPanelFooter(
                resetMessage: "Size, opacity, stick spacing, shoulder height, haptics, controller hiding and deadzone go back to how EspressU ships, in every game.",
                onReset: resetToDefault,
                onDone: onDone)
        })
    }

    @ViewBuilder private var rows: some View {
        PanelCaption(text: "The sticks and buttons are always on screen. Nothing here reaches the game.")

        PanelSliderRow("Button size", leadingIcon: "minus.magnifyingglass", trailingIcon: "plus.magnifyingglass",
                       value: $controlScale,
                       range: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale)

        PanelSliderRow("Opacity", leadingIcon: "circle.lefthalf.filled", trailingIcon: "circle.fill",
                       value: $controlOpacity, range: 0.2...1.0)

        PanelSliderRow("Stick spacing",
                       leadingIcon: "arrow.right.and.line.vertical.and.arrow.left",
                       trailingIcon: "arrow.left.and.line.vertical.and.arrow.right",
                       value: $stickSpacing,
                       range: ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                       step: ControllerLayoutSettings.stickSpacingStep,
                       spokenValue: ControllerLayoutSettings.stickSpacingLabel(stickSpacing))

        if ControllerLayoutSettings.supportsShoulderOffset {
            PanelSliderRow("Shoulder button height", title: "L/R", leadingIcon: "arrow.up.and.down",
                           value: $shoulderOffset,
                           range: ControllerLayoutSettings.shoulderOffsetRange(touchLab: true),
                           step: ControllerLayoutSettings.shoulderOffsetStep,
                           spokenValue: ControllerLayoutSettings.shoulderOffsetLabel(max(0, shoulderOffset)))
        }

        if UIDevice.current.userInterfaceIdiom == .phone {
            PanelToggle(title: "Haptic feedback", isOn: $haptics)
        }

        PanelToggle(title: "Disable on-screen controls when a controller is connected", isOn: $autoHide)

        PanelSliderRow("Stick deadzone", title: "Deadzone",
                       value: $stickDeadzone,
                       range: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone,
                       spokenValue: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded())) percent",
                       readout: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded()))%")

        PanelCaption(text: "These settings apply to every game.", prominent: false)
    }

    private func resetToDefault() {
        controlScale = ControllerLayoutSettings.defaultScale
        controlOpacity = ControllerLayoutSettings.defaultOpacity
        stickSpacing = ControllerLayoutSettings.defaultStickSpacing
        shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
        haptics = ControllerLayoutSettings.defaultHaptics
        autoHide = ControllerLayoutSettings.defaultAutoHideWithController
        stickDeadzone = ControllerLayoutSettings.defaultDeadzone
    }
}
