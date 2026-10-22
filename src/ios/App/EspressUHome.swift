// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UniformTypeIdentifiers
import UIKit

enum EspressUHome {
    static let enabledKey = "espressu.home.enabled"

    static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true
    }
}

struct EspressUHomeActions {
    let play: (GameMetadata) -> Void
    let toggleFavorite: (GameMetadata) -> Void
    let openGamePage: (GameMetadata) -> Void
    let renameGame: (GameMetadata) -> Void
    let changeCover: (GameMetadata) -> Void
    let removeGame: (GameMetadata) -> Void
    let openSettings: () -> Void
    let importFile: () -> Void
    let importFolder: () -> Void
    let importPickedGame: (URL) -> Void
    let importPickedContent: (URL, DlcUpdateImport.ContentKind) -> Void
    let importKeys: () -> Void
    let linkFolder: () -> Void
    let linkFile: () -> Void
    let importDLC: () -> Void
    let importUpdate: () -> Void
}

private struct HomeGridOffsetKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

struct EspressUHomeScreen<Lead: View, GameMenu: View>: View {
    @ObservedObject var gameManager: GameManager
    let games: [GameMetadata]
    @Binding var showingFavorites: Bool
    @Binding var searchText: String
    let activityText: String?
    let actions: EspressUHomeActions
    let lead: Lead
    let gameMenu: (GameMetadata) -> GameMenu

    init(gameManager: GameManager,
         games: [GameMetadata],
         showingFavorites: Binding<Bool>,
         searchText: Binding<String>,
         activityText: String?,
         actions: EspressUHomeActions,
         @ViewBuilder lead: () -> Lead,
         @ViewBuilder gameMenu: @escaping (GameMetadata) -> GameMenu) {
        self.gameManager = gameManager
        self.games = games
        self._showingFavorites = showingFavorites
        self._searchText = searchText
        self.activityText = activityText
        self.actions = actions
        self.lead = lead()
        self.gameMenu = gameMenu
    }

    @Environment(\.horizontalSizeClass) private var sizeClass
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @ObservedObject private var uiStyle = UIStyleStore.shared
    @ObservedObject private var playStats = LibraryPlayStats.shared
    @ObservedObject private var vpn = LocalDevVPNMonitor.shared
    @State private var gridAppeared = false
    @State private var keyboardUp = false
    @State private var selectedId: String?
    @State private var showingSearch = false
    @State private var showingAdd = false
    @State private var showingJIT = false
    @StateObject private var jitPanel = EspressUJITPanelModel()
    @State private var optionsGameId: String?
    @State private var optionsFocus = 0
    @State private var optionsArmed: String?
    @State private var importerMode: ImporterMode?
    @State private var importerPurpose: ImporterMode = .gameFolder

    private enum ImporterMode: Equatable {
        case gameFolder
        case content(DlcUpdateImport.ContentKind)
    }
    @State private var insets = EdgeInsets()
    @State private var screenSize = CGSize(width: 800, height: 600)
    @State private var addContentHeight: CGFloat = 400
    @StateObject private var pad = ControllerMenuInput()
    @State private var barFocus: Int?
    @State private var gameBeforeBar: String?
    @State private var addFocus: String?
    @State private var centerId: String?
    @State private var carouselDrag: CGFloat = 0
    @State private var gridAtTop = true

    private var isPhone: Bool { sizeClass == .compact }

    private var selectedGame: GameMetadata? {
        guard let selectedId else { return nil }
        return games.first { $0.id == selectedId }
    }

