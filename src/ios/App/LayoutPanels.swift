// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

private struct PanelRowsHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

struct LayoutPanelCard<Rows: View, Footer: View>: View {
    private let rows: Rows
    private let footer: Footer
    @State private var rowsHeight: CGFloat = 0

    init(@ViewBuilder rows: () -> Rows, @ViewBuilder footer: () -> Footer) {
        self.rows = rows()
        self.footer = footer()
    }

    private static var chrome: CGFloat { 124 }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 0) {
                VStack(spacing: 10) {
                    ScrollView(.vertical, showsIndicators: true) {
                        VStack(spacing: 10) { rows }
                            .background(
                                GeometryReader { inner in
                                    Color.clear.preference(key: PanelRowsHeightKey.self, value: inner.size.height)
                                }
                            )
                    }
                    .frame(height: rowsHeight > 0 ? min(rowsHeight, max(120, proxy.size.height - Self.chrome)) : nil)
                    footer
                }
                .padding(14)
                .frame(maxWidth: 420)
                .background(Color.black.opacity(0.82))
                .cornerRadius(14)
                .padding(.top, 12)

                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity)
        }
        .onPreferenceChange(PanelRowsHeightKey.self) { rowsHeight = $0 }
        .transition(.opacity)
    }
}

struct LayoutPanelFooter: View {
    let resetMessage: String
    let onReset: () -> Void
    let onDone: () -> Void
    @State private var confirmingReset = false

    var body: some View {
        HStack(spacing: 12) {
            Button("Reset to default") { confirmingReset = true }
                .buttonStyle(EspressUSecondaryButtonStyle())
                .confirmationDialog("Reset controls to default?", isPresented: $confirmingReset, titleVisibility: .visible) {
                    Button("Reset to default", role: .destructive, action: onReset)
                    Button("Cancel", role: .cancel) { }
                } message: {
                    Text(resetMessage)
                }

            Button("Done", action: onDone)
                .buttonStyle(EspressUSecondaryButtonStyle())
        }
    }
}

struct PanelCaption: View {
    let text: String
    var prominent = true

    var body: some View {
        Text(text)
            .font(.system(size: prominent ? 12 : 11, weight: prominent ? .semibold : .regular, design: .rounded))
            .foregroundColor(.white.opacity(prominent ? 0.85 : 0.65))
            .multilineTextAlignment(.center)
            .fixedSize(horizontal: false, vertical: true)
    }
}

struct PanelSliderRow: View {
    let label: String
    var title: String?
    let leadingIcon: String?
    let trailingIcon: String?
    @Binding var value: Double
    let range: ClosedRange<Double>
    var step: Double?
    var spokenValue: String?
    var readout: String?

    init(_ label: String, title: String? = nil, leadingIcon: String? = nil, trailingIcon: String? = nil,
         value: Binding<Double>, range: ClosedRange<Double>, step: Double? = nil,
         spokenValue: String? = nil, readout: String? = nil) {
        self.label = label
        self.title = title
        self.leadingIcon = leadingIcon
        self.trailingIcon = trailingIcon
        self._value = value
        self.range = range
        self.step = step
        self.spokenValue = spokenValue
        self.readout = readout
    }

    var body: some View {
        HStack(spacing: 10) {
            if let title {
                Text(title)
                    .font(.system(size: 12, weight: .semibold, design: .rounded))
                    .foregroundColor(.white.opacity(0.85))
            }
            if let leadingIcon { icon(leadingIcon) }
            slider
            if let trailingIcon { icon(trailingIcon) }
            if let readout {
                Text(readout)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.white.opacity(0.7))
                    .frame(width: 34, alignment: .trailing)
                    .accessibilityHidden(true)
            }
        }
    }

    private func icon(_ name: String) -> some View {
        Image(systemName: name)
            .font(.system(size: 12))
            .foregroundColor(.white.opacity(0.7))
            .accessibilityHidden(true)
    }

    @ViewBuilder private var slider: some View {
        if let step {
            spoken(Slider(value: $value, in: range, step: step))
        } else {
            spoken(Slider(value: $value, in: range))
        }
    }

    @ViewBuilder private func spoken<S: View>(_ slider: S) -> some View {
        if let spokenValue {
            slider.accessibilityLabel(label).accessibilityValue(spokenValue)
        } else {
            slider.accessibilityLabel(label)
        }
    }
}

struct PanelToggle: View {
    let title: String
    @Binding var isOn: Bool

    var body: some View {
        Toggle(isOn: $isOn) {
            Text(title)
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .foregroundColor(.white.opacity(0.85))
        }
        .tint(EspressUTheme.pixelBlue)
    }
}

struct EspressUPadLayoutPanel: View {
    let onDone: () -> Void

