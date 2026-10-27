// SPDX-License-Identifier: MPL-2.0

import SwiftUI

struct EspressURowList: View {
    let groups: [SettingsGroup]
    @Binding var focus: String?
    @Binding var armed: String?
    let showsFocus: Bool
    var stacked = false
    var dropdown: Binding<SettingsDropdownState?>? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            ForEach(groups) { group in
                VStack(alignment: .leading, spacing: 6) {
                    if !group.title.isEmpty {
                        Text(group.title.uppercased())
                            .font(.footnote)
                            .foregroundColor(.secondary)
                            .padding(.horizontal, 16)
                    }
                    VStack(spacing: 0) {
                        ForEach(Array(group.items.enumerated()), id: \.element.id) { index, item in
                            if index > 0 {
                                Rectangle()
                                    .fill(EspressUTheme.hairline)
                                    .frame(height: EspressUTheme.hairlineWidth)
                                    .padding(.leading, 16)
                            }
                            row(item)
                                .id(item.id)
                        }
                    }
                    .espressUSurface(RoundedRectangle(cornerRadius: 20, style: .continuous))
                }
            }
        }
    }

    private func row(_ item: SettingsItem) -> some View {
        let focused = showsFocus && focus == item.id
        return rowContent(item)
            .disabled(!item.enabled)
            .opacity(item.enabled ? 1 : 0.45)
            .background(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .fill(EspressUTheme.pixelBlue.opacity(focused ? 0.12 : 0)))
            .overlay(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 2)
                    .opacity(focused ? 1 : 0))
            .animation(.easeOut(duration: 0.15), value: focused)
    }

    @ViewBuilder private func rowContent(_ item: SettingsItem) -> some View {
        switch item.control {
        case .toggle(let binding):
            Button {
                focus = item.id
                binding.wrappedValue.toggle()
            } label: {
                rowLabel(item) { SettingsSwitch(isOn: binding.wrappedValue) }
            }
            .buttonStyle(.plain)
            .accessibilityValue(binding.wrappedValue ? "On" : "Off")

        case .choice(let options, let binding):
            if EspressUSettingsView.usesSegments(options) {
                rowLabel(item, stacked: stacked) { segments(options, binding, id: item.id) }
            } else {
                Button {
                    focus = item.id
                    dropdown?.wrappedValue = SettingsDropdown.open(item)
                } label: {
                    rowLabel(item) {
                        HStack(spacing: 6) {
                            Text(options.first { $0.id == binding.wrappedValue }?.title ?? "")
                                .font(.body)
                                .foregroundColor(EspressUTheme.brownMid)
                                .multilineTextAlignment(.trailing)
                            Image(systemName: "chevron.up.chevron.down")
                                .font(.footnote.weight(.semibold))
                                .foregroundColor(EspressUTheme.brownMid)
                        }
                    }
                }
                .buttonStyle(.plain)
            }

        case .slider(let binding, let range, let step, let label):
            VStack(alignment: .leading, spacing: 6) {
                rowLabel(item, padded: false) {
                    Text(label(binding.wrappedValue))
                        .font(.body)
                        .foregroundColor(EspressUTheme.brownMid)
                        .monospacedDigit()
                }
                Slider(value: binding, in: range, step: step)
                    .tint(EspressUTheme.pixelBlue)
                    .accessibilityLabel(item.title)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 11)

        case .action(let title, let destructive, let confirm, let run):
            let isArmed = armed == item.id
            Button {
                focus = item.id
                EspressURowLogic.press(item.id, confirm: confirm, armed: $armed, run: run)
            } label: {
                rowLabel(item, titleColor: destructive ? EspressUTheme.alertText : nil) {
                    Text(isArmed ? "Press again" : title)
                        .font(.body)
                        .foregroundColor(isArmed ? EspressUTheme.onAccent
                                         : (destructive ? EspressUTheme.alertText : EspressUTheme.accentText))
                        .padding(.horizontal, 16)
                        .padding(.vertical, 8)
                        .modifier(SettingsActionCapsule(armed: isArmed,
                                                        fill: destructive ? EspressUTheme.alertText : EspressUTheme.pixelBlue))
                }
            }
            .buttonStyle(.plain)

        case .info(let value, let tint):
            rowLabel(item) {
                Text(value)
                    .font(.body)
                    .foregroundColor(tint ?? EspressUTheme.brownMid)
                    .multilineTextAlignment(.trailing)
            }

        case .banner(let view):
            view
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(16)

        case .custom(let view):
            if item.title.isEmpty {
                view
                    .padding(.horizontal, 16)
                    .padding(.vertical, 12)
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else {
                rowLabel(item) { view }
            }

        case .link, .expand, .open, .padButton:
            rowLabel(item) { EmptyView() }
        }
    }

    private func rowLabel<Trailing: View>(_ item: SettingsItem, stacked: Bool = false, padded: Bool = true,
                                          titleColor: Color? = nil,
                                          @ViewBuilder trailing: () -> Trailing) -> some View {
        let text = VStack(alignment: .leading, spacing: 2) {
            Text(item.title)
                .font(.body)
                .foregroundColor(titleColor ?? EspressUTheme.brownDarkest)
                .fixedSize(horizontal: false, vertical: true)
            if let detail = item.detail, !detail.isEmpty {
                Text(detail)
                    .font(.footnote)
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        let control = trailing()
        return Group {
            if stacked {
                VStack(alignment: .leading, spacing: 10) {
                    text
                    control
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            } else {
                HStack(spacing: 16) {
                    text
                    Spacer(minLength: 8)
                    control
                }
            }
        }
        .padding(.horizontal, padded ? 16 : 0)
        .padding(.vertical, padded ? 11 : 0)
        .frame(minHeight: padded ? 44 : 0)
        .contentShape(Rectangle())
    }

    private func segments(_ options: [SettingsOption], _ binding: Binding<String>, id: String) -> some View {
        HStack(spacing: 2) {
            ForEach(options) { option in
                let selected = option.id == binding.wrappedValue
                Button {
                    focus = id
                    binding.wrappedValue = option.id
                } label: {
                    Text(option.title)
                        .font(.subheadline.weight(selected ? .semibold : .regular))
                        .foregroundColor(selected ? EspressUTheme.brownDarkest : EspressUTheme.brownMid)
                        .lineLimit(1)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 7)
                        .background {
                            if selected { Color.clear.espressUSurface(Capsule()) }
                        }
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? .isSelected : [])
            }
        }
        .padding(3)
        .background(Color.white.opacity(0.08), in: Capsule())
    }
}

enum EspressURowLogic {
    static func focusOrder(_ groups: [SettingsGroup]) -> [String] {
        groups.flatMap(\.items).filter(\.isFocusable).map(\.id)
    }

    static func item(_ id: String?, in groups: [SettingsGroup]) -> SettingsItem? {
        guard let id else { return nil }
        return groups.flatMap(\.items).first { $0.id == id }
    }

    static func step(_ focus: String?, in order: [String], by delta: Int) -> String? {
        guard let focus, let index = order.firstIndex(of: focus) else { return order.first }
        let next = index + delta
        return order.indices.contains(next) ? order[next] : focus
    }

    static func press(_ id: String, confirm: Bool, armed: Binding<String?>, run: () -> Void) {
        guard confirm else {
            armed.wrappedValue = nil
            run()
            return
        }
        if armed.wrappedValue == id {
            armed.wrappedValue = nil
            run()
        } else {
            armed.wrappedValue = id
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                if armed.wrappedValue == id { armed.wrappedValue = nil }
            }
        }
    }

    static func activate(_ item: SettingsItem, armed: Binding<String?>,
                         dropdown: Binding<SettingsDropdownState?>? = nil) {
        switch item.control {
        case .toggle(let binding):
            binding.wrappedValue.toggle()
        case .choice(let options, let binding):
            guard !options.isEmpty else { return }
            if let dropdown, !EspressUSettingsView.usesSegments(options) {
                dropdown.wrappedValue = SettingsDropdown.open(item)
                return
            }
            let index = options.firstIndex { $0.id == binding.wrappedValue } ?? -1
            binding.wrappedValue = options[(index + 1) % options.count].id
        case .action(_, _, let confirm, let run):
            press(item.id, confirm: confirm, armed: armed, run: run)
        default:
            break
        }
    }

    static func adjust(_ item: SettingsItem, by direction: Int) -> Bool {
        switch item.control {
        case .choice(let options, let binding):
            guard let index = options.firstIndex(where: { $0.id == binding.wrappedValue }) else {
                if let first = options.first { binding.wrappedValue = first.id }
                return true
            }
            let next = min(max(index + direction, 0), options.count - 1)
            if next != index { binding.wrappedValue = options[next].id }
            return true
        case .slider(let binding, let range, let step, _):
            let amount = step > 0 ? step : (range.upperBound - range.lowerBound) / 20
            let next = min(max(binding.wrappedValue + amount * Double(direction), range.lowerBound), range.upperBound)
            if next != binding.wrappedValue { binding.wrappedValue = next }
            return true
        default:
            return false
        }
    }
}

struct EspressUCloseButton: View {
    let showsHint: Bool
    let hintSymbol: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if showsHint {
                    Image(systemName: hintSymbol)
                        .font(.body.weight(.semibold))
                        .foregroundColor(EspressUTheme.brownMid)
                }
                Image(systemName: "xmark")
                    .font(.body.weight(.semibold))
                    .foregroundColor(EspressUTheme.brownDark)
            }
            .padding(.horizontal, showsHint ? 14 : 0)
            .frame(minWidth: 44, minHeight: 44)
            .espressUSurface(Capsule())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Close")
    }
}

