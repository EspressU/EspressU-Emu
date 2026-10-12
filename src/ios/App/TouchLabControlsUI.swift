// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

func touchLabStyleLabel(_ id: String) -> String {
    let isDefault = id == TouchLabSettings.defaultScheme
    if id.isEmpty { return isDefault ? "EspressU (default)" : "EspressU (classic)" }
    let name = TouchLabSettings.styles.first { $0.id == id }?.name ?? id
    return isDefault ? name + " (default)" : name
}

private var touchLabStyleIDs: [String] { [""] + TouchLabSettings.styles.map(\.id) }

struct TouchLabStyleSettingsRows: View {
    @AppStorage(TouchLabSettings.schemeKey) private var scheme = TouchLabSettings.defaultScheme
    @AppStorage(TouchLabSettings.floatCameraKey) private var floatCamera = TouchLabSettings.defaultFloatCamera
    @AppStorage(TouchLabSettings.racingAutoAccelerateKey) private var racingAuto = false
    @AppStorage(TouchLabSettings.racingTiltKey) private var racingTilt = false
    @AppStorage(TouchLabSettings.aScaleKey) private var aScale = TouchLabSettings.defaultAScale
    @State private var showingAdaptiveReset = false

    var body: some View {
        let selection = Binding<String>(
            get: { scheme },
            set: { newValue in scheme = newValue }
        )

        Group {
            Picker(selection: selection) {
                ForEach(touchLabStyleIDs, id: \.self) { id in
                    Text(touchLabStyleLabel(id)).tag(id)
                }
            } label: {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Control style")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(summary)
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            if TouchLabSettings.isTouchLab(scheme) {

                if scheme == TouchLabSettings.floatStyleID {
                    Picker("Camera", selection: $floatCamera) {
                        ForEach(TouchLabSettings.cameraOptions, id: \.value) { option in
                            Text(option.title).tag(option.value)
                        }
                    }
                    .pickerStyle(.segmented)
                }

                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("A button size")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Spacer()
                        Button(action: { aScale = TouchLabSettings.defaultAScale }) {
                            Text("\(Int((aScale * 100).rounded()))%")
                                .font(.system(size: 13))
                                .foregroundColor(.secondary)
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel("Reset A button size")
                        .accessibilityValue("\(Int((aScale * 100).rounded())) percent")
                    }
                    HStack(spacing: 8) {
                        Text("100%").font(.system(size: 12)).foregroundColor(.secondary)
                        Slider(value: $aScale, in: TouchLabSettings.aScaleRange, step: 0.01)
                            .tint(EspressUTheme.pixelBlue)
                            .accessibilityLabel("A button size")
                        Text("180%").font(.system(size: 12)).foregroundColor(.secondary)
                    }
                    Text("Makes A bigger and easier to hit. The buttons around it shrink a little. Tap the percentage to reset.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                }

                if scheme == TouchLabSettings.racingStyleID {
                    Toggle(isOn: $racingAuto) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Auto-accelerate")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            Text("Holds A for you. Touching Brake lets it go.")
                                .font(.system(size: 12))
                                .foregroundColor(.secondary)
                        }
                    }
                    .tint(EspressUTheme.pixelBlue)

                    Toggle(isOn: $racingTilt) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Tilt steering")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            Text("Turn the device like a wheel. Tap C at the top to set straight ahead.")
                                .font(.system(size: 12))
                                .foregroundColor(.secondary)
                        }
                    }
                    .tint(EspressUTheme.pixelBlue)
                }

                if scheme == TouchLabSettings.adaptiveStyleID {
                    Button(role: .destructive, action: { showingAdaptiveReset = true }) {
                        DestructiveSettingsLabel(title: "Reset learned layouts", systemImage: "arrow.uturn.backward")
                    }
                }

                Text("These styles don't support dragging individual buttons. Size, opacity, haptics and the stick options below apply to them.")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .onAppear { TouchLabSettings.migrateLegacyLargeA() }
        .confirmationDialog("Reset learned layouts?", isPresented: $showingAdaptiveReset, titleVisibility: .visible) {
            Button("Reset", role: .destructive) { TouchLabSettings.resetAdaptiveAll() }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Adaptive forgets where your thumbs land, in every game, and starts from its home positions again.")
        }
    }

