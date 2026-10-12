// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct OnScreenControlsSection: View {
    @AppStorage(ControllerLayoutSettings.scaleKey)
    private var controlScale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey)
    private var controlOpacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.joystickKey)
    private var joystickMode = ControllerLayoutSettings.defaultJoystick
    @AppStorage(ControllerLayoutSettings.comfortControlsKey)
    private var comfortControls = ControllerLayoutSettings.defaultComfortControls
    @AppStorage(ControllerLayoutSettings.deadzoneKey)
    private var stickDeadzone = ControllerLayoutSettings.defaultDeadzone
    @AppStorage(ControllerLayoutSettings.stickCurveKey)
    private var stickCurve = ControllerLayoutSettings.defaultStickCurve
    @AppStorage(ControllerLayoutSettings.stickGateKey)
    private var stickGateRaw = ControllerLayoutSettings.defaultStickGateRaw
    @AppStorage(ControllerLayoutSettings.hapticsKey)
    private var hapticsEnabled = ControllerLayoutSettings.defaultHaptics
    @AppStorage(ControllerLayoutSettings.autoHideWithControllerKey)
    private var autoHideWithController = ControllerLayoutSettings.defaultAutoHideWithController
    @AppStorage(TouchLabSettings.schemeKey)
    private var touchLabScheme = TouchLabSettings.defaultScheme
    @AppStorage(ControllerLayoutSettings.stickSpacingKey)
    private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey)
    private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(SettingsMode.storageKey)
    private var settingsModeRaw = SettingsMode.defaultValue.rawValue
    @State private var showingResetLayoutConfirmation = false
    @State private var showingResetBindingsConfirmation = false
    @State private var bindingsResetResult: String?

    private var stickGate: ControllerGeometry.StickGate {
        ControllerGeometry.StickGate(rawValue: stickGateRaw) ?? ControllerLayoutSettings.defaultStickGate
    }

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    private var usingTouchLab: Bool { TouchLabSettings.isTouchLab(touchLabScheme) }

    var body: some View {
        Section {
            TouchLabStyleSettingsRows()

            if !usingTouchLab {
                Toggle(isOn: $joystickMode) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Add analog sticks")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text(joystickMode
                             ? "Both sticks shown alongside the d-pad and face buttons, not instead of them."
                             : "Just the d-pad and face buttons, from the measured layout.")
                            .font(.system(size: 12))
                            .foregroundColor(.secondary)
                    }
                }
                .tint(EspressUTheme.pixelBlue)
            }

            if joystickMode || usingTouchLab {
                NavigationLink {
                    Form { Section { joystickOptions } }
                        .navigationTitle("Stick tuning")
                } label: {
                    Label("Stick tuning", systemImage: "dial.medium")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                }
            }

            if ControllerLayoutSettings.supportsShoulderOffset
                && (!usingTouchLab || TouchLabSettings.hasMovableShoulders(touchLabScheme)) {
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("L, R, ZL and ZR height")
                            .font(.system(size: 13, weight: .semibold, design: .rounded))
                        Spacer()
                        Text(ControllerLayoutSettings.shoulderOffsetLabel(usingTouchLab ? max(0, shoulderOffset) : shoulderOffset))
                            .font(.system(size: 13, design: .monospaced))
                            .foregroundColor(.secondary)
                    }
                    HStack(spacing: 10) {
                        Image(systemName: "arrow.up.and.down")
                            .accessibilityHidden(true)
                        Slider(
                            value: $shoulderOffset,
                            in: ControllerLayoutSettings.shoulderOffsetRange(touchLab: usingTouchLab),
                            step: ControllerLayoutSettings.shoulderOffsetStep
                        )
                        .accessibilityLabel("Shoulder button height")
                        .accessibilityValue(ControllerLayoutSettings.shoulderOffsetLabel(usingTouchLab ? max(0, shoulderOffset) : shoulderOffset))
                    }
                    Text("Moves the four shoulder buttons up or down together. They stop before they would leave the screen or touch the sticks and buttons.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Group {
            VStack(alignment: .leading, spacing: 4) {
                Text("Button size")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                HStack(spacing: 10) {
                    Image(systemName: "minus.magnifyingglass")
                    Slider(
                        value: $controlScale,
                        in: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale
                    )
                    .accessibilityLabel("Button size")
                    Image(systemName: "plus.magnifyingglass")
                }
            }

            VStack(alignment: .leading, spacing: 4) {
                Text("Opacity")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                HStack(spacing: 10) {
                    Image(systemName: "circle.lefthalf.filled")
                    Slider(value: $controlOpacity, in: 0.2...1.0)
                        .accessibilityLabel("Opacity")
                    Image(systemName: "circle.fill")
                }
            }

            Toggle(isOn: $hapticsEnabled) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Haptic feedback")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("A light tap on press. Turn off if it feels like buzzing rather than a button.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                }
            }
            .tint(EspressUTheme.pixelBlue)

            NavigationLink {
                Form { PreviewPadSection() }
                    .navigationTitle("New pad system (preview)")
            } label: {
                Label("New pad system (preview)", systemImage: "sparkles")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }

            Toggle(isOn: $autoHideWithController) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Hide on-screen controls when a controller is connected")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("The controls come back when it disconnects. The GamePad's screen stays, so you can still touch it.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                }
            }
            .tint(EspressUTheme.pixelBlue)
            }

            Button(role: .destructive, action: { showingResetLayoutConfirmation = true }) {
                DestructiveSettingsLabel(title: "Reset controls to default", systemImage: "arrow.uturn.backward")
            }

            Button(role: .destructive, action: { showingResetBindingsConfirmation = true }) {
                DestructiveSettingsLabel(title: "Reset controller bindings", systemImage: "gamecontroller.fill")
            }

            if let bindingsResetResult {
                Text(bindingsResetResult)
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
            }
        } header: {
            SettingsSectionHeader("On-screen Controls", icon: "gamecontroller", accent: .io)
        } footer: {
            InfoButton.footer(
                "Sticks are analog like the real GamePad. Comfort controls move the shoulder buttons onto the sticks.",
                title: "On-screen Controls",
                text: fullText)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .confirmationDialog("Reset controls to default?", isPresented: $showingResetLayoutConfirmation, titleVisibility: .visible) {
            Button("Reset to default", role: .destructive) {
                ControllerLayoutSettings.reset()
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Button size, opacity, stick spacing, shoulder height and every button you've moved go back to how EspressU ships.")
        }
        .confirmationDialog("Reset controller bindings?", isPresented: $showingResetBindingsConfirmation, titleVisibility: .visible) {
            Button("Reset bindings", role: .destructive) {
                let ok = cemu_bridge_reset_controller_bindings()
                let count = Int(cemu_bridge_input_button_mapping_count())
                bindingsResetResult = ok
                    ? "Reset. The GamePad now has \(count) button bindings."
                    : (count < 0
                        ? "No GamePad is wired yet - start a game, then try again."
                        : "Reset, but the GamePad still has no button bindings.")
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Rebuilds the default button bindings. Use this if some buttons do nothing while the sticks still work. Any buttons you remapped go back to default.")
        }
    }

    @ViewBuilder private var joystickOptions: some View {
        if !usingTouchLab {
            Toggle(isOn: $comfortControls) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Comfort controls")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(comfortControls
                         ? "L, ZL and minus sit on the left stick; R, ZR and plus sit on the right stick."
                         : "L, ZL and minus stay on the d-pad; R, ZR and plus stay on A/B/X/Y.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                }
            }
            .tint(EspressUTheme.pixelBlue)
        }

        if !usingTouchLab || TouchLabSettings.hasFixedSticks(touchLabScheme) {
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Stick spacing")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                    Spacer()
                    Text(ControllerLayoutSettings.stickSpacingLabel(stickSpacing))
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                HStack(spacing: 10) {
                    Image(systemName: "arrow.right.and.line.vertical.and.arrow.left")
                        .accessibilityHidden(true)
                    Slider(
                        value: $stickSpacing,
                        in: ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                        step: ControllerLayoutSettings.stickSpacingStep
                    )
                    .accessibilityLabel("Stick spacing")
                    .accessibilityValue(ControllerLayoutSettings.stickSpacingLabel(stickSpacing))
                    Image(systemName: "arrow.left.and.line.vertical.and.arrow.right")
                        .accessibilityHidden(true)
                }
                Text("Moves both sticks closer together or further apart, to fit your hands.")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }

        if advanced {
            VStack(alignment: .leading, spacing: 4) {
                Picker("Stick gate", selection: $stickGateRaw) {
                    ForEach(ControllerGeometry.StickGate.allCases) { gate in
                        Text(gate.title).tag(gate.rawValue)
                    }
                }
                .pickerStyle(.segmented)
                Text(stickGate.summary)
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Stick deadzone")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                    Spacer()
                    Text(stickDeadzone <= 0.0005
                         ? "off"
                         : "\(Int((stickDeadzone * 100).rounded()))%")
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                Slider(
                    value: $stickDeadzone,
                    in: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone
                )
                .accessibilityLabel("Stick deadzone")
            }

            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Fine control")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                    Spacer()
                    Text(stickCurve <= ControllerLayoutSettings.minStickCurve + 0.005
                         ? "linear"
                         : String(format: "%.1fx", stickCurve))
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                Slider(
                    value: $stickCurve,
                    in: ControllerLayoutSettings.minStickCurve...ControllerLayoutSettings.maxStickCurve
                )
                .accessibilityLabel("Fine control")
            }
        }
    }

    private var fullText: String {
        "Add analog sticks puts both sticks on screen alongside the d-pad and face buttons. Push further for more speed.\n\nGate is the shape the stick can reach. Octagon matches the real GamePad; Round reaches full travel in every direction.\n\nDeadzone is how far you can move before the game notices. Turn it up only if a resting thumb makes the game drift.\n\nFine control makes small movements gentler: at linear, halfway is half speed; higher values make halfway slower.\n\nStick spacing moves both sticks closer together or further apart, for smaller or bigger hands. On a narrow screen it stops before the sticks would touch.\n\nOn iPad, L, R, ZL and ZR height moves the four shoulder buttons up or down together, stopping before they would leave the screen or touch a stick or button.\n\nButton size and opacity adjust the size EspressU picks for your screen.\n\nHide the top bar while playing fades the Back and pause bar out a few seconds after you last touch it, so it stops covering the picture. A small handle stays at the top centre of the screen: tap it, or swipe down from it, to bring the bar back. The bar stays up while the game is paused, a menu is open, or VoiceOver is on.\n\nIn Advanced mode, Top bar hides after sets the wait (2, 4 or 8 seconds) and Reveal handle sets the size of the area you tap to bring the bar back. Normal is the default; Large is easier to hit but can take touches meant for the GamePad screen at the top of the picture.\n\nHide on-screen controls when a controller is connected takes the controls off the screen while a controller is paired, and puts them back when it disconnects. It is off by default. The GamePad's screen stays visible and touchable either way.\n\nThese settings apply to every game. Two things are kept per game instead: Adaptive remembers where your thumbs land.\n\nTo move a cluster, start a game and tap the move button in the top bar. There you can also switch control style without leaving the game."
    }
}