struct EspressUFocusRing: ViewModifier {
    let isOn: Bool
    var radius: CGFloat = 999

    func body(content: Content) -> some View {
        content
            .overlay(
                RoundedRectangle(cornerRadius: radius, style: .continuous)
                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 2)
                    .padding(-4)
                    .opacity(isOn ? 1 : 0))
            .animation(.easeOut(duration: 0.15), value: isOn)
    }
}

extension View {
    func espressUFocusRing(_ isOn: Bool, radius: CGFloat = 999) -> some View {
        modifier(EspressUFocusRing(isOn: isOn, radius: radius))
    }
}

struct SettingsDropdownState: Equatable {
    var itemID: String
    var index: Int
}

enum SettingsDropdown {
    static func open(_ item: SettingsItem) -> SettingsDropdownState? {
        guard case .choice(let options, let binding) = item.control, !options.isEmpty else { return nil }
        return SettingsDropdownState(itemID: item.id, index: options.firstIndex { $0.id == binding.wrappedValue } ?? 0)
    }

    static func options(_ state: SettingsDropdownState, in groups: [SettingsGroup]) -> (SettingsItem, [SettingsOption], Binding<String>)? {
        guard let item = groups.flatMap(\.items).first(where: { $0.id == state.itemID }),
              case .choice(let options, let binding) = item.control else { return nil }
        return (item, options, binding)
    }

