import SwiftUI
import Combine

enum HomeMenuDestination: String, CaseIterable, Identifiable {
    case resume
    case screenLayout
    case touchControls
    case moveControls
    case scanAmiibo
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
        case .scanAmiibo: return "Scan amiibo"
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
        case .scanAmiibo: return "wave.3.right"
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
        case .scanAmiibo: return "Opens a list of amiibo files to scan. The game has to be asking for an amiibo."
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
    let scanAmiibo: () -> Void
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
                card(availableHeight: proxy.size.height)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
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
        EspressUTheme.scrim
            .ignoresSafeArea()
            .contentShape(Rectangle())
            .onTapGesture { actions.resume() }
            .accessibilityHidden(true)
    }

    private func card(availableHeight: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 18) {
            header
            ScrollViewReader { reader in
                ScrollView(.vertical, showsIndicators: false) {
                    VStack(spacing: 10) {
                        ForEach(Array(rows.enumerated()), id: \.element.id) { index, row in
                            HomeMenuRowView(row: row, isFocused: usingController && index == focus)
                                .id(row.id)
                        }
                    }
                    .padding(4)
                }
                .onValueChange(of: focus) { _ in
                    let current = rows
                    if current.indices.contains(focus) {
                        withAnimation(.easeInOut(duration: 0.15)) { reader.scrollTo(current[focus].id) }
                    }
                }
            }
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 22)
        .frame(width: 340)
        .frame(maxHeight: .infinity)
        .background(.ultraThinMaterial)
        .overlay(alignment: .trailing) {
            Rectangle().fill(Color.white.opacity(0.12)).frame(width: 1)
        }
        .environment(\.colorScheme, .dark)
        .ignoresSafeArea()
        .transition(.move(edge: .leading))
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
                .font(.system(size: 30, weight: .heavy, design: .rounded))
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
        .padding(.top, 8)
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
        case .scanAmiibo: return make(actions.scanAmiibo)
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
                                      hint: "Switches which Wii U screen is shown.", action: actions.swapScreens))
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
            HStack(spacing: 14) {
                Image(systemName: row.symbol)
                    .font(.system(size: 19, weight: .semibold))
                    .foregroundColor(iconColor)
                    .frame(width: 40, height: 40)
                    .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(iconFill))
                    .accessibilityHidden(true)
                Text(row.title)
                    .font(.system(size: 18, weight: .semibold, design: .rounded))
                    .foregroundColor(row.isDestructive ? EspressUTheme.alertText : .white)
                    .lineLimit(1)
                Spacer(minLength: 4)
                if row.isSelected {
                    Image(systemName: "checkmark").foregroundColor(EspressUTheme.pixelBlue)
                } else if row.showsChevron {
                    Image(systemName: "chevron.right").foregroundColor(.white.opacity(0.4))
                }
            }
            .padding(.horizontal, 12)
            .frame(maxWidth: .infinity, minHeight: 60, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .fill(row.isPrimary ? EspressUTheme.pixelBlue.opacity(0.35) : Color.white.opacity(isFocused ? 0.2 : 0.08))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 3)
                    .opacity(isFocused ? 1 : 0)
            )
            .contentShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(row.title)
        .accessibilityHint(row.hint)
        .accessibilityAddTraits(row.isSelected ? .isSelected : [])
    }

    private var iconFill: Color {
        if row.isPrimary { return EspressUTheme.pixelBlue }
        if row.isDestructive { return EspressUTheme.alertText.opacity(0.18) }
        return Color.white.opacity(0.12)
    }

    private var iconColor: Color {
        if row.isPrimary { return .white }
        return row.isDestructive ? EspressUTheme.alertText : .white
    }
}
