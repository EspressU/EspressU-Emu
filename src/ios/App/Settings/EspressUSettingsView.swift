// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit
import GameController
import UniformTypeIdentifiers

struct EspressUSettingsView: View {
    @ObservedObject var gameManager: GameManager
    @Environment(\.dismiss) var dismiss
    @Environment(\.scenePhase) private var scenePhase

    @StateObject var values = SettingsValues()
    @StateObject var input = ControllerMenuInput()
    @ObservedObject var mapping = ControllerMappingSession.shared
    @ObservedObject var accountSheets = AccountSheetRouter.shared
    @ObservedObject var uiStyle = UIStyleStore.shared

    @State var category: SettingsCategory = .jit
    @State var showingCategory = false
    @State var legacy: LegacySettingsPage?
    @State var focus: String?
    @State var sidebarFocused = true
    @State var gridFocus = "cat.0"
    @State var isWide = true
    @State var expanded: Set<SettingsCategory> = []
    @State var armed: String?
    @State var notes: [String: String] = [:]
    @State var dropdown: SettingsDropdownState?
    @State var pageAlert: SettingsAlert?
    @State var busyRows: Set<String> = []
    @State var importingAccount = false
    @ObservedObject var gameData = GameDataStore.shared
    @ObservedObject var externalLibrary = ExternalLibrary.shared
    @ObservedObject var graphicPackStore = GraphicPackStore.shared
    @State var selectedPackGame = ""
    @State var selectedPackPath = ""
    @State var packDetails: GraphicPackDetails?
    @State var returnFocus: [LegacySettingsPage: String] = [:]
    @State var jitActive = JITStatus.isActive
    @ObservedObject var jitCoordinator = StikJITCoordinator.shared
    @ObservedObject var pairing = DevicePairing.shared
    @State var importingPairing = false
    @State var importingServer = false
    @State var showingServerForm = false
    @State var shaderLearned: Int64 = 0
    @State var shaderCompiled: Int64 = 0

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width >= 700
            ZStack(alignment: .bottom) {
                EspressUBackground()
                    .ignoresSafeArea()

                Group {
                    if wide {
                        wideLayout(width: geo.size.width)
                    } else {
                        phoneLayout
                    }
                }

                if !wide { bottomBar(wide: false) }
            }
            .overlay(alignment: .topTrailing) { closeButton(wide: wide) }
            .overlay { SettingsDropdownOverlay(state: $dropdown, groups: currentGroups(), showsFocus: input.showsFocus) }
            .animation(.easeOut(duration: 0.15), value: dropdown)
            .animation(.easeOut(duration: 0.2), value: input.showsFocus)
            .onAppear { isWide = wide }
            .onChange(of: wide) { isWide = $0 }
        }
        .preferredColorScheme(EspressUAppearance.colorScheme)
        .onAppear(perform: appear)
        .onDisappear {
            ControllerMappingSession.shared.cancel()
            input.stop()
        }
        .onChange(of: scenePhase) { phase in
            guard phase == .active else { return }
            jitActive = JITStatus.isActive
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { jitActive = JITStatus.isActive }
            values.refresh()
        }
        .fileImporter(isPresented: $importingPairing, allowedContentTypes: [.propertyList, .xml, .data, .item]) { result in
            importPairingFile(result)
        }
        .fileImporter(isPresented: $importingServer, allowedContentTypes: [.xml, .plainText, .data]) { result in
            importCustomServer(result)
        }
        .sheet(isPresented: $showingServerForm, onDismiss: { values.refresh() }) {
            CustomServerFormView()
        }
        .alert(pageAlert?.title ?? "", isPresented: Binding(
            get: { pageAlert != nil }, set: { if !$0 { pageAlert = nil } }), presenting: pageAlert) { alert in
            ForEach(alert.actions) { action in
                Button(action.title, role: action.destructive ? .destructive : (action.cancel ? .cancel : nil)) {
                    pageAlert = nil
                    action.run()
                }
            }
        } message: { alert in
            Text(alert.message)
        }
        .background(AccountImportButton(locked: cemu_bridge_accounts_locked(), onChange: {
            cemu_bridge_accounts_refresh()
            values.refresh()
        }, external: $importingAccount))
        .alert(jitCoordinator.failure?.title ?? "", isPresented: Binding(
            get: { jitCoordinator.failure != nil && jitCoordinator.failure?.offersInterpreter == false },
            set: { if !$0 { jitCoordinator.failure = nil } })) {
            Button("OK", role: .cancel) { jitCoordinator.failure = nil }
        } message: {
            Text(jitCoordinator.failure?.message ?? "")
        }
        .sheet(isPresented: $accountSheets.showingCreateAccount, onDismiss: accountSheets.sheetClosed) {
            CreateAccountView()
        }
    }

    private func appear() {
        UserDefaults.standard.set(SettingsMode.advanced.rawValue, forKey: SettingsMode.storageKey)
        if uiStyle.useClassicUI { uiStyle.useClassicUI = false }
        refreshShaderStats()
        jitActive = JITStatus.isActive
        jitCoordinator.refreshTXM()
        input.onCommand = handle
        input.isBlocked = { ControllerMappingSession.shared.isListening }
        input.isExclusive = true
        input.start()
        cemu_bridge_log_line("Settings: opened")
    }

    func close() {
        ControllerMappingSession.shared.cancel()
        input.stop()
        dismiss()
    }

    private func wideLayout(width: CGFloat) -> some View {
        HStack(alignment: .top, spacing: 24) {
            sidebar
                .frame(width: min(300, max(230, width * 0.27)))
            Group {
                if let legacy, let native = subpageGroups(legacy) {
                    subpage(legacy, groups: native)
                } else if let legacy {
                    legacyPage(legacy)
                        .padding(.top, 58)
                } else {
                    categoryPage(category, showsBack: false)
                }
            }
            .frame(maxWidth: .infinity)
        }
        .padding(.horizontal, 28)
        .padding(.top, 20)
        .padding(.bottom, 20)
    }

    @ViewBuilder private var phoneLayout: some View {
        if let legacy, let native = subpageGroups(legacy) {
            subpage(legacy, groups: native)
                .padding(.horizontal, 16)
        } else if let legacy {
            legacyPage(legacy)
                .padding(.horizontal, 16)
                .padding(.top, 64)
                .padding(.bottom, phoneBottomRoom)
        } else if showingCategory {
            categoryPage(category, showsBack: true)
                .padding(.horizontal, 16)
        } else {
            categoryGrid
        }
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Settings")
                .font(.system(size: 30, weight: .heavy, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
                .padding(.horizontal, 10)
                .padding(.top, 4)
                .padding(.bottom, 14)
            ForEach(SettingsCategory.allCases) { item in
                let selected = item == category
                Button { pick(item) } label: {
                    HStack(spacing: 14) {
                        Image(systemName: item.icon)
                            .font(.system(size: 17, weight: .semibold))
                            .foregroundColor(selected ? EspressUTheme.onAccent : EspressUTheme.pixelBlue)
                            .frame(width: 36, height: 36)
                            .background(selected ? Color.white.opacity(0.22) : EspressUTheme.wrapper,
                                        in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                        Text(item.title)
                            .font(.system(size: 17, weight: .bold, design: .rounded))
                            .foregroundColor(selected ? EspressUTheme.onAccent : EspressUTheme.brownDarkest)
                            .lineLimit(1)
                            .minimumScaleFactor(0.8)
                        Spacer(minLength: 0)
                    }
                    .padding(.horizontal, 14)
                    .frame(minHeight: 56)
                    .background(selected ? EspressUTheme.pixelBlue : Color.clear,
                                in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                    .contentShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
                }
                .buttonStyle(.plain)
                .controllerFocusRing(input.showsFocus && sidebarFocused && item == category,
                                     shape: RoundedRectangle(cornerRadius: 18, style: .continuous))
            }
            Spacer(minLength: 0)
            if input.showsFocus {
                sidebarHints
                    .transition(.opacity.combined(with: .move(edge: .bottom)))
            }
        }
        .padding(.vertical, 18)
        .padding(.horizontal, 14)
        .frame(maxHeight: .infinity, alignment: .top)
        .espressUSurface(RoundedRectangle(cornerRadius: 30, style: .continuous))
    }

    private var categoryGrid: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("Settings")
                    .font(.system(size: 32, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .padding(.top, 16)
                VStack(spacing: 0) {
                    ForEach(Array(SettingsCategory.allCases.enumerated()), id: \.element.id) { index, item in
                        if index > 0 {
                            Rectangle()
                                .fill(EspressUTheme.wrapper)
                                .frame(height: 1)
                                .padding(.leading, 66)
                        }
                        Button { openCategory(item) } label: {
                            HStack(spacing: 14) {
                                Image(systemName: item.icon)
                                    .font(.system(size: 17, weight: .semibold))
                                    .foregroundColor(EspressUTheme.pixelBlue)
                                    .frame(width: 36, height: 36)
                                    .background(EspressUTheme.wrapper, in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(item.title)
                                        .font(.system(size: 17, weight: .bold, design: .rounded))
                                        .foregroundColor(EspressUTheme.brownDarkest)
                                        .lineLimit(1)
                                    Text(item.hint)
                                        .font(.system(size: 13, design: .rounded))
                                        .foregroundColor(EspressUTheme.brownMid)
                                        .lineLimit(2)
                                        .fixedSize(horizontal: false, vertical: true)
                                }
                                Spacer(minLength: 8)
                                Image(systemName: "chevron.right")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundColor(EspressUTheme.brownMid.opacity(0.7))
                            }
                            .padding(.horizontal, 16)
                            .padding(.vertical, 12)
                            .frame(maxWidth: .infinity, minHeight: 64, alignment: .leading)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .controllerFocusRing(input.showsFocus && gridFocus == "cat.\(item.rawValue)",
                                             shape: RoundedRectangle(cornerRadius: 22, style: .continuous))
                    }
                }
                .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
            }
            .padding(.horizontal, 16)
            .padding(.bottom, isWide ? 24 : phoneBottomRoom)
        }
    }

    private func categoryPage(_ page: SettingsCategory, showsBack: Bool) -> some View {
        rowsPage(title: page.title, summary: page.summary, groups: groups(for: page),
                 showsBack: showsBack, backLabel: "Settings", onBack: closeCategory)
    }

    private func subpage(_ page: LegacySettingsPage, groups: [SettingsGroup]) -> some View {
        rowsPage(title: subpageTitle(page), summary: page.summary, groups: groups,
                 showsBack: true, backLabel: page.parent.map(subpageTitle) ?? category.title, onBack: closeLegacy)
    }

    private func subpageTitle(_ page: LegacySettingsPage) -> String {
        switch page {
        case .graphicPackGame:
            return selectedPackGame == GraphicPack.allGames ? "Every game" : selectedPackGame
        case .graphicPack:
            return GraphicPackStore.shared.packs.first { $0.path == selectedPackPath }?.displayName ?? page.title
        default:
            return page.title
        }
    }

    private func rowsPage(title: String, summary: String, groups: [SettingsGroup], showsBack: Bool,
                          backLabel: String, onBack: @escaping () -> Void) -> some View {
        ScrollViewReader { proxy in
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    if showsBack {
                        Button { onBack() } label: {
                            Label(backLabel, systemImage: "chevron.left")
                                .font(.system(size: 17, weight: .semibold, design: .rounded))
                                .foregroundColor(EspressUTheme.accentText)
                        }
                        .buttonStyle(.plain)
                        .padding(.top, 12)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        Text(title)
                            .font(.system(size: isWide ? 32 : 28, weight: .heavy, design: .rounded))
                            .foregroundColor(EspressUTheme.brownDarkest)
                        Text(summary)
                            .font(.system(size: isWide ? 16 : 15, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .padding(.horizontal, 6)
                    .padding(.top, showsBack ? 0 : 10)
                    .id("top")

                    ForEach(groups) { group in
                        VStack(alignment: .leading, spacing: 6) {
                            if !group.title.isEmpty {
                                Text(group.title.uppercased())
                                    .font(.system(size: 13, weight: .heavy, design: .rounded))
                                    .tracking(0.6)
                                    .foregroundColor(EspressUTheme.brownMid)
                                    .padding(.horizontal, 8)
                            }
                            VStack(spacing: 0) {
                                ForEach(Array(group.items.enumerated()), id: \.element.id) { index, item in
                                    if index > 0 {
                                        Rectangle()
                                            .fill(EspressUTheme.wrapper)
                                            .frame(height: 1)
                                            .padding(.horizontal, 18)
                                    }
                                    row(item)
                                        .id(item.id)
                                }
                            }
                            .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
                            .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
                        }
                    }
                }
                .padding(.bottom, isWide ? 24 : phoneBottomRoom)
            }
            .onChange(of: focus) { id in
                guard let id else { return }
                withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(id, anchor: .center) }
            }
            .onChange(of: category) { _ in proxy.scrollTo("top", anchor: .top) }
            .onChange(of: legacy) { _ in proxy.scrollTo("top", anchor: .top) }
        }
    }

    @ViewBuilder private func row(_ item: SettingsItem) -> some View {
        let focused = input.showsFocus && !sidebarFocused && focus == item.id
        rowContent(item)
            .disabled(!item.enabled)
            .opacity(item.enabled ? 1 : 0.45)
            .background(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .fill(EspressUTheme.pixelBlue.opacity(focused ? 0.09 : 0)))
            .overlay(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 3)
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
            if Self.usesSegments(options) {
                rowLabel(item, stacked: !isWide) {
                    segments(options, binding, id: item.id)
                }
            } else {
                Button { openDropdown(item) } label: {
                    rowLabel(item) {
                        HStack(spacing: 6) {
                            Text(options.first { $0.id == binding.wrappedValue }?.title ?? "")
                                .font(.system(size: 16, design: .rounded))
                                .foregroundColor(EspressUTheme.brownMid)
                                .multilineTextAlignment(.trailing)
                            Image(systemName: "chevron.up.chevron.down")
                                .font(.system(size: 13, weight: .semibold))
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
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .monospacedDigit()
                }
                Slider(value: binding, in: range, step: step)
                    .tint(EspressUTheme.pixelBlue)
                    .accessibilityLabel(item.title)
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 13)

        case .action(let title, let destructive, let confirm, let run):
            let isArmed = armed == item.id
            Button {
                focus = item.id
                press(item.id, confirm: confirm, run: run)
            } label: {
                rowLabel(item, titleColor: destructive ? EspressUTheme.alertText : nil) {
                    Text(isArmed ? "Press again" : title)
                        .font(.system(size: 15, weight: .bold, design: .rounded))
                        .foregroundColor(isArmed ? EspressUTheme.onAccent
                                         : (destructive ? EspressUTheme.alertText : EspressUTheme.accentText))
                        .padding(.horizontal, 16)
                        .padding(.vertical, 9)
                        .background(isArmed ? (destructive ? EspressUTheme.alertText : EspressUTheme.pixelBlue) : EspressUTheme.wrapper,
                                    in: Capsule())
                }
            }
            .buttonStyle(.plain)

        case .open(let value, let run):
            Button {
                focus = item.id
                run()
            } label: {
                rowLabel(item) {
                    HStack(spacing: 6) {
                        if !value.isEmpty {
                            Text(value)
                                .font(.system(size: 16, design: .rounded))
                                .foregroundColor(EspressUTheme.brownMid)
                        }
                        Image(systemName: "chevron.right")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                }
            }
            .buttonStyle(.plain)

        case .link(let value, let page):
            Button {
                focus = item.id
                openLegacy(page)
            } label: {
                rowLabel(item) {
                    HStack(spacing: 6) {
                        if !value.isEmpty {
                            Text(value)
                                .font(.system(size: 16, design: .rounded))
                                .foregroundColor(EspressUTheme.brownMid)
                        }
                        Image(systemName: "chevron.right")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                }
            }
            .buttonStyle(.plain)

        case .info(let value, let tint):
            rowLabel(item) {
                Text(value)
                    .font(.system(size: 16, weight: .semibold, design: .rounded))
                    .foregroundColor(tint ?? EspressUTheme.brownMid)
                    .multilineTextAlignment(.trailing)
            }

        case .custom(let view):
            rowLabel(item) { view }

        case .modeSwitch(let binding):
            JITModeSwitch(isOn: binding.wrappedValue) { on in
                focus = item.id
                binding.wrappedValue = on
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 12)

        case .banner(let view):
            view
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(18)

        case .padButton(let label, let value, let changed, let run, let reset):
            HStack(spacing: 6) {
                Button {
                    focus = item.id
                    run()
                } label: {
                    HStack(spacing: 14) {
                        Text(label)
                            .font(.system(size: 15, weight: .heavy, design: .rounded))
                            .foregroundColor(EspressUTheme.brownDarkest)
                            .minimumScaleFactor(0.6)
                            .lineLimit(1)
                            .frame(width: 38, height: 38)
                            .background(Circle().fill(EspressUTheme.pixelBlue.opacity(changed ? 0.3 : 0.14)))
                        VStack(alignment: .leading, spacing: 2) {
                            Text(item.title)
                                .font(.system(size: 17, weight: .bold, design: .rounded))
                                .foregroundColor(EspressUTheme.brownDarkest)
                            if let detail = item.detail, !detail.isEmpty {
                                Text(detail)
                                    .font(.system(size: 13, design: .rounded))
                                    .foregroundColor(EspressUTheme.brownMid)
                            }
                        }
                        Spacer(minLength: 8)
                        Text(value)
                            .font(.system(size: 15, weight: changed ? .bold : .regular, design: .rounded))
                            .foregroundColor(changed ? EspressUTheme.accentText : EspressUTheme.brownMid)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 7)
                            .background(Capsule().fill(changed ? EspressUTheme.pixelBlue.opacity(0.14) : EspressUTheme.wrapper.opacity(0.7)))
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                if let reset {
                    Button {
                        focus = item.id
                        reset()
                    } label: {
                        Image(systemName: "arrow.counterclockwise")
                            .font(.system(size: 15, weight: .bold))
                            .foregroundColor(EspressUTheme.brownMid)
                            .frame(width: 40, height: 40)
                            .background(Circle().fill(EspressUTheme.wrapper.opacity(0.7)))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Reset \(item.title)")
                }
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 9)
            .frame(minHeight: isWide ? 64 : 56)

        case .expand(let open):
            Button {
                focus = item.id
                toggleExpanded()
            } label: {
                rowLabel(item) {
                    Image(systemName: "chevron.down")
                        .font(.system(size: 14, weight: .bold))
                        .foregroundColor(EspressUTheme.brownMid)
                        .rotationEffect(.degrees(open ? 180 : 0))
                }
            }
            .buttonStyle(.plain)
        }
    }

    private func rowLabel<Trailing: View>(_ item: SettingsItem, stacked: Bool = false, padded: Bool = true,
                                          titleColor: Color? = nil,
                                          @ViewBuilder trailing: () -> Trailing) -> some View {
        let text = VStack(alignment: .leading, spacing: 3) {
            Text(item.title)
                .font(.system(size: 17, weight: .bold, design: .rounded))
                .foregroundColor(titleColor ?? EspressUTheme.brownDarkest)
                .fixedSize(horizontal: false, vertical: true)
            if let detail = notes[item.id] ?? item.detail, !detail.isEmpty {
                Text(detail)
                    .font(.system(size: 14, design: .rounded))
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
        .padding(.horizontal, padded ? 18 : 0)
        .padding(.vertical, padded ? (isWide ? 13 : 11) : 0)
        .frame(minHeight: padded ? (isWide ? 64 : 56) : 0)
        .contentShape(Rectangle())
    }

    static func usesSegments(_ options: [SettingsOption]) -> Bool {
        options.count <= 3 && options.reduce(0) { $0 + $1.title.count } <= 30
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
                        .font(.system(size: 14, weight: .bold, design: .rounded))
                        .foregroundColor(selected ? EspressUTheme.brownDarkest : EspressUTheme.brownMid)
                        .lineLimit(1)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 7)
                        .background(selected ? EspressUTheme.cream : Color.clear,
                                    in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                        .shadow(color: EspressUTheme.shadow.opacity(selected ? 0.12 : 0), radius: 2, y: 1)
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? .isSelected : [])
            }
        }
        .padding(3)
        .background(EspressUTheme.wrapper, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    private func legacyPage(_ page: LegacySettingsPage) -> some View {
        NavigationView {
            legacyContent(page)
                .navigationTitle(page.title)
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .navigationBarLeading) {
                        Button { closeLegacy() } label: {
                            Label("Back", systemImage: "chevron.left")
                                .labelStyle(.titleAndIcon)
                                .font(.system(size: 16, weight: .semibold, design: .rounded))
                        }
                        .foregroundColor(EspressUTheme.accentText)
                    }
                }
        }
        .navigationViewStyle(.stack)
        .clipShape(RoundedRectangle(cornerRadius: 26, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 26, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
    }

    @ViewBuilder private func legacyContent(_ page: LegacySettingsPage) -> some View {
        switch page {
        case .graphicPacks:
            GraphicPacksView()
        case .figureManager:
            EmulatedDevicesView()
        default:
            Form { legacySections(page) }
                .settingsPageBackground()
        }
    }

    @ViewBuilder private func legacySections(_ page: LegacySettingsPage) -> some View {
        switch page {
        case .emulatedClock: EmulatedClockSection()
        case .figures: EmulatedDevicesSettingsSection()
        case .overlay:
            OverlaySettingsSection()
            NotificationSettingsSection()
        case .graphicPacks: EmptyView()
        case .linkedFolders:
            Section { LinkedLocationsRows(gameManager: gameManager) }
        case .keys: KeysSettingsSection()
        case .files: FilesSettingsSection()
        case .backup: SettingsBackupSection()
        case .account:
            AccountSettingsSection()
            NetworkServiceSettingsSection()
        case .diagnostics:
            DeviceReportSection()
            DiagnosticsSection()
        case .about: AboutSettingsSection()
        case .networkService: CustomServersSettingsSection()
        case .figureManager, .licences, .graphicPackGame, .graphicPack, .controllerMapping: EmptyView()
        }
    }

    private var phoneBottomRoom: CGFloat { input.showsFocus ? 96 : 24 }

    private var sidebarHints: some View {
        VStack(alignment: .leading, spacing: 10) {
            Rectangle()
                .fill(EspressUTheme.wrapper)
                .frame(height: 1)
                .padding(.bottom, 4)
            if legacy == nil {
                ControllerButtonHint(symbol: input.confirmSymbol, text: hintConfirm)
            } else {
                ControllerButtonHint(symbol: "dpad", text: "Scroll")
            }
            ControllerButtonHint(symbol: input.backSymbol, text: hintBack)
            if legacy == nil {
                HStack(spacing: 4) {
                    Image(systemName: leftShoulderSymbol)
                        .font(.system(size: 17, weight: .semibold))
                        .foregroundColor(EspressUTheme.brownMid)
                    ControllerButtonHint(symbol: rightShoulderSymbol, text: "Category")
                }
                .accessibilityHidden(true)
            }
        }
        .padding(.horizontal, 12)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private func bottomBar(wide: Bool) -> some View {
        if input.showsFocus {
            HStack(spacing: 20) {
                if legacy == nil {
                    ControllerButtonHint(symbol: input.confirmSymbol, text: hintConfirm)
                } else {
                    ControllerButtonHint(symbol: "dpad", text: "Scroll")
                }
                ControllerButtonHint(symbol: input.backSymbol, text: hintBack)
                if legacy == nil && (wide || showingCategory) {
                    HStack(spacing: 4) {
                        Image(systemName: leftShoulderSymbol)
                            .font(.system(size: 17, weight: .semibold))
                            .foregroundColor(EspressUTheme.brownMid)
                        ControllerButtonHint(symbol: rightShoulderSymbol, text: "Category")
                    }
                    .accessibilityHidden(true)
                }
            }
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .padding(.horizontal, 24)
            .frame(minHeight: 56)
            .espressUSurface(Capsule())
            .padding(.bottom, 12)
            .transition(.opacity)
        }
    }

    private func closeButton(wide: Bool) -> some View {
        Button(action: close) {
            Image(systemName: "xmark")
                .font(.system(size: 17, weight: .bold))
                .foregroundColor(EspressUTheme.brownDarkest)
                .frame(width: 44, height: 44)
                .espressUSurface(Circle())
                .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Close Settings")
        .padding(.top, wide ? 22 : 12)
        .padding(.trailing, wide ? 28 : 16)
    }

    private var hintConfirm: String {
        if !isWide && !showingCategory { return "Open" }
        return sidebarFocused && isWide ? "Open" : "Change"
    }

    private var hintBack: String {
        if legacy != nil { return "Back" }
        if isWide { return sidebarFocused ? "Close" : "Back" }
        return showingCategory ? "Back" : "Close"
    }

    private var leftShoulderSymbol: String {
        input.controllers.first?.extendedGamepad?.leftShoulder.sfSymbolsName ?? "l1.rectangle.roundedbottom"
    }

    private var rightShoulderSymbol: String {
        input.controllers.first?.extendedGamepad?.rightShoulder.sfSymbolsName ?? "r1.rectangle.roundedbottom"
    }

    func pick(_ item: SettingsCategory) {
        legacy = nil
        dropdown = nil
        armed = nil
        category = item
        focus = nil
    }

    func openCategory(_ item: SettingsCategory) {
        pick(item)
        gridFocus = "cat.\(item.rawValue)"
        showingCategory = true
        sidebarFocused = false
        focus = input.showsFocus ? focusOrder().first : nil
    }

    func closeCategory() {
        showingCategory = false
        focus = nil
        armed = nil
    }

    func openLegacy(_ page: LegacySettingsPage) {
        armed = nil
        dropdown = nil
        if let focus { returnFocus[page] = focus }
        legacy = page
        subpageOpened(page)
        focus = input.showsFocus ? focusOrder().first : nil
    }

    func closeLegacy() {
        ControllerMappingSession.shared.cancel()
        let page = legacy
        dropdown = nil
        legacy = page?.parent
        if let page, let opener = returnFocus.removeValue(forKey: page) {
            focus = opener
        } else if let page, let row = currentGroups().flatMap(\.items).first(where: {
            if case .link(_, let target) = $0.control { return target == page }
            return false
        }) {
            focus = row.id
        } else {
            focus = nil
        }
    }

    func currentGroups() -> [SettingsGroup] {
        if let legacy, let native = subpageGroups(legacy) { return native }
        return groups(for: category)
    }

    func openDropdown(_ item: SettingsItem) {
        focus = item.id
        dropdown = SettingsDropdown.open(item)
    }

    func toggleExpanded() {
        if expanded.contains(category) { expanded.remove(category) } else { expanded.insert(category) }
    }

    func press(_ id: String, confirm: Bool, run: () -> Void) {
        guard confirm else {
            run()
            return
        }
        if armed == id {
            armed = nil
            run()
        } else {
            armed = id
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                if armed == id { armed = nil }
            }
        }
    }

    func focusOrder() -> [String] {
        currentGroups().flatMap(\.items).filter(\.isFocusable).map(\.id)
    }

    func handle(_ command: ControllerMenuCommand) {
        if Self.coveredByAnotherScreen {
            ControllerScreenCursor.shared.handle(command)
            return
        }
        ControllerScreenCursor.shared.reset()
        if dropdown != nil {
            SettingsDropdown.handle(command, state: $dropdown, groups: currentGroups())
        } else if let legacy, subpageGroups(legacy) != nil {
            handleRows(command, onLeftEdge: nil, onBack: closeLegacy)
        } else if legacy != nil {
            handleLegacy(command)
        } else if isWide {
            handleWide(command)
        } else if showingCategory {
            handleRows(command, onLeftEdge: nil, onBack: closeCategory)
        } else {
            handleGrid(command)
        }
    }

    private func handleWide(_ command: ControllerMenuCommand) {
        switch command {
        case .previousPage, .nextPage:
            switchCategory(by: command == .nextPage ? 1 : -1)
            return
        case .menu:
            close()
            return
        default:
            break
        }
        if sidebarFocused {
            switch command {
            case .up: switchCategory(by: -1, wrap: false)
            case .down: switchCategory(by: 1, wrap: false)
            case .right, .confirm:
                sidebarFocused = false
                focus = focusOrder().first
            case .back: close()
            default: break
            }
        } else {
            handleRows(command, onLeftEdge: { sidebarFocused = true }, onBack: { sidebarFocused = true })
        }
    }

    private func handleRows(_ command: ControllerMenuCommand, onLeftEdge: (() -> Void)?, onBack: () -> Void) {
        let order = focusOrder()
        let current = focus.flatMap { id in order.contains(id) ? id : nil }
        let item = current.flatMap { id in currentGroups().flatMap(\.items).first { $0.id == id } }
        switch command {
        case .up, .down:
            guard let current, let index = order.firstIndex(of: current) else {
                focus = order.first
                return
            }
            let next = index + (command == .down ? 1 : -1)
            if order.indices.contains(next) { focus = order[next] }
        case .left, .right:
            if let item, adjust(item, by: command == .right ? 1 : -1) { return }
            if command == .left { onLeftEdge?() }
        case .confirm:
            guard let item else {
                focus = order.first
                return
            }
            activate(item)
        case .back:
            onBack()
        case .previousPage, .nextPage:
            switchCategory(by: command == .nextPage ? 1 : -1)
            focus = focusOrder().first
        case .menu:
            close()
        case .alternate:
            if let item, case .padButton(_, _, _, _, let reset?) = item.control { reset() }
        case .select:
            break
        }
    }

    private func handleGrid(_ command: ControllerMenuCommand) {
        let ids = SettingsCategory.allCases.map { "cat.\($0.rawValue)" }
        let grid = ControllerFocusGrid(rows: ids.map { [$0] })
        switch command {
        case .up, .down, .left, .right:
            if let next = grid.move(gridFocus, command) { gridFocus = next }
        case .confirm:
            if let index = Int(gridFocus.dropFirst(4)), let item = SettingsCategory(rawValue: index) {
                openCategory(item)
            }
        case .back, .menu:
            close()
        default:
            break
        }
    }

    private func handleLegacy(_ command: ControllerMenuCommand) {
        switch command {
        case .up, .down:
            Self.scrollLegacyPage(down: command == .down)
        case .back:
            if !Self.popLegacyNavigation() { closeLegacy() }
        case .menu:
            close()
        default:
            break
        }
    }

    private func switchCategory(by step: Int, wrap: Bool = true) {
        let all = SettingsCategory.allCases
        var index = category.rawValue + step
        if wrap {
            index = (index + all.count) % all.count
        } else {
            index = min(max(index, 0), all.count - 1)
        }
        guard let next = SettingsCategory(rawValue: index), next != category || legacy != nil else { return }
        pick(next)
        gridFocus = "cat.\(index)"
    }

    private func activate(_ item: SettingsItem) {
        switch item.control {
        case .toggle(let binding), .modeSwitch(let binding):
            binding.wrappedValue.toggle()
        case .choice(let options, let binding):
            guard !options.isEmpty else { return }
            if !Self.usesSegments(options) {
                openDropdown(item)
                return
            }
            let index = options.firstIndex { $0.id == binding.wrappedValue } ?? -1
            binding.wrappedValue = options[(index + 1) % options.count].id
        case .slider:
            break
        case .action(_, _, let confirm, let run):
            press(item.id, confirm: confirm, run: run)
        case .link(_, let page):
            openLegacy(page)
        case .open(_, let run):
            run()
        case .expand:
            toggleExpanded()
        case .padButton(_, _, _, let run, _):
            run()
        case .info, .custom, .banner:
            break
        }
    }

    private func adjust(_ item: SettingsItem, by direction: Int) -> Bool {
        switch item.control {
        case .padButton(_, _, _, _, let reset?):
            guard direction > 0 else { return false }
            reset()
            return true
        case .modeSwitch(let binding):
            let on = direction < 0
            if binding.wrappedValue != on { binding.wrappedValue = on }
            return true
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

    private static var settingsController: UIViewController? {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
        let window = windows.first { $0.isKeyWindow } ?? windows.first
        return window?.rootViewController?.presentedViewController
    }

    static var coveredByAnotherScreen: Bool {
        guard let above = settingsController?.presentedViewController else { return false }
        return !above.isBeingDismissed
    }

    static func popLegacyNavigation() -> Bool {
        guard let root = settingsController else { return false }
        func find(_ controller: UIViewController) -> UINavigationController? {
            if let navigation = controller as? UINavigationController { return navigation }
            for child in controller.children {
                if let found = find(child) { return found }
            }
            return nil
        }
        guard let navigation = find(root), navigation.viewControllers.count > 1 else { return false }
        navigation.popViewController(animated: true)
        return true
    }

    static func scrollLegacyPage(down: Bool) {
        guard let root = settingsController?.view else { return }
        var best: UIScrollView?
        func search(_ view: UIView) {
            if let scroll = view as? UIScrollView, !view.isHidden, scroll.isScrollEnabled,
               scroll.contentSize.height > scroll.bounds.height + 1,
               scroll.bounds.width * scroll.bounds.height > (best.map { $0.bounds.width * $0.bounds.height } ?? 0) {
                best = scroll
            }
            view.subviews.forEach(search)
        }
        search(root)
        guard let scroll = best else { return }
        let inset = scroll.adjustedContentInset
        let maxY = max(-inset.top, scroll.contentSize.height - scroll.bounds.height + inset.bottom)
        let step = scroll.bounds.height * 0.45 * (down ? 1 : -1)
        let y = min(max(scroll.contentOffset.y + step, -inset.top), maxY)
        scroll.setContentOffset(CGPoint(x: scroll.contentOffset.x, y: y), animated: true)
    }
}

private extension View {
    @ViewBuilder func settingsPageBackground() -> some View {
        if #available(iOS 16.0, *) {
            self.scrollContentBackground(.hidden)
                .background(EspressUBackground().ignoresSafeArea())
        } else {
            self
        }
    }
}
