// Copyright (c) 2026 EspressU (https://espressu.dev).
//
// Crema Licence: non-commercial use only. Anything that contains or is derived from this
// file must be fully open source, keep this file under the Crema Licence, keep this notice
// in every copy, and credit "Crema touch controls by EspressU (https://espressu.dev)" in
// its source, About screen, README, website, store page, release notes and ads.
// Full text: LICENSE.txt in the Crema folder. Not covered by MPL-2.0.


import SwiftUI

/// The floating control shown while the player moves and resizes single controls. It rests as a small chip
/// that can be dragged out of the way; tapping the chip opens the size slider and the resets, and starting to
/// drag a control closes it again. The controls themselves are picked and dragged on the game screen.
struct CremaElementPanel: View {
    @Binding var selection: CremaElement?
    let onDone: () -> Void

    @AppStorage(ControllerLayoutSettings.elementsKey) private var elementsJSON = ControllerLayoutSettings.defaultElements
    @State private var confirmingResetAll = false
    @State private var expanded = false
    @State private var offset = CGSize.zero
    @State private var dragStart: CGSize?
    @State private var lastPosition: [Double] = []

    private var overrides: CremaElementOverrides {
        CremaElementOverrides(json: elementsJSON) ?? CremaElementOverrides()
    }

    private func update(_ element: CremaElement, _ change: (inout CremaElementTweak) -> Void) {
        var all = overrides
        var tweak = all[element]
        change(&tweak)
        all[element] = tweak
        elementsJSON = all.json
    }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 8) {
                chip
                    .gesture(
                        DragGesture(minimumDistance: 6)
                            .onChanged { value in
                                let start = dragStart ?? offset
                                dragStart = start
                                let limitX = max(proxy.size.width / 2 - 70, 0)
                                let limitY = max(proxy.size.height - 120, 0)
                                offset = CGSize(width: min(max(start.width + value.translation.width, -limitX), limitX),
                                                height: min(max(start.height + value.translation.height, 0), limitY))
                            }
                            .onEnded { _ in dragStart = nil })
                if expanded { details }
                Spacer(minLength: 0)
            }
            .offset(offset)
            .padding(.top, 8)
            .frame(maxWidth: .infinity)
        }
        .onValueChange(of: selection) { _ in
            lastPosition = position
            withAnimation(.easeInOut(duration: 0.15)) { expanded = false }
        }
        .onValueChange(of: elementsJSON) { _ in
            // The slider only changes the size, so a changed position means a control is being dragged.
            let now = position
            if expanded, now != lastPosition { withAnimation(.easeInOut(duration: 0.15)) { expanded = false } }
            lastPosition = now
        }
    }

    private var position: [Double] {
        guard let element = selection else { return [] }
        let tweak = overrides[element]
        return [tweak.x, tweak.y]
    }

    private var chip: some View {
        HStack(spacing: 6) {
            Button {
                withAnimation(.easeInOut(duration: 0.15)) { expanded.toggle() }
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: expanded ? "chevron.up" : "slider.horizontal.3")
                        .font(.system(size: 14, weight: .bold))
                    if let element = selection {
                        Text(element.title)
                            .font(.footnote.weight(.semibold))
                            .lineLimit(1)
                    }
                }
                .foregroundColor(.white)
                .padding(.horizontal, 12)
                .frame(minHeight: 44)
                .contentShape(Capsule())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(expanded ? "Hide control options" : "Show control options")

            Button(action: onDone) {
                Image(systemName: "checkmark")
                    .font(.system(size: 14, weight: .bold))
                    .foregroundColor(.white)
                    .frame(width: 44, height: 44)
                    .background(EspressUTheme.pixelBlue, in: Circle())
                    .contentShape(Circle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Done")
        }
        .padding(.leading, 2)
        .padding(.trailing, 0)
        .espressUSurface(Capsule())
        .environment(\.colorScheme, .dark)
    }

    private var details: some View {
        VStack(spacing: 8) {
            if let element = selection {
                PanelSliderRow("Size", leadingIcon: "minus", trailingIcon: "plus",
                               value: Binding(
                                   get: { overrides[element].scale },
                                   set: { value in update(element) { $0.scale = value } }),
                               range: CremaElementTweak.minScale...CremaElementTweak.maxScale)

                Button {
                    update(element) { $0 = CremaElementTweak() }
                } label: {
                    Label("Reset element", systemImage: "arrow.counterclockwise")
                        .font(.subheadline.weight(.semibold))
                        .foregroundColor(.white)
                        .panelRowStyle()
                }
                .buttonStyle(.plain)
                .disabled(overrides[element].isDefault)
                .opacity(overrides[element].isDefault ? 0.5 : 1)
            } else {
                PanelCaption(text: "Tap a control to select it, then drag it to move it and use the slider to resize it.")
            }

            Button { confirmingResetAll = true } label: {
                Label("Reset all", systemImage: "arrow.counterclockwise")
                    .font(.subheadline.weight(.semibold))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .background(Color.white.opacity(0.1), in: Capsule())
                    .contentShape(Capsule())
            }
            .buttonStyle(.plain)
            .disabled(overrides.isEmpty)
            .opacity(overrides.isEmpty ? 0.5 : 1)
            .confirmationDialog("Reset every control?", isPresented: $confirmingResetAll, titleVisibility: .visible) {
                Button("Reset all", role: .destructive) { elementsJSON = ControllerLayoutSettings.defaultElements }
                Button("Cancel", role: .cancel) { }
            } message: {
                Text("Every control you moved or resized goes back to its default place and size.")
            }
        }
        .padding(12)
        .frame(maxWidth: 320)
        .espressUSurface(RoundedRectangle(cornerRadius: EspressUTheme.Radius.card, style: .continuous))
        .environment(\.colorScheme, .dark)
    }
}
