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

extension View {
    /// The look every row in the on-screen controls editor shares: a soft tile on the glass card.
    func panelRowStyle() -> some View {
        padding(.horizontal, 14)
            .padding(.vertical, 11)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.white.opacity(0.08), in: RoundedRectangle(cornerRadius: EspressUTheme.Radius.chip, style: .continuous))
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

    private static var chrome: CGFloat { 168 }

    private var shape: RoundedRectangle {
        RoundedRectangle(cornerRadius: EspressUTheme.Radius.card, style: .continuous)
    }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 0) {
                VStack(spacing: 12) {
                    HStack(spacing: 8) {
                        Image(systemName: "slider.horizontal.3")
                            .font(.system(size: 14, weight: .bold))
                        Text("On-screen controls")
                            .font(.system(size: 17, weight: .heavy, design: .rounded))
                        Spacer(minLength: 0)
                    }
                    .foregroundColor(.white)
                    .accessibilityAddTraits(.isHeader)

                    ScrollView(.vertical, showsIndicators: false) {
                        VStack(spacing: 8) { rows }
                            .background(
                                GeometryReader { inner in
                                    Color.clear.preference(key: PanelRowsHeightKey.self, value: inner.size.height)
                                }
                            )
                    }
                    .frame(height: rowsHeight > 0 ? min(rowsHeight, max(120, proxy.size.height - Self.chrome)) : nil)
                    footer
                }
                .padding(16)
                .frame(maxWidth: 440)
                .background(shape.fill(Color.black.opacity(0.5)))
                .espressUSurface(shape)
                .environment(\.colorScheme, .dark)
                .padding(.top, 12)
                .padding(.horizontal, 12)

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
        HStack(spacing: 10) {
            Button { confirmingReset = true } label: {
                Label("Reset", systemImage: "arrow.counterclockwise")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                    .foregroundColor(.white)
                    .padding(.horizontal, 16)
                    .frame(minHeight: 44)
                    .background(Color.white.opacity(0.1), in: Capsule())
                    .contentShape(Capsule())
            }
            .buttonStyle(.plain)
            .confirmationDialog("Reset controls to default?", isPresented: $confirmingReset, titleVisibility: .visible) {
                Button("Reset to default", role: .destructive, action: onReset)
                Button("Cancel", role: .cancel) { }
            } message: {
                Text(resetMessage)
            }

            Button(action: onDone) {
                Label("Done", systemImage: "checkmark")
                    .font(.system(size: 15, weight: .bold, design: .rounded))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .background(EspressUTheme.pixelBlue, in: Capsule())
                    .contentShape(Capsule())
            }
            .buttonStyle(.plain)
        }
    }
}

struct PanelCaption: View {
    let text: String
    var prominent = true

    var body: some View {
        Text(text)
            .font(.system(size: prominent ? 13 : 12, weight: prominent ? .semibold : .regular, design: .rounded))
            .foregroundColor(.white.opacity(prominent ? 0.85 : 0.6))
            .multilineTextAlignment(.leading)
            .fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 4)
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

    private var shownValue: String? {
        if let readout { return readout }
        if let spokenValue { return spokenValue }
        if range.lowerBound >= 0, range.upperBound <= 2.5 { return "\(Int((value * 100).rounded()))%" }
        return nil
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(label)
                    .font(.system(size: 14, weight: .semibold, design: .rounded))
                    .foregroundColor(.white)
                Spacer(minLength: 8)
                if let shownValue {
                    Text(shownValue)
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(.white.opacity(0.7))
                        .accessibilityHidden(true)
                }
            }
            HStack(spacing: 10) {
                if let leadingIcon { icon(leadingIcon) }
                slider
                if let trailingIcon { icon(trailingIcon) }
            }
        }
        .panelRowStyle()
    }

    private func icon(_ name: String) -> some View {
        Image(systemName: name)
            .font(.system(size: 13))
            .foregroundColor(.white.opacity(0.7))
            .accessibilityHidden(true)
    }

    @ViewBuilder private var slider: some View {
        if let step {
            spoken(Slider(value: $value, in: range, step: step).tint(EspressUTheme.pixelBlue))
        } else {
            spoken(Slider(value: $value, in: range).tint(EspressUTheme.pixelBlue))
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
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .foregroundColor(.white)
        }
        .tint(EspressUTheme.pixelBlue)
        .panelRowStyle()
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

        PanelSliderRow("Stick deadzone", title: "Deadzone",
                       value: $stickDeadzone,
                       range: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone,
                       spokenValue: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded())) percent",
                       readout: stickDeadzone <= 0.0005 ? "off" : "\(Int((stickDeadzone * 100).rounded()))%")

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