    private var summary: String {
        TouchLabSettings.isTouchLab(scheme)
            ? TouchLabSettings.summary(scheme)
            : "EspressU's measured GamePad layout, which you can also drag around."
    }
}

struct TouchLabStylePicker: View {
    @AppStorage(TouchLabSettings.schemeKey) private var scheme = TouchLabSettings.defaultScheme

    var body: some View {
        Picker("Control style", selection: $scheme) {
            ForEach(touchLabStyleIDs, id: \.self) { id in
                Text(id.isEmpty ? "EspressU" : (TouchLabSettings.styles.first { $0.id == id }?.name ?? id))
                    .tag(id)
            }
        }
        .pickerStyle(.segmented)
    }
}

struct TouchLabLayoutPanel: View {
    let gameID: String?
    let onDone: () -> Void

    @AppStorage(TouchLabSettings.schemeKey) private var scheme = TouchLabSettings.defaultScheme
    @AppStorage(TouchLabSettings.floatCameraKey) private var floatCamera = TouchLabSettings.defaultFloatCamera
    @AppStorage(ControllerLayoutSettings.scaleKey) private var controlScale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey) private var controlOpacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.stickSpacingKey) private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey) private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset

    private var isAdaptive: Bool { scheme == TouchLabSettings.adaptiveStyleID }

    var body: some View {
        LayoutPanelCard(rows: { rows }, footer: {
            LayoutPanelFooter(
                resetMessage: isAdaptive
                    ? "Size, opacity, stick spacing and shoulder height go back to how EspressU ships in every game, and Adaptive forgets where your thumbs land in this game only."
                    : "Size, opacity, stick spacing and shoulder height go back to how EspressU ships, in every game.",
                onReset: resetToDefault,
                onDone: onDone)
        })
    }

    @ViewBuilder private var rows: some View {
        TouchLabStylePicker()

        PanelCaption(text: "\(TouchLabSettings.summary(scheme)) These styles don't support dragging individual buttons. Nothing here reaches the game.")

        PanelSliderRow("Button size", leadingIcon: "minus.magnifyingglass", trailingIcon: "plus.magnifyingglass",
                       value: $controlScale,
                       range: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale)

        PanelSliderRow("Opacity", leadingIcon: "circle.lefthalf.filled", trailingIcon: "circle.fill",
                       value: $controlOpacity, range: 0.2...1.0)

        if TouchLabSettings.hasFixedSticks(scheme) {
            PanelSliderRow("Stick spacing",
                           leadingIcon: "arrow.right.and.line.vertical.and.arrow.left",
                           trailingIcon: "arrow.left.and.line.vertical.and.arrow.right",
                           value: $stickSpacing,
                           range: ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                           step: ControllerLayoutSettings.stickSpacingStep,
                           spokenValue: ControllerLayoutSettings.stickSpacingLabel(stickSpacing))
        }

        if ControllerLayoutSettings.supportsShoulderOffset && TouchLabSettings.hasMovableShoulders(scheme) {
            PanelSliderRow("Shoulder button height", title: "L/R", leadingIcon: "arrow.up.and.down",
                           value: $shoulderOffset,
                           range: ControllerLayoutSettings.shoulderOffsetRange(touchLab: true),
                           step: ControllerLayoutSettings.shoulderOffsetStep,
                           spokenValue: ControllerLayoutSettings.shoulderOffsetLabel(max(0, shoulderOffset)))
        }

        if scheme == TouchLabSettings.floatStyleID {
            Picker("Camera", selection: $floatCamera) {
                ForEach(TouchLabSettings.cameraOptions, id: \.value) { option in
                    Text(option.title).tag(option.value)
                }
            }
            .pickerStyle(.segmented)
        }

        PanelCaption(text: isAdaptive
                     ? "These settings apply to every game, except that Adaptive remembers where your thumbs land separately for each game."
                     : "These settings apply to every game.",
                     prominent: false)
    }

    private func resetToDefault() {
        controlScale = ControllerLayoutSettings.defaultScale
        controlOpacity = ControllerLayoutSettings.defaultOpacity
        stickSpacing = ControllerLayoutSettings.defaultStickSpacing
        shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
        if isAdaptive {
            TouchLabSettings.resetAdaptive(gameID: gameID)
        }
    }
}