    @AppStorage(ControllerLayoutSettings.individualEditModeKey)
    private var individualEditMode = ControllerLayoutSettings.defaultIndividualEditMode
    @AppStorage(ControllerLayoutSettings.scaleKey)
    private var controlScale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey)
    private var controlOpacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.stickSpacingKey)
    private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey)
    private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
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

    var body: some View {
        LayoutPanelCard(rows: { rows }, footer: {
            LayoutPanelFooter(
                resetMessage: "Button size, opacity, stick spacing, shoulder height and every button you've moved go back to how EspressU ships, in every game.",
                onReset: { ControllerLayoutSettings.reset() },
                onDone: onDone)
        })
    }

    @ViewBuilder private var rows: some View {
        Picker("Edit mode", selection: $individualEditMode) {
            Text("Grouped").tag(false)
            Text("Individual").tag(true)
        }
        .pickerStyle(.segmented)

        PanelCaption(text: individualEditMode
                     ? "Drag any button to move it on its own, or pinch it to resize. L and ZL move together, and so do R and ZR. Nothing here reaches the game."
                     : "Drag the empty space inside a dashed box to move that whole half - L/ZL and the rest of the left side together, R/ZR and the right side together. Nothing here reaches the game.")

        PanelSliderRow("Button size", leadingIcon: "minus.magnifyingglass", trailingIcon: "plus.magnifyingglass",
                       value: $controlScale,
                       range: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale)

        PanelSliderRow("Opacity", leadingIcon: "circle.lefthalf.filled", trailingIcon: "circle.fill",
                       value: $controlOpacity, range: 0.2...1.0)

        if ControllerLayoutSettings.supportsShoulderOffset {
            PanelSliderRow("Shoulder button height", title: "L/R", leadingIcon: "arrow.up.and.down",
                           value: $shoulderOffset,
                           range: ControllerLayoutSettings.shoulderOffsetRange(touchLab: false),
                           step: ControllerLayoutSettings.shoulderOffsetStep,
                           spokenValue: ControllerLayoutSettings.shoulderOffsetLabel(shoulderOffset))
        }

        PanelToggle(title: "Add analog sticks", isOn: $joystickMode)

        if joystickMode { stickRows }

        PanelCaption(text: "These settings apply to every game.", prominent: false)
    }

    @ViewBuilder private var stickRows: some View {
        PanelToggle(title: "Comfort controls", isOn: $comfortControls)

        PanelCaption(text: comfortControls
                     ? "L, ZL and minus sit on the left stick; R, ZR and plus sit on the right stick."
                     : "L, ZL and minus stay on the d-pad; R, ZR and plus stay on A/B/X/Y.",
                     prominent: false)

        Picker("Stick gate", selection: $stickGateRaw) {
            ForEach(ControllerGeometry.StickGate.allCases) { gate in
                Text(gate.title).tag(gate.rawValue)
            }
        }
        .pickerStyle(.segmented)

        PanelSliderRow("Stick deadzone", title: "Deadzone",
                       value: $stickDeadzone,
                       range: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone,
                       spokenValue: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded())) percent",
                       readout: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded()))%")

        PanelSliderRow("Fine control", title: "Fine",
                       value: $stickCurve,
                       range: ControllerLayoutSettings.minStickCurve...ControllerLayoutSettings.maxStickCurve,
                       spokenValue: stickCurve <= ControllerLayoutSettings.minStickCurve + 0.005
                           ? "linear" : String(format: "%.1f times", stickCurve),
                       readout: stickCurve <= ControllerLayoutSettings.minStickCurve + 0.005
                           ? "lin" : String(format: "%.1fx", stickCurve))

        PanelSliderRow("Stick spacing", title: "Sticks",
                       leadingIcon: "arrow.right.and.line.vertical.and.arrow.left",
                       trailingIcon: "arrow.left.and.line.vertical.and.arrow.right",
                       value: $stickSpacing,
                       range: ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                       step: ControllerLayoutSettings.stickSpacingStep,
                       spokenValue: ControllerLayoutSettings.stickSpacingLabel(stickSpacing))
    }
}

struct PreviewLayoutPanel: View {
    let onDone: () -> Void

    var body: some View {
        LayoutPanelCard(rows: { rows }, footer: {
            LayoutPanelFooter(
                resetMessage: "Every group goes back to the preset's own position and size, in every game. The preset, colours and picture mode you picked stay as they are.",
                onReset: { PreviewPadStore.shared.resetAdjustments() },
                onDone: onDone)
        })
    }

    @ViewBuilder private var rows: some View {
        PanelCaption(text: "Drag a group of buttons to move it, or pinch it to resize it. Nothing here reaches the game.")
        PanelCaption(text: "This is the experimental pad. Its size, colours and layout presets are in Settings.", prominent: false)
    }
}
