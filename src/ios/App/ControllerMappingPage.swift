import SwiftUI

/// The page body of "Controller buttons". It only draws what it is given, so it can be rendered on its own:
/// the app fills the model from the real mapping and passes its own colours and card style.
struct MappingPalette {
    var ink: Color
    var mid: Color
    var secondary: Color
    var accent: Color
    var accentText: Color
    var caution: Color
    var good = Color(red: 0.36, green: 0.86, blue: 0.56)
}

struct MappingSlotInfo: Identifiable {
    let id: Int
    let name: String
    let label: String
}

struct MappingPageModel {
    var groups: [(title: String, slots: [MappingSlotInfo])]
    var selected: Int?
    var listening: Bool
    var secondsLeft: Double
    var timeout: Double
    var blink: Bool
    var controllerName: String?
    var assigned: (Int) -> String
    var builtIn: (Int) -> String
    var isChanged: (Int) -> Bool

    var changedIDs: Set<Int> {
        Set(groups.flatMap { $0.slots.map(\.id) }.filter(isChanged))
    }

    func slot(_ id: Int) -> MappingSlotInfo? {
        groups.flatMap(\.slots).first { $0.id == id }
    }
}

struct MappingPageActions {
    var select: (Int) -> Void
    var change: () -> Void
    var cancel: () -> Void
    var reset: (Int) -> Void
    var resetAll: () -> Void
}

struct ControllerMappingPage: View {
    let model: MappingPageModel
    let actions: MappingPageActions
    let palette: MappingPalette
    let surface: (AnyView) -> AnyView
    var maxDiagramWidth: CGFloat = 600

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            controllerCard
            detailCard
            ForEach(model.groups, id: \.title) { group in
                section(group.title, slots: group.slots)
            }
            if !model.changedIDs.isEmpty {
                resetAllButton
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    // MARK: Controller card

    private var controllerCard: some View {
        surface(AnyView(
            VStack(spacing: 14) {
                Text("Tap a button, then press the one you want on your controller.")
                    .font(.system(size: 14, weight: .medium, design: .rounded))
                    .foregroundColor(palette.mid)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 12)
                    .frame(maxWidth: .infinity)
                ControllerDiagram(accent: palette.accent, selected: model.selected, changed: model.changedIDs,
                                  listening: model.listening, blink: model.blink, onSelect: actions.select)
                    .frame(maxWidth: maxDiagramWidth)
                    .frame(maxWidth: .infinity)
                connectionChip
            }
            .padding(.horizontal, 6)
            .padding(.vertical, 16)
            .frame(maxWidth: .infinity)
        ))
    }

    private var connectionChip: some View {
        let connected = model.controllerName != nil
        return HStack(spacing: 8) {
            Circle()
                .fill(connected ? palette.good : palette.caution)
                .frame(width: 8, height: 8)
            Text(connected ? "Using \(model.controllerName ?? "")" : "No controller connected")
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundColor(connected ? palette.mid : palette.caution)
                .lineLimit(1)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 7)
        .background(Capsule().fill(Color.primary.opacity(0.06)))
        .accessibilityElement(children: .combine)
    }

    // MARK: Detail card

    @ViewBuilder private var detailCard: some View {
        if model.listening, let id = model.selected, let slot = model.slot(id) {
            listeningCard(slot)
        } else if let id = model.selected, let slot = model.slot(id) {
            selectedCard(id: id, slot: slot)
        } else {
            hintCard
        }
    }