    var body: some View {
        let _ = (pad.onCommand = handlePad)
        ZStack(alignment: .bottom) {
            EspressUBackground()

            VStack(spacing: 0) {
                topBar
                    .padding(.top, insets.top + (isPhone ? 6 : 14))
                if showingSearch {
                    HStack(spacing: 10) {
                        SearchBarPolished(text: $searchText, autoFocus: isPhone)
                        if isPhone {
                            Button { closeSearch() } label: {
                                Text("Cancel")
                                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                                    .foregroundColor(EspressUTheme.accentText)
                                    .frame(minHeight: 44)
                                    .contentShape(Rectangle())
                            }
                            .transition(.opacity)
                        }
                    }
                    .padding(.horizontal, isPhone ? 16 : 40)
                    .padding(.top, isPhone ? 8 : 14)
                    .padding(.bottom, isPhone ? 18 : 28)
                    .gesture(isPhone ? DragGesture(minimumDistance: 16).onEnded { v in
                        if v.translation.height < -24 { closeSearch() }
                    } : nil)
                    .transition(isPhone
                        ? .opacity.combined(with: .offset(y: -28))
                        : .move(edge: .top).combined(with: .opacity))
                }
                lead
                library
            }
            .padding(.leading, insets.leading)
            .padding(.trailing, insets.trailing)

            if gameManager.isLoading || games.isEmpty {
                emptyState
                    .padding(.horizontal, 32)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            if showingAdd {
                Color.black.opacity(0.35)
                    .ignoresSafeArea()
                    .onTapGesture { showingAdd = false }
                    .transition(.opacity)
            }

            VStack(spacing: 12) {
                if showingAdd {
                    addPanel
                        .transition(.move(edge: .bottom).combined(with: .opacity))
                }
                EspressUMenuBar(items: barItems, compact: isPhone, focusedIndex: pad.showsFocus ? barFocus : nil)
                    .allowsHitTesting(!(isPhone && keyboardUp))
                    .opacity(isPhone && keyboardUp ? 0.4 : 1)
                    .overlay {
                        if isPhone && keyboardUp {
                            Color.clear.contentShape(Rectangle()).onTapGesture { tapAwayFromSearch() }
                        }
                    }
            }
            .padding(.horizontal, isPhone ? 16 : 40)
            .padding(.leading, insets.leading)
            .padding(.trailing, insets.trailing)
            .padding(.bottom, max(insets.bottom, 8) + (isPhone ? 0 : 8))

            if let game = optionsGame {
                Color.black.opacity(0.35)
                    .ignoresSafeArea()
                    .onTapGesture { closeOptions() }
                    .transition(.opacity)
                optionsPanel(game)
                    .padding(.horizontal, 16)
                    .padding(.leading, insets.leading)
                    .padding(.trailing, insets.trailing)
                    .padding(.bottom, isPhone ? max(insets.bottom, 12) : 0)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: isPhone ? .bottom : .center)
                    .transition(.scale(scale: 0.94).combined(with: .opacity))
            }

            if showingJIT {
                Color.black.opacity(0.35)
                    .ignoresSafeArea()
                    .onTapGesture { showingJIT = false }
                    .transition(.opacity)
                EspressUJITPanel(model: jitPanel, showsFocus: pad.showsFocus, backSymbol: pad.backSymbol,
                                 maxHeight: screenSize.height - insets.top - max(insets.bottom, 12) - 12)
                    .padding(.horizontal, 16)
                    .padding(.leading, insets.leading)
                    .padding(.trailing, insets.trailing)
                    .padding(.top, insets.top + 12)
                    .padding(.bottom, max(insets.bottom, 12))
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: isPhone ? .bottom : .center)
                    .transition(.scale(scale: 0.94).combined(with: .opacity))
            }
        }
        .ignoresSafeArea()
        .background(
            GeometryReader { geo in
                Color.clear
                    .onAppear {
                        measure(geo)
                        for delay in [0.15, 0.5, 1.2] {
                            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { measureInsets() }
                        }
                    }
                    .onReceive(NotificationCenter.default.publisher(for: UIApplication.didChangeStatusBarFrameNotification)) { _ in measureInsets() }
                    .onChange(of: geo.size) { _ in measure(geo) }
            }
        )
        .overlay(alignment: .top) {
            if let activityText {
                LibraryActivityBanner(text: activityText)
                    .padding(.top, insets.top + 8)
            }
        }
        .fileImporter(
            isPresented: Binding(get: { importerMode != nil }, set: { if !$0 { importerMode = nil } }),
            allowedContentTypes: importerPurpose == .gameFolder ? [.folder] : [.folder, .item]
        ) { result in
            let mode = importerPurpose
            importerMode = nil
            switch result {
            case .success(let url):
                cemu_bridge_log_checkpoint("fileImporter: picked \(url.path)")
                switch mode {
                case .gameFolder: actions.importPickedGame(url)
                case .content(let kind): actions.importPickedContent(url, kind)
                }
            case .failure(let error):
                cemu_bridge_log_checkpoint("fileImporter: failed \(error.localizedDescription)")
            }
        }
        .ignoresSafeArea(.keyboard, edges: isPhone ? .bottom : [])
        .animation(.spring(response: 0.35, dampingFraction: 0.85), value: selectedId)
        .animation(.spring(response: 0.35, dampingFraction: 0.85), value: showingAdd)
        .animation(.spring(response: 0.3, dampingFraction: 0.86), value: optionsGameId)
        .animation(.spring(response: 0.3, dampingFraction: 0.86), value: showingJIT)
        .animation(isPhone ? .spring(response: 0.5, dampingFraction: 0.9) : .easeInOut(duration: 0.2), value: showingSearch)
        .onChange(of: games.map(\.id)) { ids in
            if let selectedId, !ids.contains(selectedId) { self.selectedId = nil }
            pad.onCommand = handlePad
        }
        .onAppear {
            pad.onCommand = handlePad
            pad.isBlocked = { [gameManager] in
                switch gameManager.emulationState {
                case .idle, .error:
                    return ControllerMenuInput.exclusiveCount > 0 || ControllerScreenCursor.topIsOpaque
                case .loading, .running, .paused:
                    return true
                }
            }
            pad.everyFrame = { ControllerScreenCursor.shared.validate() }
            pad.start()
        }
        .onDisappear { pad.stop() }
        .onChange(of: showingAdd) { open in
            addFocus = open && pad.showsFocus ? addGroups.first?.1.first?.id : nil
            pad.onCommand = handlePad
        }
        .onChange(of: gameManager.isLoading) { _ in pad.onCommand = handlePad }
    }

    private func measure(_ geo: GeometryProxy) {
        measureInsets()
        if screenSize != geo.size { screenSize = geo.size }
    }

    private func measureInsets() {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
        let window = windows.first { $0.isKeyWindow } ?? windows.first
        let w = window?.safeAreaInsets ?? .zero
        let statusBar = (window?.windowScene?.statusBarManager?.statusBarFrame.height) ?? 0
        let measured = EdgeInsets(top: max(w.top, statusBar, UIDevice.current.userInterfaceIdiom == .pad ? 24 : 0), leading: w.left, bottom: w.bottom, trailing: w.right)
        if insets != measured { insets = measured }
    }

    private var topBar: some View {
        HStack(spacing: 12) {
            Image("AppIcon-preview")
                .resizable()
                .scaledToFill()
                .frame(width: isPhone ? 36 : 44, height: isPhone ? 36 : 44)
                .clipShape(RoundedRectangle(cornerRadius: isPhone ? 9 : 11, style: .continuous))
                .shadow(color: EspressUTheme.shadow.opacity(0.25), radius: 4, y: 2)
                .accessibilityHidden(true)
            Text("EspressU")
                .font(.system(size: isPhone ? 22 : 26, weight: .heavy, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
            Spacer(minLength: 8)
            if !(isPhone && verticalSizeClass == .regular) {
            TimelineView(.everyMinute) { context in
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    if !isPhone {
                        Text(context.date, format: .dateTime.weekday(.abbreviated).day().month(.abbreviated))
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                    Text(context.date, format: .dateTime.hour().minute())
                        .font(.system(size: isPhone ? 22 : 30, weight: .bold, design: .rounded))
                        .monospacedDigit()
                        .foregroundColor(EspressUTheme.brownDarkest)
                }
            }
            }
        }
        .padding(.horizontal, isPhone ? 20 : 40)
        .padding(.bottom, 12)
    }

    @ViewBuilder private var library: some View {
        if gameManager.isLoading || games.isEmpty {
            Spacer()
        } else {
            GeometryReader { geo in
                if isPhone {
                    phoneGrid(in: geo.size)
                } else {
                    carousel(in: geo.size)
                }
            }
        }
    }

    private var centerIndex: Int {
        if let id = selectedId ?? centerId ?? gameBeforeBar, let index = games.firstIndex(where: { $0.id == id }) {
            return index
        }
        return 0
    }

    private func carousel(in size: CGSize) -> some View {
        let center = centerIndex
        let reserve: CGFloat = isPhone ? 118 : 132
        let info: CGFloat = isPhone ? 118 : 126
        let height = max(160, size.height - reserve - info)
        let side = max(140, min(height, size.width * (isPhone ? 0.64 : 0.4)))
        let small: CGFloat = 0.62
        let gap: CGFloat = isPhone ? 16 : 34
        let step = side * (0.5 + small * 0.5) + gap
        let farStep = side * small + gap
        let drag = carouselDrag / step
        let game = games[min(center, games.count - 1)]
        let focused = pad.showsFocus && barFocus == nil && !showingAdd && selectedId != nil

        return VStack(spacing: 18) {
            ZStack {
                ForEach(Array(games.enumerated()), id: \.element.id) { index, item in
                    let position = CGFloat(index - center) + drag
                    let distance = abs(position)
                    if distance < 3.6 {
                        let near = min(distance, 1)
                        let x = (position < 0 ? -1 : 1) * (near * step + max(distance - 1, 0) * farStep)
                        let scale = 1 - (1 - small) * near
                        let isCenter = index == center
                        EspressUCover(game: item, radius: 26)
                            .frame(width: side, height: side)
                            .overlay(
                                RoundedRectangle(cornerRadius: 26, style: .continuous)
                                    .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 5)
                                    .opacity(isCenter && focused ? 1 : 0))
                            .shadow(color: isCenter ? EspressUTheme.shadow.opacity(0.35) : EspressUTheme.shadow.opacity(0.15),
                                    radius: isCenter ? 22 : 8, y: isCenter ? 10 : 4)
                            .contentShape(Rectangle())
                            .onTapGesture { if !isCenter { bringToCenter(index) } }
                        .scaleEffect(scale)
                        .opacity(1 - 0.22 * min(distance, 2.5))
                        .offset(x: x)
                        .zIndex(-Double(distance))
                        .accessibilityElement(children: .ignore)
                        .accessibilityLabel(item.cardName.name)
                        .accessibilityAddTraits(isCenter ? [] : .isButton)
                        .accessibilityHint(isCenter ? "" : "Brings the game to the middle.")
                    }
                }
            }
            .frame(width: size.width, height: side)
            .contentShape(Rectangle())
            .highPriorityGesture(
                DragGesture(minimumDistance: 10)
                    .onChanged { value in carouselDrag = value.translation.width }
                    .onEnded { value in
                        let predicted = value.predictedEndTranslation.width
                        let moved = Int((-predicted / step).rounded())
                        let target = min(max(center + moved, 0), games.count - 1)
                        withAnimation(.spring(response: 0.42, dampingFraction: 0.86)) {
                            carouselDrag = 0
                            bringToCenter(target, animated: false)
                        }
                    })

            VStack(spacing: 12) {
                VStack(spacing: 4) {
                    HStack(spacing: 14) {
                        if pad.showsFocus {
                            Image(systemName: pad.leftShoulderSymbol)
                                .font(.system(size: 20, weight: .semibold))
                                .foregroundColor(EspressUTheme.brownMid)
                                .opacity(center > 0 ? 1 : 0.3)
                                .accessibilityHidden(true)
                        }
                        Text(game.cardName.name)
                            .font(.system(size: isPhone ? 21 : 26, weight: .heavy, design: .rounded))
                            .foregroundColor(EspressUTheme.brownDarkest)
                            .lineLimit(1)
                            .minimumScaleFactor(0.7)
                        if pad.showsFocus {
                            Image(systemName: pad.rightShoulderSymbol)
                                .font(.system(size: 20, weight: .semibold))
                                .foregroundColor(EspressUTheme.brownMid)
                                .opacity(center < games.count - 1 ? 1 : 0.3)
                                .accessibilityHidden(true)
                        }
                    }
                    Text("\(center + 1) of \(games.count)" + (game.region.map { "  ·  \($0)" } ?? ""))
                        .font(.system(size: 14, weight: .semibold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .monospacedDigit()
                }
                HStack(spacing: 12) {
                    Button { actions.toggleFavorite(game) } label: {
                        sideButtonLabel(symbol: game.isFavorite ? "heart.fill" : "heart",
                                        tint: game.isFavorite ? EspressUTheme.alertText : EspressUTheme.brownDark,
                                        hint: pad.selectSymbol)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(game.isFavorite ? "Remove from favourites" : "Add to favourites")
                    Button { actions.play(game) } label: {
                        HStack(spacing: 6) {
                            if pad.showsFocus {
                                Image(systemName: pad.confirmSymbol)
                                    .accessibilityHidden(true)
                            }
                            Text("Start")
                        }
                        .font(.system(size: 17, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.onCremaTop)
                        .padding(.horizontal, 34)
                        .frame(height: 46)
                        .background(EspressUTheme.cremaTopGradient, in: Capsule())
                    }
                    .buttonStyle(.plain)
                    Button { openOptions(game) } label: {
                        sideButtonLabel(symbol: "ellipsis", tint: EspressUTheme.brownDark, hint: pad.menuSymbol, hintTrailing: true)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("More options for \(game.cardName.name)")
                }
            }
            .animation(nil, value: center)
        }
        .frame(width: size.width, height: size.height - reserve, alignment: .center)
        .animation(.spring(response: 0.42, dampingFraction: 0.86), value: center)
    }

    /// The carousel needs more width than a phone has, so phones get a plain two-column grid.
    /// Tap starts the game, long press opens favourite and options, and pulling the list down
    /// from the top opens search (there is no search button on phones).
    private func phoneGrid(in size: CGSize) -> some View {
        let columns = [GridItem(.flexible(), spacing: 14), GridItem(.flexible(), spacing: 14)]
        let resume: GameMetadata? = {
            guard let id = playStats.recentIDs(limit: 1).first else { return nil }
            return games.first { $0.id == id }
        }()
        return ScrollViewReader { scroller in
        ScrollView(showsIndicators: false) {
            GeometryReader { proxy in
                Color.clear.preference(key: HomeGridOffsetKey.self, value: proxy.frame(in: .named("homeGrid")).minY)
            }
            .frame(height: 0)
            VStack(spacing: 0) {
            if let resume {
                phoneResumeCard(resume)
                    .padding(.horizontal, 16)
                    .padding(.bottom, 20)
                    .frame(height: showingSearch ? 0 : 128, alignment: .top)
                    .clipped()
                    .opacity(showingSearch ? 0 : (gridAppeared ? 1 : 0))
                    .allowsHitTesting(!showingSearch)
            }
            LazyVGrid(columns: columns, spacing: 20) {
                ForEach(Array(games.enumerated()), id: \.element.id) { index, item in
                    Button {
                        UIImpactFeedbackGenerator(style: .light).impactOccurred()
                        actions.play(item)
                    } label: {
                        VStack(spacing: 10) {
                            EspressUCover(game: item, radius: 22)
                                .aspectRatio(1, contentMode: .fit)
                                .shadow(color: EspressUTheme.shadow.opacity(0.22), radius: 9, y: 5)
                                .overlay(alignment: .topTrailing) {
                                    if item.isFavorite {
                                        Image(systemName: "heart.fill")
                                            .font(.system(size: 13, weight: .bold))
                                            .foregroundColor(EspressUTheme.alertText)
                                            .padding(7)
                                            .background(.ultraThinMaterial, in: Circle())
                                            .padding(7)
                                    }
                                }
                                .overlay(alignment: .bottomTrailing) {
                                    Image(systemName: "play.fill")
                                        .font(.system(size: 11, weight: .bold))
                                        .foregroundColor(.white)
                                        .frame(width: 26, height: 26)
                                        .background(.black.opacity(0.35), in: Circle())
                                        .padding(8)
                                }
                            Text(item.cardName.name)
                                .font(.system(size: 14, weight: .bold, design: .rounded))
                                .foregroundColor(EspressUTheme.brownDarkest)
                                .lineLimit(2)
                                .multilineTextAlignment(.center)
                                .frame(maxWidth: .infinity, minHeight: 36, alignment: .top)
                        }
                    }
                    .buttonStyle(EspressUPressStyle())
                    .controllerFocusRing(pad.showsFocus && selectedId == item.id && barFocus == nil,
                                         shape: RoundedRectangle(cornerRadius: 24, style: .continuous))
                    .id(item.id)
                    .opacity(gridAppeared ? 1 : 0)
                    .offset(y: gridAppeared ? 0 : 16)
                    .animation(.spring(response: 0.45, dampingFraction: 0.85).delay(min(Double(index), 8) * 0.03), value: gridAppeared)
                    .contextMenu {
                        Button { actions.play(item) } label: { Label("Start", systemImage: "play.fill") }
                        Button { actions.toggleFavorite(item) } label: {
                            Label(item.isFavorite ? "Remove from favourites" : "Add to favourites",
                                  systemImage: item.isFavorite ? "heart.slash" : "heart")
                        }
                        Button { openOptions(item) } label: { Label("More options", systemImage: "ellipsis.circle") }
                    }
                    .accessibilityLabel(item.cardName.name)
                    .accessibilityHint("Starts the game.")
                }
            }
            .padding(.horizontal, 14)
            .padding(.bottom, max(insets.bottom, 8) + 110)
            }
        }
        .coordinateSpace(name: "homeGrid")
        .onChange(of: selectedId) { id in
            guard let id else { return }
            withAnimation(.easeInOut(duration: 0.25)) { scroller.scrollTo(id, anchor: .center) }
        }
        }
        .onAppear {
            withAnimation(.spring(response: 0.45, dampingFraction: 0.85)) { gridAppeared = true }
        }
        .onPreferenceChange(HomeGridOffsetKey.self) { gridAtTop = $0 >= -2 }
        .simultaneousGesture(
            DragGesture(minimumDistance: 24)
                .onEnded { value in
                    guard gridAtTop, !showingSearch, value.translation.height > 110,
                          abs(value.translation.width) * 2 < value.translation.height else { return }
                    showingAdd = false
                    withAnimation(.spring(response: 0.5, dampingFraction: 0.9)) { showingSearch = true }
                }
        )
        .frame(width: size.width, height: size.height)
        .overlay {
            if keyboardUp {
                Color.clear
                    .contentShape(Rectangle())
                    .onTapGesture { tapAwayFromSearch() }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillShowNotification)) { _ in
            keyboardUp = true
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillHideNotification)) { _ in
            keyboardUp = false
        }
    }

    /// A tap anywhere but the search bar: with an empty field it closes keyboard and bar,
    /// with text it only closes the keyboard so the results stay.
    private func tapAwayFromSearch() {
        if searchText.isEmpty {
            closeSearch()
        } else {
            UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        }
    }

    private func closeSearch() {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        withAnimation(.spring(response: 0.5, dampingFraction: 0.9)) { showingSearch = false }
        searchText = ""
    }

    private func phoneResumeCard(_ item: GameMetadata) -> some View {
        Button {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            actions.play(item)
        } label: {
            HStack(spacing: 14) {
                EspressUCover(game: item, radius: 18)
                    .frame(width: 84, height: 84)
                    .shadow(color: EspressUTheme.shadow.opacity(0.22), radius: 8, y: 4)
                VStack(alignment: .leading, spacing: 4) {
                    Text("Continue playing")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                    Text(item.cardName.name)
                        .font(.system(size: 17, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                        .lineLimit(2)
                        .multilineTextAlignment(.leading)
                }
                Spacer(minLength: 0)
                Image(systemName: "play.fill")
                    .font(.system(size: 16, weight: .bold))
                    .foregroundColor(.white)
                    .frame(width: 44, height: 44)
                    .background(EspressUTheme.accentText, in: Circle())
            }
            .padding(12)
            .frame(maxWidth: .infinity)
            .espressUSurface(RoundedRectangle(cornerRadius: 26, style: .continuous))
        }
        .buttonStyle(EspressUPressStyle())
        .contextMenu {
            Button { actions.play(item) } label: { Label("Start", systemImage: "play.fill") }
            Button { actions.toggleFavorite(item) } label: {
                Label(item.isFavorite ? "Remove from favourites" : "Add to favourites",
                      systemImage: item.isFavorite ? "heart.slash" : "heart")
            }
            Button { openOptions(item) } label: { Label("More options", systemImage: "ellipsis.circle") }
        }
        .accessibilityLabel("Continue playing \(item.cardName.name)")
    }

    private func sideButtonLabel(symbol: String, tint: Color, hint: String, hintTrailing: Bool = false) -> some View {
        let hintView = Image(systemName: hint)
            .font(.system(size: 17, weight: .semibold))
            .foregroundColor(EspressUTheme.brownMid)
            .accessibilityHidden(true)
        return HStack(spacing: 7) {
            if pad.showsFocus && !hintTrailing { hintView }
            Image(systemName: symbol)
                .font(.system(size: 18, weight: .bold))
                .foregroundColor(tint)
            if pad.showsFocus && hintTrailing { hintView }
        }
        .padding(.horizontal, pad.showsFocus ? 14 : 0)
        .frame(minWidth: 46, minHeight: 46)
        .espressUSurface(Capsule())
    }

    private func bringToCenter(_ index: Int, animated: Bool = true) {
        guard games.indices.contains(index) else { return }
        let id = games[index].id
        let change = {
            centerId = id
            if barFocus == nil { selectedId = id }
        }
        if animated {
            withAnimation(.spring(response: 0.42, dampingFraction: 0.86)) { change() }
        } else {
            change()
        }
    }

    @ViewBuilder private var emptyState: some View {
        if gameManager.isLoading {
            LoadingView()
        } else {
            let (symbol, heading, line): (String, String, String) = {
                if !searchText.isEmpty {
                    return ("magnifyingglass", "No matches", "Nothing matches \u{201C}\(searchText)\u{201D}.")
                }
                if showingFavorites {
                    return ("heart", "No favourites yet", "Long-press a game and choose Favourite to see it here.")
                }
                return ("square.grid.2x2", "No games yet",
                        "Tap Add to import a game, or put game files in Files \u{203A} On My \(UIDevice.current.userInterfaceIdiom == .pad ? "iPad" : "iPhone") \u{203A} EspressU \u{203A} Roms.")
            }()
            VStack(spacing: 14) {
                Image(systemName: symbol)
                    .font(.system(size: 54, weight: .regular))
                    .foregroundColor(EspressUTheme.pixelBlue.opacity(0.7))
                    .accessibilityHidden(true)
                Text(heading)
                    .font(.system(size: 22, weight: .bold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .accessibilityAddTraits(.isHeader)
                Text(line)
                    .font(.system(size: 15, weight: .medium, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                if searchText.isEmpty && !showingFavorites {
                    Button { showingAdd = true } label: {
                        Label("Add games", systemImage: "plus")
                            .font(.system(size: 16, weight: .heavy, design: .rounded))
                            .foregroundColor(EspressUTheme.onCremaTop)
                            .padding(.horizontal, 24)
                            .frame(height: 46)
                            .background(EspressUTheme.cremaTopGradient, in: Capsule())
                    }
                    .buttonStyle(.plain)
                    .padding(.top, 6)
                }
            }
            .frame(maxWidth: 420)
        }
    }

    /// Phones search by pulling the list down, so the bar has no Search button there.
    private var barItems: [EspressUMenuBar.Item] {
        isPhone ? allBarItems.filter { $0.title != "Search" } : allBarItems
    }

    private var allBarItems: [EspressUMenuBar.Item] {
        let jitOn = JITStatus.isActive && vpn.isConnected
        return [
            .init(title: "Add", systemImage: "plus", tint: EspressUTheme.accentText, isOn: showingAdd) {
                showingAdd.toggle()
            },
            .init(title: "Favourites", systemImage: showingFavorites ? "heart.fill" : "heart",
                  tint: showingFavorites ? EspressUTheme.alertText : EspressUTheme.brownDark, isOn: showingFavorites) {
                showingAdd = false
                showingFavorites.toggle()
            },
            .init(title: "Search", systemImage: "magnifyingglass",
                  tint: showingSearch ? EspressUTheme.accentText : EspressUTheme.brownDark, isOn: showingSearch) {
                showingAdd = false
                showingSearch.toggle()
                if !showingSearch { searchText = "" }
            },
            .init(title: !jitPanel.enabled ? "No JIT" : (jitOn ? "JIT on" : "JIT"), systemImage: "bolt.fill",
                  tint: jitOn && jitPanel.enabled ? Color(hex: "#2E8B57") : EspressUTheme.brownMid, isOn: showingJIT) {
                showingAdd = false
                openJITPanel()
            },
            .init(title: "Settings", systemImage: "gearshape.fill", tint: EspressUTheme.brownDark, isOn: false) {
                showingAdd = false
                actions.openSettings()
            }
        ]
    }

    private func handlePad(_ command: ControllerMenuCommand) {
        cemu_bridge_log_checkpoint("Pad: \(command) add=\(showingAdd) bar=\(barFocus.map(String.init) ?? "-") selected=\(selectedId ?? "-") games=\(games.count) presented=\(ControllerMenuInput.somethingPresented)")
        if ControllerMenuInput.somethingPresented {
            ControllerScreenCursor.shared.handle(command)
            return
        }
        ControllerScreenCursor.shared.reset()
        if showingJIT {
            jitPanel.handle(command)
        } else if optionsGameId != nil {
            handleOptions(command)
        } else if showingAdd {
            handleAddPanel(command)
        } else if let index = barFocus {
            handleBar(command, index: index)
        } else {
            handleGames(command)
        }
    }

    private func handleGames(_ command: ControllerMenuCommand) {
        guard !games.isEmpty else {
            if command != .back { enterBar() }
            return
        }
        let center = centerIndex
        let centerGame = selectedGame ?? (games.indices.contains(center) ? games[center] : nil)
        // Phones show a two-column grid instead of the carousel, so up and down move a whole row.
        if isPhone, [.left, .right, .up, .down].contains(command) {
            guard selectedId != nil else { bringToCenter(center); return }
            let step = (command == .left || command == .right) ? 1 : 2
            let target = (command == .left || command == .up) ? center - step : center + step
            if games.indices.contains(target) {
                bringToCenter(target)
            } else if command == .down {
                if center / 2 < (games.count - 1) / 2 { bringToCenter(games.count - 1) } else { enterBar() }
            }
            return
        }
        switch command {
        case .left, .right, .previousPage, .nextPage:
            guard selectedId != nil else { bringToCenter(center); return }
            let target = (command == .left || command == .previousPage) ? center - 1 : center + 1
            if games.indices.contains(target) { bringToCenter(target) }
        case .up:
            if selectedId == nil { bringToCenter(center) }
        case .down:
            enterBar()
        case .confirm:
            if let game = selectedGame { actions.play(game) } else { bringToCenter(center) }
        case .alternate, .select:
            if let centerGame { actions.toggleFavorite(centerGame) }
        case .back:
            if showingSearch {
                showingSearch = false
                searchText = ""
            } else {
                selectedId = nil
            }
        case .menu:
            if let centerGame { openOptions(centerGame) }
        }
    }

    private func enterBar() {
        let keep = selectedId ?? (games.indices.contains(centerIndex) ? games[centerIndex].id : nil)
        gameBeforeBar = keep
        centerId = keep
        selectedId = nil
        barFocus = 0
    }

    private func handleBar(_ command: ControllerMenuCommand, index: Int) {
        let items = barItems
        switch command {
        case .left: barFocus = max(0, index - 1)
        case .right: barFocus = min(items.count - 1, index + 1)
        case .up, .back:
            barFocus = nil
            let back = gameBeforeBar.flatMap { id in games.first { $0.id == id } } ?? games.first
            gameBeforeBar = nil
            if let back {
                centerId = back.id
                selectedId = back.id
            }
        case .confirm:
            items[min(index, items.count - 1)].action()
        case .menu:
            if games.indices.contains(centerIndex) { openOptions(games[centerIndex]) }
        case .previousPage, .nextPage:
            barFocus = nil
            let back = gameBeforeBar.flatMap { id in games.first { $0.id == id } }
                ?? (games.indices.contains(centerIndex) ? games[centerIndex] : games.first)
            gameBeforeBar = nil
            if let back {
                centerId = back.id
                selectedId = back.id
            }
        default:
            break
        }
    }

    private func openJITPanel() {
        jitPanel.close = { showingJIT = false }
        jitPanel.openSettings = actions.openSettings
        jitPanel.open()
        showingJIT = true
    }

    private func handleAddPanel(_ command: ControllerMenuCommand) {
        let columns = 1
        var rows: [[String]] = []
        for group in addGroups {
            let ids = group.1.map(\.id)
            stride(from: 0, to: ids.count, by: columns).forEach { rows.append(Array(ids[$0..<min($0 + columns, ids.count)])) }
        }
        switch command {
        case .up, .down, .left, .right:
            addFocus = ControllerFocusGrid(rows: rows).move(addFocus ?? rows.first?.first, command)
        case .confirm:
            guard let id = addFocus, let option = addGroups.flatMap({ $0.1 }).first(where: { $0.id == id }) else { return }
            runAddOption(option)
        case .back:
            showingAdd = false
        default:
            break
        }
    }

    private func runAddOption(_ option: AddOption) {
        showingAdd = false
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { option.action() }
    }

    private struct OptionRow: Identifiable {
        let id: String
        let title: String
        let systemImage: String
        var destructive = false
        var confirm = false
        var opensScreen = false
        let action: () -> Void
    }

    private var optionsGame: GameMetadata? {
        optionsGameId.flatMap { id in games.first { $0.id == id } }
    }

    private func optionRows(_ game: GameMetadata) -> [OptionRow] {
        [
            OptionRow(id: "play", title: "Play", systemImage: "play.fill") { actions.play(game) },
            OptionRow(id: "favourite", title: game.isFavorite ? "Remove from favourites" : "Add to favourites",
                      systemImage: game.isFavorite ? "heart.slash" : "heart") { actions.toggleFavorite(game) },
            OptionRow(id: "page", title: "Game page", systemImage: "info.circle", opensScreen: true) { actions.openGamePage(game) },
            OptionRow(id: "rename", title: "Rename", systemImage: "pencil", opensScreen: true) { actions.renameGame(game) },
            OptionRow(id: "cover", title: "Change cover", systemImage: "photo", opensScreen: true) { actions.changeCover(game) },
            OptionRow(id: "remove", title: game.removeActionTitle, systemImage: "trash", destructive: true, confirm: true) {
                actions.removeGame(game)
            }
        ]
    }

    private func openOptions(_ game: GameMetadata) {
        optionsFocus = 0
        optionsArmed = nil
        optionsGameId = game.id
    }

    private func closeOptions() {
        optionsGameId = nil
        optionsArmed = nil
    }

    private func runOption(_ row: OptionRow) {
        if row.confirm && optionsArmed != row.id {
            optionsArmed = row.id
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                if optionsArmed == row.id { optionsArmed = nil }
            }
            return
        }
        if row.id == "favourite" {
            row.action()
            return
        }
        closeOptions()
        if row.opensScreen {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { row.action() }
        } else {
            row.action()
        }
    }

    private func optionsPanel(_ game: GameMetadata) -> some View {
        let rows = optionRows(game)
        return VStack(spacing: 14) {
            HStack(spacing: 14) {
                EspressUCover(game: game, radius: 16)
                    .frame(width: 64, height: 64)
                VStack(alignment: .leading, spacing: 3) {
                    Text(game.cardName.name)
                        .font(.system(size: 19, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                        .lineLimit(2)
                    if let install = game.installLabel {
                        Text(install)
                            .font(.system(size: 13, weight: .medium, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                            .lineLimit(1)
                    }
                }
                Spacer(minLength: 0)
                Button { closeOptions() } label: {
                    HStack(spacing: 6) {
                        if pad.showsFocus {
                            Image(systemName: pad.backSymbol)
                                .font(.system(size: 15, weight: .semibold))
                        }
                        Image(systemName: "xmark")
                            .font(.system(size: 15, weight: .bold))
                    }
                    .foregroundColor(EspressUTheme.brownMid)
                    .padding(.horizontal, pad.showsFocus ? 12 : 0)
                    .frame(minWidth: 36, minHeight: 36)
                    .background(EspressUTheme.wrapper.opacity(0.6), in: Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Close")
            }
            VStack(spacing: 6) {
                ForEach(Array(rows.enumerated()), id: \.element.id) { index, row in
                    let armed = optionsArmed == row.id
                    let focused = pad.showsFocus && optionsFocus == index
                    Button {
                        optionsFocus = index
                        runOption(row)
                    } label: {
                        HStack(spacing: 14) {
                            Image(systemName: row.systemImage)
                                .font(.system(size: 17, weight: .semibold))
                                .foregroundColor(row.destructive ? EspressUTheme.alertText
                                                 : (index == 0 ? EspressUTheme.onCremaTop : EspressUTheme.accentText))
                                .frame(width: 38, height: 38)
                                .background(index == 0 ? AnyShapeStyle(EspressUTheme.cremaTopGradient)
                                                       : AnyShapeStyle(EspressUTheme.wrapper),
                                            in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                            Text(armed ? "Press again to remove" : row.title)
                                .font(.system(size: 17, weight: .bold, design: .rounded))
                                .foregroundColor(row.destructive ? EspressUTheme.alertText : EspressUTheme.brownDarkest)
                            Spacer(minLength: 0)
                            if focused && index == 0 {
                                Image(systemName: pad.confirmSymbol)
                                    .font(.system(size: 17, weight: .semibold))
                                    .foregroundColor(EspressUTheme.brownMid)
                            }
                        }
                        .padding(.horizontal, 10)
                        .frame(minHeight: 54)
                        .background(
                            RoundedRectangle(cornerRadius: 16, style: .continuous)
                                .fill(armed ? EspressUTheme.alertText.opacity(0.12)
                                      : EspressUTheme.pixelBlue.opacity(focused ? 0.1 : 0)))
                        .overlay(
                            RoundedRectangle(cornerRadius: 16, style: .continuous)
                                .strokeBorder(EspressUTheme.pixelBlue, lineWidth: 3)
                                .opacity(focused ? 1 : 0))
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding(18)
        .frame(maxWidth: 440)
        .espressUSurface(RoundedRectangle(cornerRadius: 30, style: .continuous))
        .animation(.easeOut(duration: 0.15), value: optionsFocus)
    }

    private func handleOptions(_ command: ControllerMenuCommand) {
        guard let game = optionsGame else {
            closeOptions()
            return
        }
        let rows = optionRows(game)
        switch command {
        case .up:
            optionsFocus = max(0, optionsFocus - 1)
        case .down:
            optionsFocus = min(rows.count - 1, optionsFocus + 1)
        case .confirm:
            if rows.indices.contains(optionsFocus) { runOption(rows[optionsFocus]) }
        case .alternate, .select:
            actions.toggleFavorite(game)
        case .back, .menu:
            closeOptions()
        default:
            break
        }
    }

    private struct AddOption: Identifiable {
        let id: String
        let title: String
        let detail: String
        let systemImage: String
        let action: () -> Void
    }

    private var addGroups: [(String, [AddOption])] {
        [
            ("Add games", [
                AddOption(id: "file", title: "Game file", detail: "A .wua, .wux, .wud or .rpx",
                          systemImage: "doc.fill", action: actions.importFile),
                AddOption(id: "folder", title: "Game folder", detail: "A dumped game folder",
                          systemImage: "folder.fill", action: { importerPurpose = .gameFolder; importerMode = .gameFolder }),
                AddOption(id: "romsInFiles", title: "Roms folder in Files", detail: "Drag games in, then come back",
                          systemImage: "folder.badge.plus", action: { Self.openInFiles(Self.documentsFolder("Roms")) })
            ]),
            ("Keep games where they are", [
                AddOption(id: "linkFolder", title: "Link a folder", detail: "Drive, SD card or iCloud. Nothing is copied",
                          systemImage: "externaldrive.fill.badge.plus", action: actions.linkFolder),
                AddOption(id: "linkFile", title: "Link a game file", detail: "Play it right where it is",
                          systemImage: "link", action: actions.linkFile)
            ]),
            ("Updates, DLC and keys", [
                AddOption(id: "update", title: "Update", detail: "A folder or .wua for a game you have",
                          systemImage: "arrow.triangle.2.circlepath", action: { importerPurpose = .content(.update); importerMode = .content(.update) }),
                AddOption(id: "dlc", title: "DLC", detail: "A folder or .wua for a game you have",
                          systemImage: "shippingbox.fill", action: { importerPurpose = .content(.dlc); importerMode = .content(.dlc) }),
                AddOption(id: "keys", title: "Keys", detail: "keys.txt for encrypted games",
                          systemImage: "key.fill", action: actions.importKeys)
            ])
        ]
    }

    private static func documentsFolder(_ name: String) -> URL {
        let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent(name, isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private static func openInFiles(_ folder: URL) {
        let path = folder.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? folder.path
        guard let url = URL(string: "shareddocuments://" + path) else { return }
        cemu_bridge_log_checkpoint("Opening Files at \(folder.lastPathComponent)")
        UIApplication.shared.open(url)
    }

    private var addPanel: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .center) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Add to EspressU")
                        .font(.system(size: 22, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                    Text("Games, updates, DLC and keys")
                        .font(.system(size: 14, weight: .medium, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                }
                Spacer()
                Button { showingAdd = false } label: {
                    Image(systemName: "xmark")
                        .font(.system(size: 15, weight: .bold))
                        .foregroundColor(EspressUTheme.brownMid)
                        .frame(width: 38, height: 38)
                        .background(EspressUTheme.wrapper.opacity(0.6), in: Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Close")
            }
            ScrollView(showsIndicators: false) {
                VStack(alignment: .leading, spacing: 18) {
                    ForEach(addGroups, id: \.0) { group in
                        VStack(alignment: .leading, spacing: 6) {
                            Text(group.0.uppercased())
                                .font(.system(size: 12, weight: .heavy, design: .rounded))
                                .tracking(0.6)
                                .foregroundColor(EspressUTheme.brownMid)
                                .padding(.horizontal, 6)
                            VStack(spacing: 0) {
                                ForEach(Array(group.1.enumerated()), id: \.element.id) { index, option in
                                    if index > 0 {
                                        Rectangle()
                                            .fill(EspressUTheme.wrapper)
                                            .frame(height: 1)
                                            .padding(.leading, 62)
                                    }
                                    addRow(option)
                                }
                            }
                            .background(EspressUTheme.cream.opacity(0.85), in: RoundedRectangle(cornerRadius: 20, style: .continuous))
                            .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
                            if group.0 == "Updates, DLC and keys" {
                                Text("You can also drop updates and DLC into Files \u{203A} EspressU \u{203A} Updates & DLC.")
                                    .font(.system(size: 12, weight: .medium, design: .rounded))
                                    .foregroundColor(EspressUTheme.brownMid)
                                    .padding(.horizontal, 6)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                    }
                }
                .padding(.bottom, 4)
                .background(
                    GeometryReader { geo in
                        Color.clear
                            .onAppear { addContentHeight = geo.size.height }
                            .onChange(of: geo.size.height) { addContentHeight = $0 }
                    }
                )
            }
            .frame(height: min(max(addContentHeight, 1), screenSize.height * (isPhone ? 0.62 : 0.55)))
        }
        .padding(isPhone ? 16 : 20)
        .frame(maxWidth: isPhone ? .infinity : 560)
        .espressUSurface(RoundedRectangle(cornerRadius: 30, style: .continuous))
    }

    private func addRow(_ option: AddOption) -> some View {
        Button {
            runAddOption(option)
        } label: {
            HStack(spacing: 14) {
                Image(systemName: option.systemImage)
                    .font(.system(size: 17, weight: .semibold))
                    .foregroundColor(EspressUTheme.onCremaTop)
                    .frame(width: 38, height: 38)
                    .background(EspressUTheme.cremaTopGradient, in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(option.title)
                        .font(.system(size: 16, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                    Text(option.detail)
                        .font(.system(size: 13, weight: .medium, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .lineLimit(2)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 8)
                Image(systemName: "chevron.right")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(EspressUTheme.brownMid.opacity(0.7))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, minHeight: 58, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(EspressUPressStyle())
        .controllerFocusRing(pad.showsFocus && addFocus == option.id,
                             shape: RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

struct EspressUMenuBar: View {
    struct Item {
        let title: String
        let systemImage: String
        let tint: Color
        let isOn: Bool
        let action: () -> Void

        init(title: String, systemImage: String, tint: Color, isOn: Bool, action: @escaping () -> Void) {
            self.title = title
            self.systemImage = systemImage
            self.tint = tint
            self.isOn = isOn
            self.action = action
        }
    }

    let items: [Item]
    let compact: Bool
    var focusedIndex: Int? = nil

    @GestureState private var slideX: CGFloat?
    @State private var lastHovered: Int?
    private let haptics = UISelectionFeedbackGenerator()

    var body: some View {
        GeometryReader { geo in
            let width = geo.size.width / CGFloat(max(items.count, 1))
            let hovered = slideX.map { index(at: $0, width: width) }
            ZStack(alignment: .leading) {
                if let slideX {
                    Capsule()
                        .fill(EspressUTheme.pixelBlue.opacity(0.16))
                        .frame(width: width - 8, height: geo.size.height - 12)
                        .offset(x: min(max(slideX - width / 2, 4), geo.size.width - width + 4))
                        .transition(.opacity)
                } else if let focusedIndex {
                    Capsule()
                        .fill(EspressUTheme.pixelBlue.opacity(0.16))
                        .overlay(Capsule().stroke(EspressUTheme.pixelBlue, lineWidth: 2))
                        .frame(width: width - 8, height: geo.size.height - 12)
                        .offset(x: width * CGFloat(focusedIndex) + 4)
                        .transition(.opacity)
                }
                HStack(spacing: 0) {
                    ForEach(items.indices, id: \.self) { index in
                        Button(action: items[index].action) {
                            label(for: items[index], highlighted: hovered == index || (slideX == nil && focusedIndex == index))
                                .frame(width: width, height: geo.size.height)
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(EspressUHomeBarButtonStyle(sliding: slideX != nil, height: geo.size.height - 12))
                        .overlay(alignment: .leading) {
                            if index > 0 && slideX == nil {
                                Rectangle()
                                    .fill(EspressUTheme.wrapper)
                                    .frame(width: 1, height: geo.size.height * 0.45)
                                    .allowsHitTesting(false)
                            }
                        }
                        .accessibilityLabel(items[index].title)
                        .accessibilityAddTraits(items[index].isOn ? .isSelected : [])
                    }
                }
            }
            .simultaneousGesture(
                DragGesture(minimumDistance: 10)
                    .updating($slideX) { value, state, _ in
                        state = value.location.x
                    }
                    .onEnded { value in
                        let start = index(at: value.startLocation.x, width: width)
                        let end = index(at: value.location.x, width: width)
                        let insideVertically = value.location.y > -60 && value.location.y < geo.size.height + 60
                        if insideVertically && start != end { items[end].action() }
                    }
            )
            .onChange(of: hovered) { newValue in
                if newValue != nil && lastHovered != nil { haptics.selectionChanged() }
                lastHovered = newValue
                haptics.prepare()
            }
        }
        .padding(.horizontal, 6)
        .frame(height: compact ? 60 : 72)
        .frame(maxWidth: compact ? .infinity : 720)
        .espressUSurface(Capsule())
        .animation(.interactiveSpring(response: 0.3, dampingFraction: 0.86), value: slideX)
        .animation(.spring(response: 0.25, dampingFraction: 0.85), value: focusedIndex)
    }

    private func index(at x: CGFloat, width: CGFloat) -> Int {
        min(max(Int(x / width), 0), items.count - 1)
    }

    private func label(for item: Item, highlighted: Bool) -> some View {
        VStack(spacing: 4) {
            Image(systemName: item.systemImage)
                .font(.system(size: compact ? 19 : 21, weight: .semibold))
                .foregroundColor(item.tint)
            if !compact {
                Text(item.title)
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .foregroundColor(highlighted || item.isOn ? item.tint : EspressUTheme.brownMid)
            }
        }
        .scaleEffect(highlighted ? 1.06 : 1)
    }
}

private struct EspressUHomeBarButtonStyle: ButtonStyle {
    let sliding: Bool
    let height: CGFloat

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                Capsule()
                    .fill(EspressUTheme.pixelBlue.opacity(configuration.isPressed && !sliding ? 0.16 : 0))
                    .frame(height: height)
                    .padding(.horizontal, 4)
            )
            .animation(.easeOut(duration: 0.15), value: configuration.isPressed)
    }
}

private struct EspressUPressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(.spring(response: 0.2, dampingFraction: 0.7), value: configuration.isPressed)
    }
}

struct EspressUCover: View {
    let game: GameMetadata
    let radius: CGFloat

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: radius, style: .continuous)
                .fill(EspressUTheme.cremaTopGradient)
            if let path = game.coverPath {
                CoverImage(path: path, fill: true)
            } else {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 28))
                    .foregroundColor(EspressUTheme.onCremaTop)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
    }
}

struct EspressUBackground: View {
    var body: some View {
        ZStack {
            EspressUTheme.backgroundGradient
            Canvas { context, size in
                var y: CGFloat = 38
                while y < size.height {
                    context.fill(Path(CGRect(x: 0, y: y, width: size.width, height: 1)),
                                 with: .color(EspressUTheme.brownDarkest.opacity(0.018)))
                    y += 40
                }
            }
        }
        .ignoresSafeArea()
    }
}

extension View {
    @ViewBuilder func espressUSurface<S: Shape>(_ shape: S) -> some View {
        if #available(iOS 26.0, *), UIStyle.allowsLiquidGlass {
            self.glassEffect(.regular, in: shape)
        } else {
            self
                .background(EspressUTheme.cream, in: shape)
                .overlay(shape.stroke(EspressUTheme.wrapper, lineWidth: 1))
                .shadow(color: EspressUTheme.shadow.opacity(0.2), radius: 12, y: 4)
        }
    }
}
