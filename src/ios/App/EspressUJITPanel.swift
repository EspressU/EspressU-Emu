// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

@MainActor
final class EspressUJITPanelModel: ObservableObject {
    struct Row: Identifiable {
        enum Kind { case toggle, settings }
        let id: String
        let kind: Kind
        let title: String
        let detail: String
        let systemImage: String
    }

    @Published var focus = 0
    @Published private(set) var enabled = JITSettings.enabled
    @Published private(set) var active = JITStatus.isActive

    var openSettings: () -> Void = {}
    var close: () -> Void = {}
    private var observers: [Any] = []

    init() {
        for name in [Notification.Name.espressuJITSettingChanged, .espressuSettingsWereReset] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.refresh() }
            })
        }
    }

    var rows: [Row] {
        var rows = [Row(id: "toggle", kind: .toggle, title: "Use JIT",
                        detail: enabled ? "Games use the fast recompiler." : "Games use the slow interpreter.",
                        systemImage: enabled ? "bolt.fill" : "tortoise.fill")]
        rows.append(Row(id: "settings", kind: .settings, title: "JIT settings",
                        detail: "On or off, pairing, pairing file.", systemImage: "gearshape.fill"))
        return rows
    }

    func open() {
        refresh()
        focus = 0
    }

    func refresh() {
        enabled = JITSettings.enabled
        active = JITStatus.isActive
        focus = min(focus, rows.count - 1)
    }

    func refreshAfterReturn() {
        Task { @MainActor in
            for _ in 0..<4 {
                refresh()
                if active { break }
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
    }

    func run(_ row: Row) {
        switch row.kind {
        case .toggle:
            JITSettings.setEnabled(!enabled)
            refresh()
            if enabled { StikJITCoordinator.shared.autoEnableIfPossible() }
        case .settings:
            close()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [openSettings] in openSettings() }
        }
    }

    func handle(_ command: ControllerMenuCommand) {
        let rows = rows
        switch command {
        case .up: focus = max(0, focus - 1)
        case .down: focus = min(rows.count - 1, focus + 1)
        case .confirm: if rows.indices.contains(focus) { run(rows[focus]) }
        case .back, .menu: close()
        default: break
        }
    }
}

struct EspressUJITPanel: View {
    @ObservedObject var model: EspressUJITPanelModel
    let showsFocus: Bool
    let backSymbol: String
    var maxHeight: CGFloat = 700
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject private var coordinator = StikJITCoordinator.shared
    @ObservedObject private var vpn = LocalDevVPNMonitor.shared

    var body: some View {
        VStack(spacing: 14) {
            header
            ScrollViewReader { proxy in
            ScrollView {
                VStack(spacing: 6) {
                    ForEach(Array(model.rows.enumerated()), id: \.element.id) { index, row in
                        rowView(row, index: index)
                            .id(row.id)
                    }
                }
            }
            .frame(maxHeight: max(120, maxHeight - 120))
            .fixedSize(horizontal: false, vertical: true)
            .onChange(of: model.focus) { index in
                let rows = model.rows
                guard showsFocus, rows.indices.contains(index) else { return }
                withAnimation(.easeOut(duration: 0.15)) { proxy.scrollTo(rows[index].id) }
            }
            }
        }
        .padding(18)
        .frame(maxWidth: 460)
        .espressUSurface(RoundedRectangle(cornerRadius: 30, style: .continuous))
        .animation(.easeOut(duration: 0.15), value: model.focus)
        .onChange(of: scenePhase) { phase in
            if phase == .active { model.refreshAfterReturn() }
        }
    }

    private var header: some View {
        let overview = JITOverview.current
        let statusTint = overview.tint
        return HStack(spacing: 14) {
            Image(systemName: "bolt.fill")
                .font(.system(size: 26, weight: .bold))
                .foregroundColor(statusTint)
                .frame(width: 56, height: 56)
                .background(statusTint.opacity(0.15), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(overview.title)
                    .font(.system(size: 19, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                Text(overview.subtitle)
                    .font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
            Button { model.close() } label: {
                HStack(spacing: 6) {
                    if showsFocus {
                        Image(systemName: backSymbol)
                            .font(.system(size: 15, weight: .semibold))
                    }
                    Image(systemName: "xmark")
                        .font(.system(size: 15, weight: .bold))
                }
                .foregroundColor(EspressUTheme.brownMid)
                .padding(.horizontal, showsFocus ? 12 : 0)
                .frame(minWidth: 36, minHeight: 36)
                .background(EspressUTheme.wrapper.opacity(0.6), in: Capsule())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Close")
        }
    }

    private func rowView(_ row: EspressUJITPanelModel.Row, index: Int) -> some View {
        let focused = showsFocus && model.focus == index
        return Button {
            model.focus = index
            model.run(row)
        } label: {
            HStack(spacing: 14) {
                Image(systemName: row.systemImage)
                    .font(.system(size: 17, weight: .semibold))
                    .foregroundColor(EspressUTheme.accentText)
                    .frame(width: 38, height: 38)
                    .background(EspressUTheme.wrapper, in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(row.title)
                        .font(.system(size: 17, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                    Text(row.detail)
                        .font(.system(size: 12, weight: .medium, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
                if row.kind == .toggle {
                    SettingsSwitch(isOn: model.enabled)
                } else {
                    Image(systemName: "chevron.right")
                        .font(.system(size: 14, weight: .bold))
                        .foregroundColor(EspressUTheme.brownMid.opacity(0.6))
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .frame(minHeight: 58)
            .background(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .fill(EspressUTheme.pixelBlue.opacity(focused ? 0.1 : 0)))
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 3)
                    .opacity(focused ? 1 : 0))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}
