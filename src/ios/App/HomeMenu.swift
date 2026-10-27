import SwiftUI
import Combine

enum HomeMenuDestination: String, CaseIterable, Identifiable {
    case resume
    case screenLayout
    case touchControls
    case moveControls
    case emulatedDevices
    case recentreAim
    case quit

    var id: String { rawValue }

    static var visible: [HomeMenuDestination] {
        let motionOn = UserDefaults.standard.object(forKey: MotionSettings.enabledKey) as? Bool
            ?? MotionSettings.defaultEnabled
        return allCases.filter { $0 != .recentreAim || motionOn }
    }

    var title: String {
        switch self {
        case .resume: return "Resume"
        case .screenLayout: return "Screen layout"
        case .touchControls: return "Touch controls"
        case .moveControls: return "Move controls"
        case .emulatedDevices: return "Emulated devices"
        case .recentreAim: return "Recentre aim"
        case .quit: return "Quit game"
        }
    }

    var symbol: String {
        switch self {
        case .resume: return "play.fill"
        case .screenLayout: return "rectangle.split.2x1"
        case .touchControls: return "hand.raised.fill"
        case .moveControls: return "arrow.up.and.down.and.arrow.left.and.right"
        case .emulatedDevices: return "externaldrive.connected.to.line.below"
        case .recentreAim: return "scope"
        case .quit: return "xmark.circle"
        }
    }

    var hint: String {
        switch self {
        case .resume: return "Closes the menu and carries on with the game."
        case .screenLayout: return "Opens the screen layout choices."
        case .touchControls: return "Shows or hides the on-screen controls, for example to touch the GamePad screen."
        case .moveControls: return "Closes the menu and lets you drag the on-screen controls."
        case .emulatedDevices: return "Opens the figure portals that are turned on in Settings."
        case .recentreAim: return "Takes the way you are holding the device now as straight ahead, then goes back to the game."
        case .quit: return "Asks before leaving the game."
        }
    }
}

enum HomeMenuEvent {
    case homeButton
    case up
    case down
    case confirm
    case back
}

final class HomeMenuRouter {
    static let shared = HomeMenuRouter()

    let events = PassthroughSubject<HomeMenuEvent, Never>()

    func padHome(pressed: Bool) {
        if pressed { post(.homeButton) }
    }

    func post(_ event: HomeMenuEvent) {
        DispatchQueue.main.async { self.events.send(event) }
    }

    func connectToBridge() {
        cemu_bridge_set_menu_input_callback { event in
            if event == CEMU_BRIDGE_MENU_HOME {
                HomeMenuRouter.shared.post(.homeButton)
            } else if event == CEMU_BRIDGE_MENU_UP {
                HomeMenuRouter.shared.post(.up)
            } else if event == CEMU_BRIDGE_MENU_DOWN {
                HomeMenuRouter.shared.post(.down)
            } else if event == CEMU_BRIDGE_MENU_CONFIRM {
                HomeMenuRouter.shared.post(.confirm)
            } else if event == CEMU_BRIDGE_MENU_BACK {
                HomeMenuRouter.shared.post(.back)
            }
        }
    }
}

struct HomeMenuEventsModifier: ViewModifier {
    let isOpen: Bool
    let onEvent: (HomeMenuEvent) -> Void

    func body(content: Content) -> some View {
        content
            .onAppear { HomeMenuRouter.shared.connectToBridge() }
            .onReceive(HomeMenuRouter.shared.events) { onEvent($0) }
            .onValueChange(of: isOpen) { open in cemu_bridge_set_menu_capture(open) }
            .onDisappear { cemu_bridge_set_menu_capture(false) }
    }
}

struct HomeMenuActions {
    let resume: () -> Void
    let moveControls: () -> Void
    let quit: () -> Void
    let swapScreens: () -> Void
    let toggleTouchControls: () -> Void
    let emulatedDevices: () -> Void
}

private struct HomeMenuRow: Identifiable {
    let id: String
    let title: String
    let symbol: String
    var hint: String = ""
    var isSelected = false
    var isPrimary = false
    var isDestructive = false
    var showsChevron = false
    var showsSeparator: Bool { !isPrimary && !isDestructive }
    let action: () -> Void
}

private struct HomeRowsHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

struct HomeMenuOverlay: View {
    let gameName: String
    let isActive: Bool
    @Binding var screenLayout: ScreenLayout
    let isDualScreen: Bool
    let canQuit: Bool
    let touchControlsHidden: Bool?
    let offersEmulatedDevices: Bool
    let actions: HomeMenuActions

