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
                            .font(.body.weight(.semibold))
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
                    .font(.subheadline.weight(.semibold))
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
                    .font(.subheadline.weight(.semibold))
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
            .font(.system(size: prominent ? 13 : 12, weight: prominent ? .semibold : .regular))
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
                    .font(.subheadline.weight(.semibold))
                    .foregroundColor(.white)
                Spacer(minLength: 8)
                if let shownValue {
                    Text(shownValue)
                        .font(.footnote.weight(.semibold))
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
                .font(.subheadline.weight(.semibold))
                .foregroundColor(.white)
        }
        .tint(EspressUTheme.pixelBlue)
        .panelRowStyle()
    }
}