    private var hintCard: some View {
        surface(AnyView(
            HStack(spacing: 12) {
                Image(systemName: "hand.tap.fill")
                    .font(.system(size: 20))
                    .foregroundColor(palette.accentText)
                Text(model.controllerName == nil
                     ? "Pair a controller in the iOS Bluetooth settings to change buttons. You can still look at the layout."
                     : "Pick a GamePad button on the drawing or in the list to change it.")
                    .font(.system(size: 15, design: .rounded))
                    .foregroundColor(palette.mid)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
        ))
    }

    private func selectedCard(id: Int, slot: MappingSlotInfo) -> some View {
        let changed = model.isChanged(id)
        let canChange = model.controllerName != nil
        return surface(AnyView(
            VStack(alignment: .leading, spacing: 14) {
                HStack(spacing: 12) {
                    chip(slot.label, size: 40, tint: palette.accent.opacity(0.22))
                    VStack(alignment: .leading, spacing: 2) {
                        Text("GamePad \(slot.name)")
                            .font(.system(size: 18, weight: .heavy, design: .rounded))
                            .foregroundColor(palette.ink)
                        Text(changed ? "Changed" : "Built-in layout")
                            .font(.system(size: 13, weight: .medium, design: .rounded))
                            .foregroundColor(changed ? palette.accentText : palette.secondary)
                    }
                    Spacer(minLength: 0)
                }
                HStack(spacing: 10) {
                    infoTile("Controller button", model.assigned(id), highlight: changed)
                    infoTile("Built-in", model.builtIn(id), highlight: false)
                }
                HStack(spacing: 10) {
                    MapActionButton(title: "Change", prominent: true, enabled: canChange, palette: palette, action: actions.change)
                    if changed {
                        MapActionButton(title: "Reset", prominent: false, enabled: true, palette: palette) { actions.reset(id) }
                    }
                }
                if !canChange {
                    Text("Connect a controller to change this button.")
                        .font(.system(size: 13, design: .rounded))
                        .foregroundColor(palette.caution)
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
        ))
    }

    private func listeningCard(_ slot: MappingSlotInfo) -> some View {
        let progress = max(0, min(1, model.secondsLeft / max(model.timeout, 1)))
        return surface(AnyView(
            VStack(spacing: 14) {
                ZStack {
                    Circle().stroke(palette.accent.opacity(0.2), lineWidth: 6)
                    Circle()
                        .trim(from: 0, to: progress)
                        .stroke(palette.accent, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                        .rotationEffect(.degrees(-90))
                    Text("\(Int(model.secondsLeft.rounded(.up)))")
                        .font(.system(size: 22, weight: .heavy, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(palette.ink)
                }
                .frame(width: 64, height: 64)
                VStack(spacing: 4) {
                    Text("Press a button on your controller")
                        .font(.system(size: 17, weight: .heavy, design: .rounded))
                        .foregroundColor(palette.ink)
                    Text("It will become GamePad \(slot.name).")
                        .font(.system(size: 14, design: .rounded))
                        .foregroundColor(palette.mid)
                }
                .multilineTextAlignment(.center)
                MapActionButton(title: "Cancel", prominent: false, enabled: true, palette: palette, action: actions.cancel)
            }
            .padding(18)
            .frame(maxWidth: .infinity)
            .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(palette.accent.opacity(0.1)))
        ))
    }

    private func infoTile(_ title: String, _ value: String, highlight: Bool) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .bold, design: .rounded))
                .tracking(0.5)
                .foregroundColor(palette.secondary)
            Text(value)
                .font(.system(size: 15, weight: .semibold, design: .rounded))
                .foregroundColor(highlight ? palette.accentText : palette.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 12, style: .continuous)
            .fill(highlight ? palette.accent.opacity(0.14) : Color.primary.opacity(0.06)))
        .accessibilityElement(children: .combine)
    }

    private func chip(_ text: String, size: CGFloat, tint: Color) -> some View {
        Text(text)
            .font(.system(size: size * 0.38, weight: .heavy, design: .rounded))
            .foregroundColor(palette.ink)
            .frame(width: size, height: size)
            .background(Circle().fill(tint))
    }

    // MARK: List

    private func section(_ title: String, slots: [MappingSlotInfo]) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title.uppercased())
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(palette.secondary)
                .padding(.horizontal, 6)
            surface(AnyView(
                VStack(spacing: 0) {
                    ForEach(slots) { slot in
                        row(slot)
                        if slot.id != slots.last?.id {
                            Divider().opacity(0.4).padding(.leading, 58)
                        }
                    }
                }
                .frame(maxWidth: .infinity)
            ))
        }
    }

    private func row(_ slot: MappingSlotInfo) -> some View {
        let isSelected = model.selected == slot.id
        let changed = model.isChanged(slot.id)
        let value = model.assigned(slot.id)
        return Button {
            actions.select(slot.id)
        } label: {
            HStack(spacing: 12) {
                chip(slot.label, size: 32, tint: palette.accent.opacity(isSelected ? 0.32 : 0.13))
                Text(slot.name)
                    .font(.system(size: 16, weight: .semibold, design: .rounded))
                    .foregroundColor(palette.ink)
                Spacer(minLength: 8)
                if changed {
                    Text(value)
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .foregroundColor(palette.accentText)
                } else {
                    Text("Default")
                        .font(.system(size: 14, design: .rounded))
                        .foregroundColor(palette.secondary)
                }
            }
            .padding(.horizontal, 14)
            .frame(minHeight: 48)
            .background(isSelected ? palette.accent.opacity(0.12) : Color.clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("GamePad \(slot.name)")
        .accessibilityValue(changed ? "Changed to \(value)" : "Default")
        .accessibilityAddTraits(isSelected ? [.isButton, .isSelected] : [.isButton])
    }

    private var resetAllButton: some View {
        Button(action: actions.resetAll) {
            Label("Reset every button", systemImage: "arrow.counterclockwise")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
                .foregroundColor(Color.red)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Color.red.opacity(0.1)))
        }
        .buttonStyle(.plain)
    }
}

private struct MapActionButton: View {
    let title: String
    let prominent: Bool
    let enabled: Bool
    let palette: MappingPalette
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 15, weight: .bold, design: .rounded))
                .foregroundColor(prominent ? Color.white : palette.ink)
                .padding(.horizontal, 22)
                .frame(minHeight: 44)
                .frame(maxWidth: prominent ? .infinity : nil)
                .background(Capsule().fill(prominent ? palette.accent : Color.primary.opacity(0.1)))
                .opacity(enabled ? 1 : 0.4)
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}