    static func handle(_ command: ControllerMenuCommand, state: Binding<SettingsDropdownState?>, groups: [SettingsGroup]) {
        guard let current = state.wrappedValue, let (_, options, binding) = options(current, in: groups) else {
            state.wrappedValue = nil
            return
        }
        switch command {
        case .up: state.wrappedValue?.index = max(0, current.index - 1)
        case .down: state.wrappedValue?.index = min(options.count - 1, current.index + 1)
        case .confirm:
            if options.indices.contains(current.index) { binding.wrappedValue = options[current.index].id }
            state.wrappedValue = nil
        case .back, .menu: state.wrappedValue = nil
        default: break
        }
    }
}

struct SettingsDropdownOverlay: View {
    @Binding var state: SettingsDropdownState?
    let groups: [SettingsGroup]
    let showsFocus: Bool

    var body: some View {
        if let current = state, let (item, options, binding) = SettingsDropdown.options(current, in: groups) {
            ZStack {
                Color.black.opacity(0.35)
                    .ignoresSafeArea()
                    .onTapGesture { state = nil }
                VStack(alignment: .leading, spacing: 10) {
                    Text(item.title)
                        .font(.headline)
                        .foregroundColor(EspressUTheme.brownDarkest)
                        .padding(.horizontal, 8)
                    ScrollViewReader { proxy in
                        ScrollView {
                            VStack(spacing: 4) {
                                ForEach(Array(options.enumerated()), id: \.element.id) { index, option in
                                    optionRow(option, index: index, selected: option.id == binding.wrappedValue,
                                              focused: showsFocus && index == current.index) {
                                        binding.wrappedValue = option.id
                                        state = nil
                                    }
                                    .id(index)
                                }
                            }
                        }
                        .frame(maxHeight: 420)
                        .fixedSize(horizontal: false, vertical: true)
                        .onAppear { proxy.scrollTo(current.index, anchor: .center) }
                        .onValueChange(of: state?.index) { index in
                            guard let index else { return }
                            withAnimation(.easeOut(duration: 0.15)) { proxy.scrollTo(index, anchor: .center) }
                        }
                    }
                }
                .padding(16)
                .frame(maxWidth: 420)
                .espressUSurface(RoundedRectangle(cornerRadius: 28, style: .continuous))
                .padding(24)
            }
            .transition(.opacity)
        }
    }

    private func optionRow(_ option: SettingsOption, index: Int, selected: Bool, focused: Bool,
                           pick: @escaping () -> Void) -> some View {
        Button(action: pick) {
            HStack(spacing: 12) {
                Text(option.title)
                    .font(.body.weight(selected ? .semibold : .regular))
                    .foregroundColor(selected ? EspressUTheme.accentText : EspressUTheme.brownDarkest)
                    .multilineTextAlignment(.leading)
                Spacer(minLength: 8)
                if selected {
                    Image(systemName: "checkmark")
                        .font(.body.weight(.semibold))
                        .foregroundColor(EspressUTheme.accentText)
                }
            }
            .padding(.horizontal, 14)
            .frame(minHeight: 44)
            .background(RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(selected ? EspressUTheme.wrapper : (focused ? EspressUTheme.pixelBlue.opacity(0.1) : Color.clear)))
            .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 2)
                .opacity(focused ? 1 : 0))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// The pill behind a row's action: Liquid Glass at rest, filled while it waits for a second press.
struct SettingsActionCapsule: ViewModifier {
    let armed: Bool
    let fill: Color

    @ViewBuilder func body(content: Content) -> some View {
        if armed {
            content.background(fill, in: Capsule())
        } else {
            content.espressUSurface(Capsule())
        }
    }
}