    private enum Page { case root, layout, confirmQuit }
    @State private var page = Page.root
    @State private var focus = 0
    @State private var rowsHeight: CGFloat = 280
    @State private var usingController = ControllerPresence.isConnected()

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                scrim
                card(in: proxy)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
            }
        }
        .transition(.opacity)
        .accessibilityElement(children: .contain)
        .accessibilityAddTraits(.isModal)
        .accessibilityAction(.escape) { goBack() }
        .onReceive(HomeMenuRouter.shared.events) { handle($0) }
        .onAppear { UIAccessibility.post(notification: .screenChanged, argument: nil) }
    }

    private var scrim: some View {
        Color.black.opacity(0.28)
            .ignoresSafeArea()
            .contentShape(Rectangle())
            .onTapGesture { actions.resume() }
            .accessibilityHidden(true)
    }

    /// A floating glass panel inset from the screen edges, sized to its content, instead of a full-height slab.
    private func card(in proxy: GeometryProxy) -> some View {
        let insets = proxy.safeAreaInsets
        let margin: CGFloat = 12
        let chrome: CGFloat = 150
        let maxRows = max(160, proxy.size.height - insets.top - insets.bottom - margin * 2 - chrome)
        return VStack(alignment: .leading, spacing: 14) {
            header
            ScrollViewReader { reader in
                ScrollView(.vertical, showsIndicators: false) {
                    VStack(spacing: 0) {
                        let current = rows
                        ForEach(Array(current.enumerated()), id: \.element.id) { index, row in
                            HomeMenuRowView(row: row, isFocused: usingController && index == focus)
                                .id(row.id)
                            if index < current.count - 1, row.showsSeparator, current[index + 1].showsSeparator {
                                Rectangle()
                                    .fill(Color.white.opacity(0.14))
                                    .frame(height: EspressUTheme.hairlineWidth)
                                    .padding(.leading, 52)
                            }
                        }
                    }
                    .background(GeometryReader { Color.clear.preference(key: HomeRowsHeightKey.self, value: $0.size.height) })
                }
                .frame(height: min(rowsHeight, maxRows))
                .onPreferenceChange(HomeRowsHeightKey.self) { rowsHeight = $0 }
                .onValueChange(of: focus) { _ in
                    let current = rows
                    if current.indices.contains(focus) {
                        withAnimation(.easeInOut(duration: 0.15)) { reader.scrollTo(current[focus].id) }
                    }
                }
            }
        }
        .padding(.horizontal, 16)
        .padding(.top, 20)
        .padding(.bottom, 14)
        .frame(width: 330)
        .modifier(HomeMenuPanelBackground())
        .environment(\.colorScheme, .dark)
        .transition(.scale(scale: 0.94).combined(with: .opacity))
    }

    private var headerTitle: String {
        switch page {
        case .root: return "Paused"
        case .layout: return "Screen layout"
        case .confirmQuit: return "Quit game?"
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(headerTitle)
                .font(.title.weight(.bold))
                .foregroundColor(.white)
                .accessibilityAddTraits(.isHeader)
            switch page {
            case .root:
                Text(gameName)
                    .font(EspressUTheme.Font.caption)
                    .foregroundColor(.white.opacity(0.6))
                    .lineLimit(1)
            case .confirmQuit:
                Text("The game's own save is kept.")
                    .font(EspressUTheme.Font.caption)
                    .foregroundColor(.white.opacity(0.6))
                    .fixedSize(horizontal: false, vertical: true)
            case .layout:
                EmptyView()
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 4)
    }

    private var rows: [HomeMenuRow] {
        switch page {
        case .root: return rootRows
        case .layout: return layoutRows
        case .confirmQuit: return confirmQuitRows
        }
    }

    private var confirmQuitRows: [HomeMenuRow] {
        [
            HomeMenuRow(id: "keep", title: "Keep playing", symbol: "play.fill",
                        hint: "Goes back to the HOME menu without quitting.", isPrimary: true, action: { show(.root) }),
            HomeMenuRow(id: "quitNow", title: "Quit game", symbol: "xmark.circle",
                        hint: "Leaves the game now.", isDestructive: true, action: actions.quit)
        ]
    }

    private var rootRows: [HomeMenuRow] {
        HomeMenuDestination.visible.compactMap { rootRow(for: $0) }
    }

    private func rootRow(for destination: HomeMenuDestination) -> HomeMenuRow? {
        func make(primary: Bool = false, chevron: Bool = false, destructive: Bool = false,
                  _ action: @escaping () -> Void) -> HomeMenuRow {
            HomeMenuRow(id: destination.id, title: destination.title, symbol: destination.symbol,
                        hint: destination.hint, isPrimary: primary, isDestructive: destructive,
                        showsChevron: chevron, action: action)
        }
        switch destination {
        case .resume: return make(primary: true, actions.resume)
        case .screenLayout: return make(chevron: true, { show(.layout) })
        case .touchControls:
            guard let hidden = touchControlsHidden else { return nil }
            return HomeMenuRow(id: destination.id, title: hidden ? "Show touch controls" : "Hide touch controls",
                               symbol: hidden ? "hand.raised.fill" : "hand.raised.slash.fill",
                               hint: destination.hint, action: actions.toggleTouchControls)
        case .moveControls: return make(actions.moveControls)
        case .emulatedDevices: return offersEmulatedDevices ? make(actions.emulatedDevices) : nil
        case .recentreAim:
            return make {
                cemu_bridge_motion_recenter()
                actions.resume()
            }
        case .quit:
            return canQuit ? make(destructive: true, { show(.confirmQuit) }) : nil
        }
    }

    private var layoutRows: [HomeMenuRow] {
        var result: [HomeMenuRow] = []
        if !isDualScreen {
            for layout in ScreenLayout.allCases {
                result.append(HomeMenuRow(id: layout.rawValue, title: layout.string, symbol: Self.symbol(for: layout),
                                          hint: layout.description, isSelected: screenLayout == layout,
                                          action: { screenLayout = layout }))
            }
        }
        if isDualScreen || screenLayout == .singleScreen {
            result.append(HomeMenuRow(id: "swap", title: "Swap TV and GamePad", symbol: "rectangle.2.swap",
                                      hint: "Switches which screen is shown.", action: actions.swapScreens))
        }
        result.append(HomeMenuRow(id: "back", title: "Back", symbol: "chevron.left",
                                  hint: "Returns to the HOME menu.", showsChevron: false, action: { show(.root) }))
        return result
    }

    private static func symbol(for layout: ScreenLayout) -> String {
        switch layout {
        case .singleScreen: return "rectangle"
        case .bothScreens: return "rectangle.split.1x2"
        case .smallGamePadTopRight: return "rectangle.inset.topright.filled"
        }
    }

    private func show(_ next: Page) {
        let previous = page
        page = next
        switch next {
        case .root:
            let opener: HomeMenuDestination = previous == .confirmQuit ? .quit : .screenLayout
            focus = rootRows.firstIndex(where: { $0.id == opener.id }) ?? 0
        case .layout:
            focus = layoutRows.firstIndex(where: { $0.isSelected }) ?? 0
        case .confirmQuit:
            focus = 0
        }
    }

    private func goBack() {
        if page != .root { show(.root) } else { actions.resume() }
    }

    private func handle(_ event: HomeMenuEvent) {
        guard isActive else { return }
        let count = rows.count
        switch event {
        case .up:
            usingController = true
            focus = (min(focus, count - 1) + count - 1) % count
        case .down:
            usingController = true
            focus = (min(focus, count - 1) + 1) % count
        case .confirm:
            usingController = true
            let current = rows
            if current.indices.contains(focus) { current[focus].action() }
        case .back:
            goBack()
        case .homeButton:
            break
        }
    }
}

private struct HomeMenuRowView: View {
    let row: HomeMenuRow
    let isFocused: Bool

    var body: some View {
        Button(action: row.action) {
            if row.isPrimary { primaryLabel } else { plainLabel }
        }
        .buttonStyle(.plain)
        .padding(.vertical, row.isPrimary ? 4 : 0)
        .accessibilityLabel(row.title)
        .accessibilityHint(row.hint)
        .accessibilityAddTraits(row.isSelected ? .isSelected : [])
    }

    /// The one prominent action: a capsule in the accent colour, like a system default button.
    private var primaryLabel: some View {
        HStack(spacing: 8) {
            Image(systemName: row.symbol)
                .font(.body.weight(.semibold))
                .accessibilityHidden(true)
            Text(row.title)
                .font(.body.weight(.semibold))
        }
        .foregroundColor(EspressUTheme.onCremaTop)
        .frame(maxWidth: .infinity, minHeight: 50)
        .modifier(HomeMenuPrimaryBackground())
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.9), lineWidth: 2).opacity(isFocused ? 1 : 0))
        .contentShape(Capsule())
    }

    private var plainLabel: some View {
        HStack(spacing: 12) {
            Image(systemName: row.symbol)
                .font(.system(size: 18, weight: .regular))
                .foregroundColor(row.isDestructive ? EspressUTheme.alertText : .white.opacity(0.85))
                .frame(width: 28, height: 28)
                .accessibilityHidden(true)
            Text(row.title)
                .font(.body)
                .foregroundColor(row.isDestructive ? EspressUTheme.alertText : .white)
                .lineLimit(1)
            Spacer(minLength: 4)
            if row.isSelected {
                Image(systemName: "checkmark")
                    .font(.body.weight(.semibold))
                    .foregroundColor(.white)
            } else if row.showsChevron {
                Image(systemName: "chevron.right")
                    .font(.footnote.weight(.semibold))
                    .foregroundColor(.white.opacity(0.4))
            }
        }
        .padding(.horizontal, 10)
        .frame(maxWidth: .infinity, minHeight: 50, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color.white.opacity(isFocused ? 0.16 : 0))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Color.white.opacity(0.5), lineWidth: 1.5)
                .opacity(isFocused ? 1 : 0)
        )
        .contentShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}

private struct HomeMenuPrimaryBackground: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.glassEffect(.regular.tint(EspressUTheme.pixelBlue).interactive(), in: Capsule())
        } else {
            content.background(EspressUTheme.pixelBlue, in: Capsule())
        }
    }
}

private struct HomeMenuPanelBackground: ViewModifier {
    private let shape = RoundedRectangle(cornerRadius: 32, style: .continuous)

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.glassEffect(.regular, in: shape)
        } else {
            content
                .background(.ultraThinMaterial, in: shape)
                .overlay(shape.strokeBorder(Color.white.opacity(0.12), lineWidth: EspressUTheme.hairlineWidth))
        }
    }
}
