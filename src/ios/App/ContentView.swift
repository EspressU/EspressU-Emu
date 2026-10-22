// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import MetalKit
import UniformTypeIdentifiers
import Foundation

struct ContentView: View {
    @StateObject var gameManager = GameManager()
    @Environment(\.scenePhase) private var appScenePhase
    @AppStorage(OnboardingState.completedKey) private var onboardingCompleted = false
    @State private var showOnboarding = OnboardingState.shouldPresentOnFirstLaunch
    @State private var selectedGame: GameMetadata?
    @State private var showingGameBrowser = true
    @State private var showingFavorites = false
    @State private var sharedImportError: String?
    static let sharedGameExtensions: Set<String> = ["wua", "wud", "wux", "rpx"]

    private func openSharedFile(_ url: URL) {
        guard url.isFileURL, Self.sharedGameExtensions.contains(url.pathExtension.lowercased()) else { return }
        if gameManager.emulationState == .idle { showingGameBrowser = true }
        Task {
            do {
                try await gameManager.importROM(from: url)
            } catch {
                sharedImportError = error.localizedDescription
            }
            if url.path.contains("/Documents/Inbox/") { try? FileManager.default.removeItem(at: url) }
        }
    }

    var body: some View {
        ZStack {
            if showingGameBrowser {
                GameBrowserView(
                    gameManager: gameManager,
                    selectedGame: $selectedGame,
                    showingGameBrowser: $showingGameBrowser,
                    showingFavorites: $showingFavorites
                )
            } else if let game = selectedGame {
                switch gameManager.emulationState {
                case .loading, .running, .paused:
                    EmulatorViewOptimized(
                        game: game,
                        gameManager: gameManager,
                        isRunning: $showingGameBrowser
                    )
                    .overlay(alignment: .top) {
                        if let notice = gameManager.launchNotice {
                            LaunchNoticeBanner(text: notice)
                                .padding(.top, 24)
                                .padding(.horizontal, 16)
                                .transition(.move(edge: .top).combined(with: .opacity))
                                .allowsHitTesting(false)
                        }
                    }
                case .error:
                    BootFailureView(
                        game: game,
                        message: gameManager.lastStatusMessage,
                        needsCleanRestart: gameManager.needsCleanRestart,
                        endedWhileRunning: gameManager.titleEndedByEngine,
                        driveWasLost: gameManager.driveWasLost,
                        onDismiss: {
                            gameManager.stopEmulation()
                            showingGameBrowser = true
                        }
                    )
                case .idle:
                    Color.clear.onAppear { showingGameBrowser = true }
                }
            }
        }
        .ignoresSafeArea()
        .onOpenURL { url in openSharedFile(url) }
        .alert("Couldn't add that game", isPresented: Binding(
            get: { sharedImportError != nil }, set: { if !$0 { sharedImportError = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(sharedImportError ?? "")
        }
        .stikJITOverlay()
        .onReceive(NotificationCenter.default.publisher(for: .espressuReopenOnboarding)) { _ in
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) { showOnboarding = true }
        }
        .fullScreenCover(isPresented: $showOnboarding, onDismiss: { onboardingCompleted = true }) {
            EspressUOnboardingView(gameManager: gameManager) {
                onboardingCompleted = true
                showOnboarding = false
            }
        }
    }
}

struct BootFailureView: View {
    let game: GameMetadata
    let message: String
    var needsCleanRestart: Bool = false
    var endedWhileRunning: Bool = false
    var driveWasLost: Bool = false
    let onDismiss: () -> Void

    private var name: String { game.displayTitle ?? game.title }

    private static var crashLogHint: String {
        let path = String(cString: cemu_bridge_crash_log_path())
        guard !path.isEmpty else {
            return "If you report this, include log.txt. No crash log could be opened this run, so there is no CemuCrashLog.txt to send."
        }
        return "If you report this, include log.txt and CemuCrashLog.txt. They are in:\n\(path)"
    }

    private var title: String {
        if needsCleanRestart { return "Restart needed before \(name)" }
        if driveWasLost { return "\(name) stopped: drive disconnected" }
        return endedWhileRunning ? "\(name) stopped" : "Couldn't start \(name)"
    }

    @State private var keysAdded: Int?
    @State private var keysError: String?

    private var needsKeys: Bool {
        guard !needsCleanRestart, !endedWhileRunning, keysAdded == nil, !WiiUKeys.keysFileExists() else { return false }
        return ["wux", "wud", "iso", "tmd"].contains(URL(fileURLWithPath: game.romPath).pathExtension.lowercased())
    }

    private var plainLine: String {
        if needsCleanRestart { return "Close EspressU and open it again from your Home Screen, then start a game." }
        if driveWasLost { return "The drive or folder this game is stored on was disconnected. Connect it again, go back and start the game again." }
        if endedWhileRunning { return "The game stopped. Go back and try it again. If it keeps happening, close and reopen EspressU." }
        if needsKeys { return "This game is encrypted, so it needs a keys.txt file to start." }
        if let keysAdded { return "Added \(keysAdded) key\(keysAdded == 1 ? "" : "s"). Go back and start the game again. If it still won't start, close and reopen EspressU." }
        return "This game didn't start. Try again. If it keeps happening, close and reopen EspressU."
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            GeometryReader { proxy in
                ScrollView {
                    content
                        .frame(maxWidth: .infinity, minHeight: proxy.size.height)
                }
            }
        }
    }

    private func importKeys() {
        DocumentImport.present(contentTypes: [.item]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    keysAdded = try WiiUKeys.importKeys(from: url)
                    keysError = nil
                } catch {
                    keysError = error.localizedDescription
                }
            case .failure(let error):
                keysError = error.localizedDescription
            }
        }
    }

    private var content: some View {
        VStack(spacing: 16) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 34, weight: .semibold))
                .foregroundColor(EspressUTheme.alertOnDark)
                .accessibilityHidden(true)

            Text(title)
                .font(.system(.headline, design: .rounded))
                .foregroundColor(.white)
                .multilineTextAlignment(.center)
                .accessibilityAddTraits(.isHeader)

            Text(plainLine)
                .font(.system(.footnote, design: .rounded))
                .foregroundColor(.white.opacity(0.85))
                .multilineTextAlignment(.center)
                .frame(maxWidth: 480)

            if let keysError {
                Text(keysError)
                    .font(.system(.footnote, design: .rounded))
                    .foregroundColor(EspressUTheme.alertOnDark)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 480)
            }

            if needsKeys {
                Button(action: importKeys) {
                    HStack(spacing: 6) {
                        Image(systemName: "key.fill")
                            .font(.system(size: 14, weight: .semibold))
                        Text("Import keys.txt")
                            .font(.system(size: 14, weight: .semibold, design: .rounded))
                    }
                }
                .buttonStyle(EspressUPrimaryButtonStyle())
            }

            DisclosureGroup("Details") {
                VStack(spacing: 10) {
                    Text(message.isEmpty ? "The engine didn't report a reason." : message)
                        .font(.system(.footnote, design: .rounded))
                        .foregroundColor(.white.opacity(0.75))
                        .multilineTextAlignment(.center)
                        .textSelection(.enabled)
                    Text(Self.crashLogHint)
                        .font(.system(.caption2, design: .rounded))
                        .foregroundColor(.white.opacity(0.6))
                        .multilineTextAlignment(.center)
                        .textSelection(.enabled)
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 8)
            }
            .font(.system(.footnote, design: .rounded))
            .foregroundColor(.white.opacity(0.75))
            .accentColor(.white.opacity(0.75))
            .frame(maxWidth: 480)

            if needsCleanRestart {
                Button(action: { fflush(nil); _exit(0) }) {
                    Text("Close EspressU")
                        .font(.system(size: 14, weight: .semibold, design: .rounded))
                }
                .buttonStyle(EspressUPrimaryButtonStyle())
                .padding(.top, 4)
            }

            Button(action: onDismiss) {
                HStack(spacing: 6) {
                    Image(systemName: "chevron.left")
                        .font(.system(size: 14, weight: .semibold))
                    Text("Back to games")
                        .font(.system(size: 14, weight: .semibold, design: .rounded))
                }
                .foregroundColor(.white)
                .padding(.horizontal, 14)
                .frame(minHeight: 44)
                .espressUSurface(Capsule())
                .contentShape(Capsule())
            }
            .buttonStyle(.plain)
            .environment(\.colorScheme, .dark)
            .padding(.top, 4)
        }
        .padding(32)
    }
}

enum LibrarySortOrder: String, CaseIterable, Hashable {
    case title
    case recentlyAdded
    case favoritesFirst
    case lastPlayed
    case mostPlayed

    var title: String {
        switch self {
        case .title: return "Title"
        case .recentlyAdded: return "Recently added"
        case .favoritesFirst: return "Favourites first"
        case .lastPlayed: return "Last played"
        case .mostPlayed: return "Most played"
        }
    }

    var systemImage: String {
        switch self {
        case .title: return "textformat"
        case .recentlyAdded: return "clock"
        case .favoritesFirst: return "heart"
        case .lastPlayed: return "clock.arrow.circlepath"
        case .mostPlayed: return "flame"
        }
    }

    func sorted(_ games: [GameMetadata], stats: LibraryPlayStats = .shared) -> [GameMetadata] {
        let byTitle: (GameMetadata, GameMetadata) -> Bool = {
            $0.sortTitle.localizedCaseInsensitiveCompare($1.sortTitle) == .orderedAscending
        }
        switch self {
        case .lastPlayed:
            return games.sorted { lhs, rhs in
                switch (stats.entry(for: lhs.id)?.last, stats.entry(for: rhs.id)?.last) {
                case let (l?, r?): return l > r
                case (nil, nil): return byTitle(lhs, rhs)
                case (nil, _): return false
                case (_, nil): return true
                }
            }
        case .mostPlayed:
            return games.sorted { lhs, rhs in
                let l = stats.entry(for: lhs.id)?.count ?? 0
                let r = stats.entry(for: rhs.id)?.count ?? 0
                return l != r ? l > r : byTitle(lhs, rhs)
            }
        case .title:
            return games.sorted { $0.sortTitle.localizedCaseInsensitiveCompare($1.sortTitle) == .orderedAscending }
        case .recentlyAdded:
            return games.sorted { lhs, rhs in
                switch (lhs.addedDate, rhs.addedDate) {
                case let (l?, r?): return l > r
                case (nil, nil): return lhs.sortTitle.localizedCaseInsensitiveCompare(rhs.sortTitle) == .orderedAscending
                case (nil, _): return false
                case (_, nil): return true
                }
            }
        case .favoritesFirst:
            return games.sorted { lhs, rhs in
                if lhs.isFavorite != rhs.isFavorite { return lhs.isFavorite && !rhs.isFavorite }
                return lhs.sortTitle.localizedCaseInsensitiveCompare(rhs.sortTitle) == .orderedAscending
            }
        }
    }
}

struct LibraryActivityBanner: View {
    let text: String

    var body: some View {
        HStack(spacing: 10) {
            ProgressView()
                .tint(EspressUTheme.brownDarkest)
            Text(text)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
                .lineLimit(1)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(EspressUTheme.cream)
        .cornerRadius(14)
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(EspressUTheme.wrapper, lineWidth: 1)
        )
        .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 8, x: 0, y: 4)
    }
}

struct LaunchNoticeBanner: View {
    let text: String

    var body: some View {
        Text(text)
            .font(.system(size: 13, weight: .semibold, design: .rounded))
            .foregroundColor(EspressUTheme.brownDarkest)
            .multilineTextAlignment(.center)
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(EspressUTheme.cream)
            .cornerRadius(14)
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(EspressUTheme.wrapper, lineWidth: 1)
            )
            .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 8, x: 0, y: 4)
            .frame(maxWidth: 520)
    }
}

struct GameBrowserView: View {
    @ObservedObject var gameManager: GameManager
    @Binding var selectedGame: GameMetadata?
    @Binding var showingGameBrowser: Bool
    @Binding var showingFavorites: Bool
    @State private var searchText = ""
    @State private var showingSettings = false
    @State private var gamePageTarget: GameMetadata?
    @AppStorage(LibraryTapAction.storageKey) private var tapActionRaw = LibraryTapAction.defaultValue.rawValue
    @State private var decryptTarget: GameMetadata?
    @State private var coverArtTarget: GameMetadata?
    @ObservedObject private var perGameSettings = PerGameSettingsStore.shared
    @ObservedObject private var playStats = LibraryPlayStats.shared
    @ObservedObject private var customNames = LibraryCustomNames.shared
    @State private var renameTarget: GameMetadata?
    @AppStorage(LibraryCardStyle.storageKey) private var cardStyleRaw = LibraryCardStyle.defaultValue.rawValue
    @AppStorage(LibraryGrouping.storageKey) private var groupingRaw = LibraryGrouping.defaultValue.rawValue
    @AppStorage(LibraryFilter.storageKey) private var filterRaw = LibraryFilter.defaultValue.rawValue
    @Environment(\.scenePhase) private var scenePhase
    @State private var pendingGameRemoval: GameMetadata?
    private static let fileImportTypes: [UTType] = [.item]
    private static let folderImportTypes: [UTType] = [.folder, .directory]
    private static let dlcUpdateImportTypes: [UTType] = [.folder, .directory, .item]

    @AppStorage("espressu.library.sortOrder") private var sortOrderRaw = LibrarySortOrder.title.rawValue
    private var sortOrder: LibrarySortOrder {
        get { LibrarySortOrder(rawValue: sortOrderRaw) ?? .title }
        nonmutating set { sortOrderRaw = newValue.rawValue }
    }

    @State private var romImportErrorMessage: String?
    @State private var pendingOverwriteConfirmation: (name: String, resume: (Bool) -> Void)?
    @State private var removingContentMessage: String?

    @State private var dlcImportErrorMessage: String?
    @State private var pendingManualMatchConfirmation: (source: URL, kind: DlcUpdateImport.ContentKind, game: GameMetadata)?
    @State private var pendingRemoval: (game: GameMetadata, kind: DlcUpdateImport.ContentKind)?
    @State private var gamePickerContext: (source: URL, kind: DlcUpdateImport.ContentKind)?
    @State private var dlcUpdateSuccessMessage: String?

    var filteredGames: [GameMetadata] {
        let gamesToShow = showingFavorites ? gameManager.favorites : gameManager.games
        let searched = searchText.isEmpty
            ? gamesToShow
            : gamesToShow.filter {
                $0.title.localizedCaseInsensitiveContains(searchText)
                    || ($0.displayTitle?.localizedCaseInsensitiveContains(searchText) ?? false)
                    || (customNames.name(for: $0.installKey)?.localizedCaseInsensitiveContains(searchText) ?? false)
                    || ($0.installLabel?.localizedCaseInsensitiveContains(searchText) ?? false)
            }
        let filtered = (LibraryFilter(rawValue: filterRaw) ?? .all).apply(searched, stats: playStats)
        return sortOrder.sorted(filtered, stats: playStats)
    }

    private var libraryCardStyle: LibraryCardStyle {
        LibraryCardStyle(rawValue: cardStyleRaw) ?? .standard
    }

    private var librarySections: [LibrarySection] {
        (LibraryGrouping(rawValue: groupingRaw) ?? .none).sections(for: filteredGames, stats: playStats)
    }

    private func installFromInbox() async {
        for _ in 0..<40 where gameManager.isLoading { try? await Task.sleep(nanoseconds: 250_000_000) }
        guard gameManager.emulationState == .idle else { return }
        let report = await EspressUInbox.process(library: gameManager.games)
        guard !report.isEmpty else { return }
        if report.failed.isEmpty {
            dlcUpdateSuccessMessage = EspressUInbox.summary(report)
        } else {
            dlcImportErrorMessage = EspressUInbox.summary(report)
        }
    }

    private func withJIT(_ start: @escaping () -> Void) {
        guard StikJITCoordinator.shared.launchNeedsJIT else { start(); return }
        StikJITCoordinator.shared.acquireForLaunch { outcome in
            guard outcome != .cancelled, gameManager.emulationState == .idle else { return }
            start()
        }
    }

    var body: some View {
        withAlerts(withSheets(
            libraryScreen
            .onChange(of: scenePhase) { phase in
                if phase == .active {
                    if gameManager.emulationState == .idle { StikJITCoordinator.shared.autoEnableIfPossible() }
                    Task {
                        await gameManager.rescanIfIdle()
                        await installFromInbox()
                    }
                }
            }
            .task { await installFromInbox() }
            .onReceive(Timer.publish(every: 10, on: .main, in: .common).autoconnect()) { _ in
                guard scenePhase == .active, gameManager.emulationState == .idle, !JITStatus.isUsable else { return }
                StikJITCoordinator.shared.autoEnableIfPossible()
            }
            .onReceive(LocalDevVPNMonitor.shared.$isConnected.removeDuplicates().dropFirst()) { connected in
                guard connected, scenePhase == .active, gameManager.emulationState == .idle, !JITStatus.isUsable else { return }
                StikJITCoordinator.shared.autoEnableIfPossible()
            }
            .task {
                try? await Task.sleep(nanoseconds: 1_500_000_000)
                if gameManager.emulationState == .idle { StikJITCoordinator.shared.autoEnableIfPossible() }
            }
            .onReceive(NotificationCenter.default.publisher(for: .espressuReopenOnboarding)) { _ in
                showingSettings = false
            }
            .onAppear {
                LocalDevVPNMonitor.shared.start()
                #if os(iOS)
                if gameManager.emulationState == .idle { GameControlHints.restoreGlobals() }
                #endif
                gameManager.confirmOverwrite = { name in
                    await withCheckedContinuation { (continuation: CheckedContinuation<Bool, Never>) in
                        pendingOverwriteConfirmation = (name: name, resume: { continuation.resume(returning: $0) })
                    }
                }
            }
        ))
    }

    @ViewBuilder private var libraryScreen: some View {
        if EspressUHome.isEnabled {
            espressUHomeScreen
        } else {
            ZStack {
                EspressUTheme.backgroundGradient
                    .ignoresSafeArea()

                VStack(spacing: 0) {
                    libraryHeader

                    libraryPanel
                }
            }
        }
    }

    private var espressUHomeScreen: some View {
        EspressUHomeScreen(
            gameManager: gameManager,
            games: filteredGames,
            showingFavorites: $showingFavorites,
            searchText: $searchText,
            activityText: {
                if case .copying(let name) = gameManager.importState { return "Importing \(name)\u{2026}" }
                return removingContentMessage
            }(),
            actions: EspressUHomeActions(
                play: { playGame($0) },
                toggleFavorite: { gameManager.toggleFavorite($0) },
                openGamePage: { gamePageTarget = $0 },
                renameGame: { renameTarget = $0 },
                changeCover: { coverArtTarget = $0 },
                removeGame: { removeGameNow($0) },
                openSettings: { showingSettings = true },
                importFile: { beginImport(contentTypes: Self.fileImportTypes) },
                importFolder: { beginImport(contentTypes: Self.folderImportTypes) },
                importPickedGame: { handleImport(.success([$0])) },
                importPickedContent: { runDlcUpdateImport(from: $0, kind: $1, longPressedGame: nil, manualMatch: nil) },
                importKeys: { beginKeysImport() },
                linkFolder: { beginLink(contentTypes: Self.folderImportTypes) },
                linkFile: { beginLink(contentTypes: Self.fileImportTypes) },
                importDLC: { beginGeneralDlcUpdateImport(kind: .dlc) },
                importUpdate: { beginGeneralDlcUpdateImport(kind: .update) }
            ),
            lead: { EmptyView() },
            gameMenu: { game in gameMenu(for: game) }
        )
        .onDrop(of: [UTType.item], isTargeted: nil, perform: handleDrop)
    }

    private var libraryHeader: some View {
        HStack(alignment: .center, spacing: 16) {
            Group {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Espress")
                        .font(.system(size: 28, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.onBackground)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)

                    Text("U")
                        .font(.system(size: 18, weight: .semibold, design: .rounded))
                        .foregroundColor(EspressUTheme.onBackgroundAccent)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                }
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("EspressU")

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                HStack(spacing: 8) {
                    Button(action: { showingSettings = true }) {
                        Image(systemName: "gearshape.fill")
                            .font(.system(size: 16, weight: .semibold))
                    }
                    .buttonStyle(EspressUSecondaryButtonStyle())
                    .frame(minWidth: 44, minHeight: 44)
                    .accessibilityLabel("Settings")

                    Button(action: { showingFavorites.toggle() }) {
                        Image(systemName: showingFavorites ? "heart.fill" : "heart")
                            .font(.system(size: 16, weight: .semibold))
                            .foregroundColor(showingFavorites ? EspressUTheme.alertText : EspressUTheme.brownDark)
                    }
                    .buttonStyle(EspressUSecondaryButtonStyle())
                    .frame(minWidth: 44, minHeight: 44)
                    .accessibilityLabel(showingFavorites ? "Show all games" : "Show favourites only")

                    Menu {
                        Button {
                            beginImport(contentTypes: Self.fileImportTypes)
                        } label: {
                            Label("Game file", systemImage: "doc")
                        }
                        Button {
                            beginImport(contentTypes: Self.folderImportTypes)
                        } label: {
                            Label("Game folder", systemImage: "folder")
                        }
                        Button {
                            beginKeysImport()
                        } label: {
                            Label("Keys (keys.txt)", systemImage: "key")
                        }
                        Divider()
                        Button {
                            beginLink(contentTypes: Self.folderImportTypes)
                        } label: {
                            Label("Link a folder\u{2026}", systemImage: "externaldrive.badge.plus")
                        }
                        Button {
                            beginLink(contentTypes: Self.fileImportTypes)
                        } label: {
                            Label("Link a game file\u{2026}", systemImage: "link")
                        }
                        Divider()
                        Button {
                            beginGeneralDlcUpdateImport(kind: .dlc)
                        } label: {
                            Label("Import DLC\u{2026}", systemImage: "shippingbox")
                        }
                        Button {
                            beginGeneralDlcUpdateImport(kind: .update)
                        } label: {
                            Label("Import Update\u{2026}", systemImage: "arrow.triangle.2.circlepath")
                        }
                    } label: {
                        Image(systemName: "doc.badge.plus")
                            .font(.system(size: 16, weight: .semibold))
                            .foregroundColor(EspressUTheme.brownDark)
                    }
                    .buttonStyle(EspressUSecondaryButtonStyle())
                    .frame(minWidth: 44, minHeight: 44)
                    .accessibilityLabel("Import")

                    VStack(alignment: .trailing, spacing: 2) {
                        Text("\(filteredGames.count)")
                            .font(.system(size: 14, weight: .bold, design: .rounded))
                            .foregroundColor(EspressUTheme.brownDarkest)
                        Text("games")
                            .font(.system(size: 10, weight: .semibold, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: EspressUTheme.Radius.chip, style: .continuous))
                    .accessibilityElement(children: .combine)
                }
            }
        }
        .padding(20)
    }

    private var libraryPanel: some View {
        VStack(spacing: 12) {
            HStack(spacing: 10) {
                SearchBarPolished(text: $searchText)

                Menu {
                    ForEach(LibrarySortOrder.allCases, id: \.self) { order in
                        Button {
                            sortOrder = order
                        } label: {
                            if sortOrder == order {
                                Label(order.title, systemImage: "checkmark")
                            } else {
                                Label(order.title, systemImage: order.systemImage)
                            }
                        }
                    }
                } label: {
                    Image(systemName: "arrow.up.arrow.down.circle")
                        .font(.system(size: 20, weight: .semibold))
                        .foregroundColor(EspressUTheme.brownMid)
                        .frame(width: 44, height: 44)
                }
                .accessibilityLabel("Sort games")

                LibraryViewMenu()
            }
            .padding(.horizontal, 16)
            .padding(.top, 16)

            if gameManager.isLoading {
                LoadingView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if filteredGames.isEmpty {
                EmptyGamesView(
                    kind: !searchText.isEmpty ? .noSearchMatch(searchText) : (showingFavorites ? .noFavourites : .emptyLibrary),
                    onImportTapped: { beginImport(contentTypes: Self.fileImportTypes) }
                )
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                LibraryGameCollection(
                    sections: librarySections,
                    style: libraryCardStyle,
                    lead: { EmptyView() },
                    card: { game in
                            LibraryCard(
                                game: game,
                                style: libraryCardStyle,
                                onTap: {
                                    if LibraryTapAction.current(raw: tapActionRaw) == .openPage {
                                        gamePageTarget = game
                                    } else {
                                        playGame(game)
                                    }
                                },
                                onFavoriteTap: {
                                    gameManager.toggleFavorite(game)
                                },
                                onPlay: { playGame(game) },
                                options: { gameMenu(for: game) }
                            )
                            .contextMenu { gameMenu(for: game) }
                    }
                )
            }
        }
        .frame(maxHeight: .infinity)
        .background(
            EspressUTheme.cream
                .clipShape(RoundedCorner(radius: 28, corners: [.topLeft, .topRight]))
                .ignoresSafeArea(edges: .bottom)
        )
        .onDrop(of: [UTType.item], isTargeted: nil, perform: handleDrop)
        .overlay(alignment: .top) {
            if case .copying(let name) = gameManager.importState {
                LibraryActivityBanner(text: "Importing \(name)…")
                    .padding(.top, 8)
            } else if let removingContentMessage {
                LibraryActivityBanner(text: removingContentMessage)
                    .padding(.top, 8)
            }
        }
    }

    @ViewBuilder private func gameMenu(for game: GameMetadata) -> some View {
        GameContextMenu(
            game: game,
            gameManager: gameManager,
            onPlay: { playGame(game) },
            onOpenPage: { gamePageTarget = game },
            onChangeCoverArt: { coverArtTarget = game },
            onRename: { renameTarget = game }
        )
        Divider()
        Button(role: .destructive) {
            pendingGameRemoval = game
        } label: {
            DestructiveSettingsLabel(title: game.removeActionTitle, systemImage: "trash")
        }
    }

    private func playGame(_ game: GameMetadata) {
        guard gameManager.emulationState == .idle else { return }
        if game.isExternal {
            Task {
                guard await gameManager.externalGameIsReady(game), gameManager.emulationState == .idle else { return }
                withJIT {
                    selectedGame = game
                    gameManager.launchGame(game)
                    showingGameBrowser = false
                }
            }
            return
        }
        withJIT {
            selectedGame = game
            gameManager.launchGame(game)
            showingGameBrowser = false
        }
    }

    private func removeGameNow(_ game: GameMetadata) {
        pendingGameRemoval = nil
        removingContentMessage = "Removing \"\(game.cardName.name)\"\u{2026}"
        Task {
            do {
                try await gameManager.removeGame(game)
            } catch {
                dlcImportErrorMessage = error.localizedDescription
            }
            removingContentMessage = nil
        }
    }

    private func withSheets<Content: View>(_ content: Content) -> some View {
        content
            .fullScreenCover(isPresented: $showingSettings) {
                EspressUSettingsView(gameManager: gameManager)
            }
            .sheet(item: $renameTarget) { game in
                EspressURenameView(game: game)
            }
            .fullScreenCover(item: $gamePageTarget) { game in
                EspressUGamePage(
                    game: game,
                    store: perGameSettings,
                    gameManager: gameManager,
                    onPlay: { playGame(game) },
                    onDecrypt: { decryptTarget = game },
                    onImportDLC: { beginDlcUpdateImport(for: game, kind: .dlc) },
                    onImportUpdate: { beginDlcUpdateImport(for: game, kind: .update) },
                    onRemoveDLC: { pendingRemoval = (game: game, kind: .dlc) },
                    onRemoveUpdate: { pendingRemoval = (game: game, kind: .update) },
                    onRemoveGame: { removeGameNow(game) }
                )
            }
            .sheet(item: $decryptTarget) { game in
                DecryptROMView(game: game)
            }
            .sheet(item: $coverArtTarget) { game in
                EspressUCoverPicker(game: game, gameManager: gameManager)
            }
            .sheet(isPresented: Binding(
                get: { gamePickerContext != nil },
                set: { if !$0 { gamePickerContext = nil } }
            )) {
                if let context = gamePickerContext {
                    DlcUpdateGamePickerSheet(games: gameManager.games, kind: context.kind) { game in
                        runDlcUpdateImport(from: context.source, kind: context.kind, longPressedGame: nil, manualMatch: game)
                    }
                }
            }
    }

    private func withAlerts<Content: View>(_ content: Content) -> some View {
        content
            .alert("Added", isPresented: .constant(dlcUpdateSuccessMessage != nil), presenting: dlcUpdateSuccessMessage) { _ in
                Button("OK") { dlcUpdateSuccessMessage = nil }
            } message: { message in
                Text(message)
            }
            .alert("Couldn't add that game", isPresented: .constant(romImportErrorMessage != nil), presenting: romImportErrorMessage) { _ in
                Button("OK") { romImportErrorMessage = nil }
            } message: { message in
                Text(message)
            }
            .alert("Couldn't import", isPresented: .constant(dlcImportErrorMessage != nil), presenting: dlcImportErrorMessage) { _ in
                Button("OK") { dlcImportErrorMessage = nil }
            } message: { message in
                Text(message)
            }
            .alert(
                "No automatic match",
                isPresented: .constant(pendingManualMatchConfirmation != nil),
                presenting: pendingManualMatchConfirmation
            ) { pending in
                Button("Add to \"\(pending.game.title)\"") {
                    pendingManualMatchConfirmation = nil
                    runDlcUpdateImport(from: pending.source, kind: pending.kind, longPressedGame: pending.game, manualMatch: pending.game)
                }
                Button("Cancel", role: .cancel) { pendingManualMatchConfirmation = nil }
            } message: { pending in
                Text("Couldn't automatically match this \(pending.kind.displayName) to a game already in your library. Add it to \"\(pending.game.title)\" - the game you long-pressed?")
            }
            .confirmationDialog(
                "Remove content?",
                isPresented: .constant(pendingRemoval != nil),
                titleVisibility: .visible,
                presenting: pendingRemoval
            ) { pending in
                Button("Remove", role: .destructive) {
                    pendingRemoval = nil
                    removingContentMessage = "Removing \(pending.kind.displayName) for \"\(pending.game.title)\"…"
                    Task {
                        do {
                            try await Task.detached {
                                try DlcUpdateImport.remove(kind: pending.kind, for: pending.game)
                            }.value
                        } catch {
                            dlcImportErrorMessage = error.localizedDescription
                        }
                        removingContentMessage = nil
                    }
                }
                Button("Cancel", role: .cancel) { pendingRemoval = nil }
            } message: { pending in
                Text("Remove the \(pending.kind.displayName) installed for \"\(pending.game.title)\"? This can't be undone - you'll need to import it again.")
            }
            .confirmationDialog(
                pendingGameRemoval?.removeConfirmTitle ?? "Remove this game?",
                isPresented: .constant(pendingGameRemoval != nil),
                titleVisibility: .visible,
                presenting: pendingGameRemoval
            ) { pending in
                Button(pending.removeConfirmButton, role: .destructive) {
                    removeGameNow(pending)
                }
                Button("Cancel", role: .cancel) { pendingGameRemoval = nil }
            } message: { pending in
                Text(pending.removeConfirmMessage)
            }
            .alert("Connect the drive to play", isPresented: .constant(gameManager.launchBlockedMessage != nil), presenting: gameManager.launchBlockedMessage) { _ in
                Button("Check again") {
                    gameManager.launchBlockedMessage = nil
                    Task { await gameManager.loadGames(showSpinner: false) }
                }
                Button("OK", role: .cancel) { gameManager.launchBlockedMessage = nil }
            } message: { message in
                Text(message)
            }
            .confirmationDialog(
                "Replace existing file?",
                isPresented: .constant(pendingOverwriteConfirmation != nil),
                titleVisibility: .visible,
                presenting: pendingOverwriteConfirmation
            ) { pending in
                Button("Replace", role: .destructive) {
                    let resume = pending.resume
                    pendingOverwriteConfirmation = nil
                    resume(true)
                }
                Button("Cancel", role: .cancel) {
                    let resume = pending.resume
                    pendingOverwriteConfirmation = nil
                    resume(false)
                }
            } message: { pending in
                Text("\"\(pending.name)\" already exists in your library. Replacing it can't be undone.")
            }
    }

    private func beginKeysImport() {
        DocumentImport.present(contentTypes: Self.fileImportTypes) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    let count = try WiiUKeys.importKeys(from: url)
                    dlcUpdateSuccessMessage = "Added \(count) key\(count == 1 ? "" : "s"). Games use them the next time you start one. If you've already started a game since opening EspressU, close and reopen EspressU first."
                } catch {
                    dlcImportErrorMessage = error.localizedDescription
                }
            case .failure(let error):
                dlcImportErrorMessage = error.localizedDescription
            }
        }
    }

    private func beginLink(contentTypes: [UTType]) {
        DocumentImport.present(contentTypes: contentTypes) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                Task {
                    do {
                        try await gameManager.linkLocation(url)
                    } catch {
                        romImportErrorMessage = error.localizedDescription
                    }
                }
            case .failure(let error):
                romImportErrorMessage = error.localizedDescription
            }
        }
    }

    private func beginImport(contentTypes: [UTType]) {
        DocumentImport.present(contentTypes: contentTypes, asCopy: true) { result in
            handleImport(result)
        }
    }

    private func handleImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            Task {
                defer { DocumentImport.discardPickerCopy(url) }
                cemu_bridge_log_checkpoint("Import: game from \(url.lastPathComponent)")
                do {
                    try await gameManager.importROM(from: url)
                    cemu_bridge_log_checkpoint("Import: game done")
                } catch {
                    cemu_bridge_log_checkpoint("Import: game failed: \(error.localizedDescription)")
                    romImportErrorMessage = error.localizedDescription
                }
            }
        case .failure(let error):
            romImportErrorMessage = error.localizedDescription
        }
    }

    private func handleDrop(providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: { $0.canLoadObject(ofClass: URL.self) }) else {
            return false
        }
        _ = provider.loadObject(ofClass: URL.self) { url, _ in
            guard let url else { return }
            DispatchQueue.main.async {
                handleImport(.success([url]))
            }
        }
        return true
    }

    private func beginDlcUpdateImport(for game: GameMetadata, kind: DlcUpdateImport.ContentKind) {
        DocumentImport.present(contentTypes: Self.dlcUpdateImportTypes, asCopy: true) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                runDlcUpdateImport(from: url, kind: kind, longPressedGame: game, manualMatch: game)
            case .failure(let error):
                dlcImportErrorMessage = error.localizedDescription
            }
        }
    }

    private func beginGeneralDlcUpdateImport(kind: DlcUpdateImport.ContentKind) {
        DocumentImport.present(contentTypes: Self.dlcUpdateImportTypes, asCopy: true) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                runDlcUpdateImport(from: url, kind: kind, longPressedGame: nil, manualMatch: nil)
            case .failure(let error):
                dlcImportErrorMessage = error.localizedDescription
            }
        }
    }

    private func runDlcUpdateImport(
        from url: URL,
        kind: DlcUpdateImport.ContentKind,
        longPressedGame: GameMetadata?,
        manualMatch: GameMetadata?
    ) {
        Task {
            do {
                let result = try await DlcUpdateImport.import(
                    from: url,
                    kind: kind,
                    library: gameManager.games,
                    manualMatch: manualMatch
                )
                if let matched = result.matchedGame {
                    dlcUpdateSuccessMessage = "Added \(kind.displayName) for \"\(matched.title)\"."
                }
                DocumentImport.discardPickerCopy(url)
            } catch DlcUpdateImport.ImportError.noBaseGameMatch {
                if let longPressedGame {
                    pendingManualMatchConfirmation = (source: url, kind: kind, game: longPressedGame)
                } else {
                    gamePickerContext = (source: url, kind: kind)
                }
            } catch {
                DocumentImport.discardPickerCopy(url)
                cemu_bridge_log_checkpoint("Import: \(kind.displayName) failed: \(error.localizedDescription)")
                dlcImportErrorMessage = error.localizedDescription
            }
        }
    }
}

struct DlcUpdateGamePickerSheet: View {
    let games: [GameMetadata]
    let kind: DlcUpdateImport.ContentKind
    let onPick: (GameMetadata) -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()
                List(games) { game in
                    Button {
                        onPick(game)
                        dismiss()
                    } label: {
                        Text(game.title)
                            .foregroundColor(EspressUTheme.brownDarkest)
                    }
                }
            }
            .navigationTitle("Add \(kind.displayName) to which game?")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
        .navigationViewStyle(.stack)
    }
}

struct RoundedCorner: Shape {
    var radius: CGFloat = 0
    var corners: UIRectCorner = .allCorners

    func path(in rect: CGRect) -> Path {
        Path(UIBezierPath(roundedRect: rect, byRoundingCorners: corners, cornerRadii: CGSize(width: radius, height: radius)).cgPath)
    }
}

struct GameCardOptimized<Options: View>: View {
    let game: GameMetadata
    let onTap: () -> Void
    let onFavoriteTap: () -> Void
    var onPlay: (() -> Void)?
    let options: Options

    init(game: GameMetadata, onTap: @escaping () -> Void, onFavoriteTap: @escaping () -> Void,
         onPlay: (() -> Void)? = nil, @ViewBuilder options: () -> Options) {
        self.game = game
        self.onTap = onTap
        self.onFavoriteTap = onFavoriteTap
        self.onPlay = onPlay
        self.options = options()
    }

    private var cardName: (name: String, titleIdText: String?) { game.cardName }

    var body: some View {
        VStack(spacing: 0) {
            ZStack {
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .fill(EspressUTheme.cremaTopGradient)

                if let coverPath = game.coverPath {
                    CoverImage(path: coverPath, padding: 10)
                        .cornerRadius(16)
                } else {
                    VStack {
                        Image(systemName: "gamecontroller.fill")
                            .font(.system(size: 28))
                            .foregroundColor(EspressUTheme.onCremaTop)
                    }
                }

                VStack {
                    HStack {
                        Menu {
                            options
                        } label: {
                            Image(systemName: "ellipsis")
                                .font(.system(size: 14, weight: .semibold))
                                .foregroundColor(EspressUTheme.sparkleCream)
                                .frame(width: 32, height: 32)
                                .background(Color.black.opacity(0.4))
                                .cornerRadius(10)
                                .frame(width: 44, height: 44)
                                .contentShape(Rectangle())
                        }
                        .accessibilityLabel("More options for \(cardName.name)")
                        .padding(8)
                        Spacer()
                        Button(action: onFavoriteTap) {
                            Image(systemName: game.isFavorite ? "heart.fill" : "heart")
                                .font(.system(size: 14, weight: .semibold))
                                .foregroundColor(game.isFavorite ? EspressUTheme.alertOnDark : EspressUTheme.sparkleCream)
                                .frame(width: 32, height: 32)
                                .background(Color.black.opacity(0.4))
                                .cornerRadius(10)
                                .frame(width: 44, height: 44)
                                .contentShape(Rectangle())
                        }
                        .accessibilityLabel(game.isFavorite ? "Remove from favourites" : "Add to favourites")
                        .padding(8)
                    }
                    Spacer()
                }
            }
            .aspectRatio(3 / 4, contentMode: .fit)

            VStack(alignment: .leading, spacing: 8) {
                Text(cardName.name)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .lineLimit(2)
                    .foregroundColor(EspressUTheme.brownDarkest)

                HStack(spacing: 8) {
                    if let region = game.region {
                        Label(region, systemImage: "globe")
                            .font(.system(size: 11, weight: .regular, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                    Spacer()
                }
                if let label = game.installLabel { LibraryInstallLine(text: label, size: 11) }
                LibraryExternalLine(game: game, size: 11)

                Button(action: onPlay ?? onTap) {
                    HStack(spacing: 6) {
                        Image(systemName: "play.fill")
                            .font(.system(size: 10, weight: .semibold))
                        Text("Play")
                            .font(.system(size: 12, weight: .semibold, design: .rounded))
                    }
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(EspressUPrimaryButtonStyle())

                if let titleIdText = cardName.titleIdText {
                    Text(titleIdText)
                        .font(.system(size: 10, weight: .regular, design: .monospaced))
                        .foregroundColor(EspressUTheme.brownMid)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                }
            }
            .padding(12)
            .background(EspressUTheme.cream)
        }
        .background(EspressUTheme.cream)
        .cornerRadius(16)
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .stroke(EspressUTheme.wrapper, lineWidth: 1)
        )
        .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 8, x: 0, y: 4)
        .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .onTapGesture(perform: onTap)
    }
}

struct SearchBarPolished: View {
    @Binding var text: String
    var autoFocus = false
    var onKeyboardDismiss: (() -> Void)?
    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(EspressUTheme.brownMid)

            TextField("Search games...", text: $text)
                .focused($focused)
                .submitLabel(.search)
                .font(.system(size: 15, weight: .regular, design: .rounded))
                .textFieldStyle(.plain)
                .foregroundColor(EspressUTheme.brownDarkest)

            if !text.isEmpty {
                Button(action: { text = "" }) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(EspressUTheme.brownMid)
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .accessibilityLabel("Clear search")
            }
        }
        .frame(height: 44)
        .frame(maxWidth: .infinity)
        .padding(.horizontal, 12)
        .background(EspressUTheme.wrapper.opacity(0.5))
        .cornerRadius(14)
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(EspressUTheme.wrapper, lineWidth: 1)
        )
        .onAppear {
            if autoFocus {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { focused = true }
            }
        }
        .onChange(of: focused) { isFocused in
            if !isFocused && text.isEmpty { onKeyboardDismiss?() }
        }
    }
}

struct LoadingView: View {
    @State private var rotation: Double = 0

    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "gamecontroller")
                .font(.system(size: 48, weight: .semibold))
                .foregroundColor(EspressUTheme.cremaTopDark)
                .rotationEffect(.degrees(rotation))
                .onAppear {
                    withAnimation(.linear(duration: 2).repeatForever(autoreverses: false)) {
                        rotation = 360
                    }
                }

            Text("Loading games...")
                .font(.system(size: 16, weight: .semibold, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
        }
    }
}

struct EmptyGamesView: View {
    enum Kind: Equatable {
        case emptyLibrary
        case noFavourites
        case noSearchMatch(String)
    }

    let kind: Kind
    let onImportTapped: () -> Void

    private var symbol: String {
        switch kind {
        case .emptyLibrary: return "doc.questionmark"
        case .noFavourites: return "heart"
        case .noSearchMatch: return "magnifyingglass"
        }
    }

    private var heading: String {
        switch kind {
        case .emptyLibrary: return "No Games Yet"
        case .noFavourites: return "No Favourites Yet"
        case .noSearchMatch: return "No Matches"
        }
    }

    private var lines: [String] {
        switch kind {
        case .emptyLibrary:
            return ["Tap Import a Game, or put game files in Files > On My iPad > EspressU > Roms.",
                    "Games you add there show up when you come back to EspressU."]
        case .noFavourites:
            return ["Tap the heart on a game to add it here."]
        case .noSearchMatch(let text):
            return ["Nothing matches \"\(text)\"."]
        }
    }

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: symbol)
                .font(.system(size: 56, weight: .regular))
                .foregroundColor(EspressUTheme.cremaTopDark.opacity(0.5))
                .accessibilityHidden(true)

            VStack(spacing: 8) {
                Text(heading)
                    .font(.system(size: 18, weight: .semibold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .accessibilityAddTraits(.isHeader)

                VStack(alignment: .center, spacing: 4) {
                    ForEach(lines, id: \.self) { line in
                        Text(line)
                            .font(.system(size: 13, weight: .regular, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                            .multilineTextAlignment(.center)
                    }
                }
                .padding(.horizontal, 24)
            }

            if kind == .emptyLibrary {
                Button(action: onImportTapped) {
                    HStack(spacing: 6) {
                        Image(systemName: "doc.badge.plus")
                            .font(.system(size: 12, weight: .semibold))
                        Text("Import a Game")
                            .font(.system(size: 13, weight: .semibold, design: .rounded))
                    }
                }
                .buttonStyle(EspressUPrimaryButtonStyle())
                .padding(.top, 4)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

enum LaunchLogSettings {
    static let showKey = "espressu.showLaunchLog"
}

struct EmulatorViewOptimized: View {
    @ObservedObject private var uiStyle = UIStyleStore.shared

    let game: GameMetadata
    @ObservedObject var gameManager: GameManager
    @Binding var isRunning: Bool
    private let controllerSkin = WiiUControllerSkin.standard
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @State private var isEditingControlLayout = false
    @State private var padControlsHidden = false
    @State private var isPaused = false
    @State private var showHomeMenu = false
    @AppStorage(ControllerLayoutSettings.autoHideWithControllerKey)
    private var autoHideWithController = ControllerLayoutSettings.defaultAutoHideWithController
    @State private var controllerConnected: Bool?
    @State private var padShownDespiteController = false
    @State private var pausedByLifecycle = false
    private static let titlePauseQueue = DispatchQueue(label: "espressu.title.pause", qos: .userInitiated)
    @AppStorage(PreviewPadStore.enabledKey) private var previewPadEnabled = PreviewPadStore.defaultEnabled
    @AppStorage(ControllerLayoutSettings.joystickKey) private var joystickMode = ControllerLayoutSettings.defaultJoystick
    @State private var touchLabScreens = TouchLabScreenState()
    @State private var topBarHeight: CGFloat = 0
    @AppStorage(TopBarAutoHide.enabledKey) private var topBarAutoHideEnabled = TopBarAutoHide.defaultEnabled
    @State private var topBarHidden = false
    @GestureState private var topBarTouched = false
    @State private var voiceOverRunning = UIAccessibility.isVoiceOverRunning
    @ObservedObject private var previewPad = PreviewPadStore.shared
    @AppStorage(DisplayLayoutSettings.showSwapButtonKey)
    private var showSwapButton = DisplayLayoutSettings.defaultShowSwapButton
    @ObservedObject private var displayRouter = DisplayRouter.shared

    @AppStorage(LocalScreenLayoutSettings.layoutKey)
    private var screenLayout = ScreenLayout.initialValue
    @AppStorage(LocalScreenLayoutSettings.showSwapButtonKey)
    private var showLocalSwapButton = LocalScreenLayoutSettings.defaultShowSwapButton
    @State private var localSwapped = false
    @AppStorage(LaunchLogSettings.showKey) private var showLaunchLog = false
    @State private var launchLog = LaunchLogStore()
    @State private var launchLogDismissed = false

    @State private var showLaunchIntro = true
    @AppStorage("espressu.showLaunchIntro") private var launchIntroEnabled = true
    @State private var showingBackConfirmation = false

    @State private var stallCardDismissed = false

    @AppStorage(AudioSettings.microphoneEnabledKey) private var realMicEnabled = AudioSettings.defaultMicrophoneEnabled
    private var realMicInUse: Bool { realMicEnabled && MicrophoneAccess.status != .denied }

    @AppStorage(EmulatedDevicesSettings.skylanderPortalKey) private var skylanderPortalEnabled = EmulatedDevicesSettings.defaultEnabled
    @AppStorage(EmulatedDevicesSettings.infinityBaseKey) private var infinityBaseEnabled = EmulatedDevicesSettings.defaultEnabled
    @AppStorage(EmulatedDevicesSettings.dimensionsToypadKey) private var dimensionsToypadEnabled = EmulatedDevicesSettings.defaultEnabled
    @State private var showEmulatedDevices = false
    @State private var showAmiibo = false
    private var anyEmulatedDeviceEnabled: Bool {
        skylanderPortalEnabled || infinityBaseEnabled || dimensionsToypadEnabled
    }

    private enum PadSystem {
        case touchLab
        case preview
        case espressu
    }

    private var padSystem: PadSystem {
        return .touchLab
    }

    private var isPhonePortrait: Bool {
        UIDevice.current.userInterfaceIdiom == .phone && verticalSizeClass == .regular
    }

    @ViewBuilder private func belowPicture<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        let built = content()
        if isPhonePortrait {
            GeometryReader { geometry in
                let pictures = visibleScreens.reduce(CGFloat(0)) { $0 + portraitScreenHeight(main: $1, in: geometry.size) }
                let needed = ControllerGeometry.Portrait.minimumHeight(joystick: joystickMode)
                let height = min(geometry.size.height, max(geometry.size.height - pictures, needed))
                VStack(spacing: 0) {
                    Spacer(minLength: 0)
                    built
                        .frame(height: height)
                }
            }
        } else {
            built
        }
    }

    private var nativeOverlayActive: Bool {
        switch gameManager.emulationState {
        case .loading, .running, .paused: return displayRouter.placement != .dualScreen
        case .idle, .error: return false
        }
    }

    private var gameName: String { game.displayTitle ?? game.title }

    private func togglePause() {
        isPaused.toggle()
        pausedByLifecycle = false
        cemu_bridge_release_all_buttons()
        setTitlePaused(isPaused)
    }

    private func setTitlePaused(_ pause: Bool) {
        Self.titlePauseQueue.async {
            if pause {
                cemu_bridge_pause()
            } else {
                cemu_bridge_resume()
            }
        }
    }

    @ViewBuilder private var layoutPanel: some View {
        let gameID = gameManager.currentGame?.settingsKey
        switch padSystem {
        case .touchLab:
            TouchLabLayoutPanel(gameID: gameID, onDone: finishEditingLayout)
        case .preview:
            PreviewLayoutPanel(onDone: finishEditingLayout)
        case .espressu:
            EspressUPadLayoutPanel(onDone: finishEditingLayout)
        }
    }

    private func finishEditingLayout() {
        withAnimation(.easeInOut(duration: 0.2)) {
            isEditingControlLayout = false
        }
    }

    /// The on-screen controls are hidden when the player hid them in the home menu or a controller hides them, but
    /// never while the layout is being edited.
    /// Worked out on every redraw from the setting and the connection, so a change to either applies at once.
    private var padHiddenByController: Bool {
        autoHideWithController && controllerConnected == true && !padShownDespiteController
    }

    private var padHidden: Bool {
        !isEditingControlLayout && (padControlsHidden || padHiddenByController)
    }

    private func controllerPresenceChanged(_ connected: Bool, atStart: Bool) {
        let previous = controllerConnected
        guard connected != previous else { return }
        let wasHidden = padHiddenByController
        controllerConnected = connected
        padShownDespiteController = false
        if !connected { padControlsHidden = false }
        if padHiddenByController { cemu_bridge_release_all_buttons() }
        guard autoHideWithController, padHiddenByController || wasHidden else { return }
        if atStart && gameManager.launchNotice != nil { return }
        gameManager.showLaunchNotice(connected
            ? "Controller connected. On-screen controls hidden. Menu: HOME, the touchpad or Select + Start."
            : "Controller disconnected. On-screen controls shown.")
    }

    private func openHomeMenu() {
        guard gameManager.emulationState == .running, !showHomeMenu, !isEditingControlLayout else { return }
        if isPaused {
            pausedByLifecycle = false
            cemu_bridge_release_all_buttons()
        } else {
            togglePause()
        }
        withAnimation(.easeInOut(duration: 0.15)) { showHomeMenu = true }
    }

    private func closeHomeMenu() {
        withAnimation(.easeInOut(duration: 0.15)) { showHomeMenu = false }
        if isPaused { togglePause() }
    }

    private func moveControlsFromHomeMenu() {
        closeHomeMenu()
        withAnimation(.easeInOut(duration: 0.2)) { isEditingControlLayout = true }
        cemu_bridge_release_all_buttons()
    }

    private func swapScreensFromHomeMenu() {
        if displayRouter.placement == .dualScreen {
            DisplayRouter.shared.toggleScreenLayoutFromSwapButton()
        } else {
            localSwapped.toggle()
        }
    }

    private func handleHomeMenuEvent(_ event: HomeMenuEvent) {
        if isEditingControlLayout {
            if event == .homeButton || event == .back { finishEditingLayout() }
            return
        }
        switch event {
        case .homeButton:
            if showEmulatedDevices {
                showEmulatedDevices = false
            } else if showAmiibo {
                showAmiibo = false
            } else if showHomeMenu {
                if !showingBackConfirmation { closeHomeMenu() }
            } else {
                openHomeMenu()
            }
        case .back:
            break
        case .up, .down, .confirm:
            break
        }
    }

    @ViewBuilder private var homeMenuLayer: some View {
        HomeMenuOverlay(
            gameName: gameName,
            isActive: !showingBackConfirmation && !showEmulatedDevices && !showAmiibo,
            screenLayout: $screenLayout,
            isDualScreen: displayRouter.placement == .dualScreen,
            canQuit: true,
            touchControlsHidden: isPadViewVisible ? padHidden : nil,
            offersEmulatedDevices: anyEmulatedDeviceEnabled,
            actions: HomeMenuActions(
                resume: closeHomeMenu,
                moveControls: moveControlsFromHomeMenu,
                quit: {
                    gameManager.quitEmulationAsync()
                    isRunning = true
                },
                swapScreens: swapScreensFromHomeMenu,
                scanAmiibo: { showAmiibo = true },
                toggleTouchControls: {
                    if padHidden {
                        padControlsHidden = false
                        padShownDespiteController = true
                    } else {
                        padControlsHidden = true
                    }
                    cemu_bridge_release_all_buttons()
                },
                emulatedDevices: { showEmulatedDevices = true }
            )
        )
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if padSystem == .preview {
                GeometryReader { proxy in
                    let insets = proxy.safeAreaInsets
                    let full = proxy.frame(in: .local)
                    let safeArea = CGRect(x: full.minX + insets.leading, y: full.minY + insets.top,
                                          width: full.width - insets.leading - insets.trailing,
                                          height: full.height - insets.top - insets.bottom)
                    let resolved = previewPad.resolve(container: proxy.size, safeArea: safeArea,
                                                      pointsPerInch: DeviceMetrics.current().pointsPerInch)
                    let native = previewPad.effectiveDisplayMode(container: proxy.size) == .native
                    ZStack(alignment: .topLeading) {
                        MetalViewIOS(gameManager: gameManager)
                    }
                    .frame(width: native ? resolved.video.width : proxy.size.width,
                          height: native ? resolved.video.height : proxy.size.height)
                    .position(x: native ? resolved.video.midX : proxy.size.width / 2,
                             y: native ? resolved.video.midY : proxy.size.height / 2)
                    .clipped()

                    if !padHidden {
                        PreviewControllerPad(
                            store: previewPad,
                            onInput: { label, pressed in
                                sendPadButton(label, pressed)
                            },
                            onStick: { stick, position in
                                PadDiagnostics.shared.recordStick(stick, position)
                                cemu_bridge_set_stick_axis(
                                    stick == 0 ? CEMU_BRIDGE_STICK_LEFT : CEMU_BRIDGE_STICK_RIGHT,
                                    Float(position.x), Float(position.y)
                                )
                            },
                            isEditingLayout: $isEditingControlLayout
                        )
                        .onAppear { PadDiagnostics.shared.report(activePad: .preview) }
                        .onDisappear { cemu_bridge_release_all_buttons() }
                    }

                    #if DEBUG
                    VStack {
                        Text("PAD DEBUG: \(PadDiagnostics.shared.lastInput) / \(PadDiagnostics.shared.lastStick)")
                            .font(.system(size: 11, weight: .semibold, design: .monospaced))
                            .foregroundColor(.yellow)
                            .padding(6)
                            .background(Color.black.opacity(0.7))
                            .cornerRadius(6)
                            .padding(.top, 4)
                        Spacer()
                    }
                    .allowsHitTesting(false)
                    #endif
                }
            } else {
                screenLayoutComposition
            }

            NativeCoreOverlayView(active: nativeOverlayActive, topInset: overlayTopInset)

            VStack(spacing: 0) {
                HStack(alignment: .top, spacing: 10) {
                    HStack(spacing: 2) {
                        if gameManager.emulationState == .loading {
                            InGamePillButton(symbol: "xmark", title: "Cancel") {
                                gameManager.stopEmulation()
                                isRunning = true
                            }
                        } else if isEditingControlLayout {
                            InGamePillButton(symbol: "checkmark", title: "Done") { finishEditingLayout() }
                        } else {
                            InGamePillButton(symbol: "house.fill", title: isPhonePortrait ? nil : "Menu", action: openHomeMenu)
                                .disabled(gameManager.emulationState != .running || showHomeMenu)
                                .accessibilityLabel("HOME menu")
                            InGamePillButton(symbol: isPaused ? "play.fill" : "pause.fill", title: nil, action: togglePause)
                                .disabled(gameManager.emulationState != .running)
                                .accessibilityLabel(isPaused ? "Resume" : "Pause")
                        }
                    }
                    .padding(4)
                    .espressUSurface(Capsule())

                    Spacer(minLength: 0)

                    if gameManager.emulationState == .running {
                        InGameBlowButton(enabled: !isPaused)
                        HStack(spacing: 6) {
                            Circle()
                                .fill(gameManager.frameRate >= 20 ? Color.green : EspressUTheme.alertOnDark)
                                .frame(width: 8, height: 8)
                            Text(gameManager.progress.hudText(wholeFramesPerSecond: gameManager.frameRate))
                                .font(.system(size: 13, weight: .bold, design: .rounded))
                                .monospacedDigit()
                                .foregroundColor(.white)
                                .lineLimit(1)
                        }
                        .padding(.horizontal, 14)
                        .frame(height: 40)
                        .espressUSurface(Capsule())
                        .accessibilityElement(children: .ignore)
                        .accessibilityLabel("Frame rate")
                        .accessibilityValue(gameManager.progress.hudText(wholeFramesPerSecond: gameManager.frameRate))
                    }
                }
                .environment(\.colorScheme, .dark)
                .padding(WindowSafeArea.padding(minimum: 12))
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0).updating($topBarTouched) { _, touched, _ in touched = true }
                )
                .topBarAutoHideEffect(hidden: topBarHidden, slideDistance: topBarHeight, slides: !reduceMotion)
                .reportTopBarBottom()
                .dynamicTypeSize(...DynamicTypeSize.xLarge)

            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            .onPreferenceChange(TopBarBottomKey.self) { bottom in
                if !topBarHidden, bottom != topBarHeight { topBarHeight = bottom }
            }
            .overlay(alignment: .top) { topBarRevealHandle }
            .onChange(of: isPaused) { paused in
                if paused, topBarHidden { setTopBarHidden(false) }
            }
            .task(id: topBarHideKey) { await runTopBarAutoHide(topBarHideKey) }
            .onReceive(NotificationCenter.default.publisher(for: UIAccessibility.voiceOverStatusDidChangeNotification)) { _ in
                voiceOverRunning = UIAccessibility.isVoiceOverRunning
            }

            if !padHidden {
                Group {
                if padSystem == .touchLab {
                    TouchLabPadOverlay(
                        gameID: gameManager.currentGame?.settingsKey,
                        screens: touchLabScreens,
                        enabled: !isPaused && !isEditingControlLayout,
                        topInset: isPhonePortrait ? 0 : topBarHeight
                    )
                    .onAppear { PadDiagnostics.shared.report(activePad: .touchLab) }
                } else if padSystem == .espressu {
                    belowPicture {
                    OptimizedControlPanel(
                        skin: controllerSkin,
                        onInput: { label, pressed in
                            sendPadButton(label, pressed)
                        },
                        onStick: { stick, position in
                            PadDiagnostics.shared.recordStick(stick, position)
                            cemu_bridge_set_stick_axis(
                                stick == 0 ? CEMU_BRIDGE_STICK_LEFT : CEMU_BRIDGE_STICK_RIGHT,
                                Float(position.x),
                                Float(position.y)
                            )
                        },
                        isEditingLayout: $isEditingControlLayout,
                        isPaused: isPaused,
                        topInset: isPhonePortrait ? 0 : topBarHeight,
                        portrait: isPhonePortrait
                    )
                    .onAppear { PadDiagnostics.shared.report(activePad: .espressu) }
                    }
                }
                }
                .dynamicTypeSize(...DynamicTypeSize.xLarge)
            }

            if PadDiagnostics.shared.isEnabled {
                VStack {
                    HStack {
                        PadDiagnosticsOverlay(
                            padControlsHidden: padHidden,
                            previewPadEnabled: previewPadEnabled,
                            touchLabScheme: TouchLabSettings.schemeID,
                            isEditingLayout: isEditingControlLayout,
                            isPaused: isPaused
                        )
                        Spacer()
                    }
                    Spacer()
                }
                .allowsHitTesting(false)
            }

            if isPaused && !showHomeMenu {
                VStack(spacing: 10) {
                    Image(systemName: "pause.circle.fill")
                        .font(.system(size: 40))
                        .accessibilityHidden(true)
                    Text("PAUSED")
                        .font(.system(size: 22, weight: .bold, design: .rounded))
                        .tracking(2)
                    Text("Tap to resume")
                        .font(.system(size: 14, weight: .semibold, design: .rounded))
                        .opacity(0.85)
                }
                .foregroundColor(.white)
                .padding(28)
                .background(Color.black.opacity(0.6))
                .cornerRadius(20)
                .transition(.opacity)
                .contentShape(RoundedRectangle(cornerRadius: 20))
                .onTapGesture { togglePause() }
                .accessibilityAddTraits(.isButton)
                .accessibilityLabel("Paused. Resume")
            }

            if gameManager.emulationState == .loading {
                VStack(spacing: 12) {
                    ProgressView()
                        .tint(.white)
                    Text("Starting \(game.title)…")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                        .foregroundColor(.white.opacity(0.8))
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 24)

                    if showLaunchLog {
                        LaunchLogView(store: launchLog)
                            .frame(maxWidth: 720, maxHeight: 340)
                            .padding(.horizontal, 24)
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color.black)
            }

            if launchIntroVisible {
                EspressUIntroView(isGameRunning: gameManager.emulationState == .running) { showLaunchIntro = false }
                    .transition(.opacity)
                    .zIndex(10)
            }

            if gameManager.emulationState == .loading {
                BootBackButton {
                    gameManager.stopEmulation()
                    isRunning = true
                }
                .zIndex(11)
            }

            if showLaunchLog && gameManager.emulationState == .running && !launchLogDismissed {
                VStack {
                    Spacer()
                    LaunchLogView(store: launchLog) {
                        launchLog.stop()
                        withAnimation(.easeInOut(duration: 0.2)) { launchLogDismissed = true }
                    }
                    .frame(maxWidth: 720, maxHeight: 240)
                    .padding(.horizontal, 24)
                    .padding(.bottom, 180)
                }
                .transition(.opacity)
            }

            if isEditingControlLayout {
                layoutPanel
            }

            if showHomeMenu {
                homeMenuLayer
                    .zIndex(20)
            }
        }
        .onAppear {
            if showLaunchLog { launchLog.start() }
            OrientationPolicy.setInGame(true)
        }
        .onDisappear {
            launchLog.stop()
            OrientationPolicy.setInGame(false)
            cemu_bridge_release_all_buttons()
        }
        .onChange(of: showLaunchLog) { enabled in
            if enabled { launchLog.start() } else { launchLog.stop() }
        }
        .onChange(of: scenePhase) { newPhase in
            if newPhase == .active {
                guard pausedByLifecycle else { return }
                pausedByLifecycle = false
                isPaused = false
                setTitlePaused(false)
            } else {
                cemu_bridge_release_all_buttons()
                cemu_bridge_set_pad_touch(0, 0, false)
                guard !isPaused else { return }
                isPaused = true
                pausedByLifecycle = true
                setTitlePaused(true)
            }
        }
        .onChange(of: gameManager.emulationState) { state in
            guard state == .running else { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { showGamePadHintIfNeeded() }
            if scenePhase != .active && !isPaused {
                isPaused = true
                pausedByLifecycle = true
            }
            if isPaused { setTitlePaused(true) }
        }
        .onChange(of: isPadViewVisible) { visible in
            if !visible { padControlsHidden = false }
            if visible { showGamePadHintIfNeeded() }
        }
        .hidingSystemOverlaysDuringPlay()
        .modifier(HeatNoticeModifier { gameManager.showLaunchNotice($0) })
        .modifier(HomeMenuEventsModifier(isOpen: showHomeMenu || isEditingControlLayout, onEvent: handleHomeMenuEvent))
        .modifier(ControllerAutoHideModifier(apply: controllerPresenceChanged))
        .onChange(of: autoHideWithController) { _ in
            padShownDespiteController = false
            cemu_bridge_release_all_buttons()
        }
        .overlay(alignment: .top) {
            if showsStallCard {
                videoStalledCard
                    .padding(.top, overlayTopInset + 8)
                    .padding(.horizontal, 16)
                    .transition(.opacity)
            }
        }
        .onChange(of: gameManager.videoStalled) { stalled in
            if !stalled {
                stallCardDismissed = false
            }
        }
        .onChange(of: gameManager.videoStallKind) { kind in
            if kind != 0 { stallCardDismissed = false }
        }
        .sheet(isPresented: $showEmulatedDevices) {
            EmulatedDevicesView()
        }
        .sheet(isPresented: $showAmiibo) {
            AmiiboSheet { message in
                showAmiibo = false
                closeHomeMenu()
                gameManager.showLaunchNotice(message)
            }
        }
    }

    private var launchIntroVisible: Bool {
        showLaunchIntro && launchIntroEnabled && !showLaunchLog && !reduceMotion
    }

    private var overlayTopInset: CGFloat { topBarHeight }

    private var topBarMayHide: Bool {
        topBarAutoHideEnabled
            && !voiceOverRunning
            && gameManager.emulationState == .running
            && !launchIntroVisible
            && !isPaused
            && !showHomeMenu
            && !isEditingControlLayout
            && !showEmulatedDevices
            && !showAmiibo
            && !showingBackConfirmation
            && !showsStallCard
            && !topBarTouched
    }

    private struct TopBarHideKey: Equatable {
        var mayHide: Bool
        var hidden: Bool
    }

    private var topBarHideKey: TopBarHideKey {
        TopBarHideKey(mayHide: topBarMayHide, hidden: topBarHidden)
    }

    private func runTopBarAutoHide(_ key: TopBarHideKey) async {
        guard key.mayHide else {
            if key.hidden { setTopBarHidden(false) }
            return
        }
        guard !key.hidden else { return }
        try? await Task.sleep(nanoseconds: TopBarAutoHide.hideDelayNanoseconds)
        guard !Task.isCancelled else { return }
        setTopBarHidden(true)
        showHintOnce(Self.topBarHintKey, "Tap or swipe down at the top edge to bring the bar back.")
    }

    private static let topBarHintKey = "espressu.hint.topBarReveal"
    private static let gamePadHintKey = "espressu.hint.gamePadTouch"

    private func showHintOnce(_ key: String, _ text: String, attempts: Int = 3, ready: @escaping () -> Bool = { true }) {
        let defaults = UserDefaults.standard
        guard !defaults.bool(forKey: key), ready() else { return }
        if gameManager.launchNotice != nil {
            guard attempts > 0 else { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) {
                showHintOnce(key, text, attempts: attempts - 1, ready: ready)
            }
            return
        }
        defaults.set(true, forKey: key)
        gameManager.showLaunchNotice(text)
    }

    private func showGamePadHintIfNeeded() {
        #if os(iOS)
        showHintOnce(Self.gamePadHintKey, "Tap the GamePad screen to use it in the game.", ready: {
            gameManager.emulationState == .running && (isPadViewVisible || padIsOnDeviceInDualScreen)
        })
        #endif
    }

    private func setTopBarHidden(_ hidden: Bool) {
        withAnimation(.easeInOut(duration: 0.25)) { topBarHidden = hidden }
    }

    @ViewBuilder private var topBarRevealHandle: some View {
        if topBarHidden {
            // Nothing stays on screen: a downward swipe (or a tap) on the top edge brings the bar back.
            Color.clear
                .frame(height: UIDevice.current.userInterfaceIdiom == .phone ? 56 : 80)
                .frame(maxWidth: .infinity)
                .contentShape(Rectangle())
                .onTapGesture { setTopBarHidden(false) }
                .gesture(
                    DragGesture(minimumDistance: 12)
                        .onEnded { value in
                            if value.translation.height > 16 { setTopBarHidden(false) }
                        }
                )
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("Show top bar")
                .accessibilityAddTraits(.isButton)
                .accessibilityAction { setTopBarHidden(false) }
        }
    }

    private var showsStallCard: Bool {
        guard gameManager.emulationState == .running, !stallCardDismissed else { return false }
        return gameManager.videoStalled
    }

    private var stallTitle: String {
        switch gameManager.videoStallKind {
        case 2: return "Graphics stopped working"
        case 3: return "Out of memory for the picture"
        case 4: return "Memory is running low"
        case 5: return "The screen stopped updating"
        default: return "The picture froze"
        }
    }

    private var stallAdvice: String {
        switch gameManager.videoStallKind {
        case 2: return "iOS stopped running this game's graphics. Tap Quit Game. EspressU will then ask you to close and reopen it before the next game."
        case 3: return "Tap Quit Game and reopen EspressU. After you quit, a lower Resolution (in Settings, Graphics) uses less memory."
        case 4: return "iOS may close EspressU soon. Quit the game soon. After you quit, a lower Resolution (in Settings, Graphics) uses less memory."
        case 5: return "The game is still running but the screen isn't taking frames. This goes away by itself if it recovers. If it doesn't, tap Quit Game and reopen EspressU."
        default: return "The picture has stopped while the game keeps running. This goes away by itself if the picture comes back."
        }
    }

    private var stallDismissTitle: String {
        let kind = gameManager.videoStallKind
        return (kind == 1 || kind == 5) ? "Keep waiting" : "Dismiss"
    }

    private var videoStalledCard: some View {
        VStack(spacing: 10) {
            Text(stallTitle)
                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                .foregroundColor(.white)
                .multilineTextAlignment(.center)
                .accessibilityAddTraits(.isHeader)
            if gameManager.videoStalled {
                Text(stallAdvice)
                    .font(.system(.caption, design: .rounded))
                    .foregroundColor(.white.opacity(0.85))
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 8) {
                Button("Quit Game") {
                    gameManager.stopEmulation()
                    isRunning = true
                }
                Button(stallDismissTitle) {
                    stallCardDismissed = true
                }
            }
            .buttonStyle(EspressUBarButtonStyle())
        }
        .padding(14)
        .background(Color.black.opacity(0.78), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .frame(maxWidth: 420)
    }

    #if os(iOS)
    private var screenLayoutComposition: some View {
        GeometryReader { geometry in
            let portrait = geometry.size.height >= geometry.size.width
            let phonePortrait = UIDevice.current.userInterfaceIdiom == .phone && portrait

            if phonePortrait {
                screensSizeLayout(in: geometry.size)
            } else if screenLayout == .smallGamePadTopRight && displayRouter.placement != .dualScreen {
                let padWidth = geometry.size.width * 0.25
                let padHeight = min(padWidth * 9 / 16, geometry.size.height)

                HStack(alignment: .top, spacing: 0) {
                    MetalViewIOS(gameManager: gameManager)
                        .frame(width: geometry.size.width - padWidth, height: geometry.size.height)
                        .touchLabTVScreen()

                    padScreen
                        .frame(width: padWidth, height: padHeight)
                        .padding(.top, topBarHidden ? 0 : max(0, topBarHeight - geometry.frame(in: .global).minY))
                        .animation(reduceMotion ? nil : .easeInOut(duration: 0.25), value: topBarHidden)
                }
            } else if portrait {
                VStack(spacing: 0) { screens }
            } else {
                HStack(spacing: 0) { screens }
            }
        }
        .ignoresSafeArea(.all, edges: verticalSizeClass == .regular ? .horizontal : .all)
        .trackTouchLabScreens($touchLabScreens, imageIsAspectFit: { !FrameStretch.isEnabled })
        .onAppear { updateVisibleOutputs() }
        .onChange(of: screenLayout) { _ in updateVisibleOutputs() }
        .onChange(of: localSwapped) { _ in updateVisibleOutputs() }
        .onChange(of: displayRouter.placement) { _ in updateVisibleOutputs() }
        .onDisappear {
            DisplayRouter.shared.updateLocalVisibleOutputs(showTV: true, showPad: false)
            cemu_bridge_set_pad_touch(0, 0, false)
        }
    }

    private var visibleScreens: [Bool] {
        if displayRouter.placement == .dualScreen { return [true] }
        if screenLayout.showsBothScreens { return localSwapped ? [false, true] : [true, false] }
        return [!localSwapped]
    }

    private func screensSizeLayout(in size: CGSize) -> some View {
        return VStack(spacing: 0) {
            ForEach(visibleScreens, id: \.self) { main in
                let height = portraitScreenHeight(main: main, in: size)
                screenView(main: main)
                    .frame(width: height * 16 / 9, height: height)
                    .frame(width: size.width)
            }
            Spacer(minLength: 0)
        }
        .frame(width: size.width, height: size.height, alignment: .top)
    }

    private func portraitScreenHeight(main: Bool, in size: CGSize) -> CGFloat {
        let full = size.width * 9.0 / 16.0
        guard !main, visibleScreens.count > 1 else { return full }
        let room = size.height - full - ControllerGeometry.Portrait.minimumHeight(joystick: joystickMode)
        return min(full, max(full * 0.55, room))
    }

    private var screens: some View {
        ForEach(visibleScreens, id: \.self) { main in
            screenView(main: main)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    @ViewBuilder
    private func screenView(main: Bool) -> some View {
        if main {
            MetalViewIOS(gameManager: gameManager)
                .simultaneousGesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            if padIsOnDeviceInDualScreen { sendPadTouch(value.location, down: true) }
                        }
                        .onEnded { value in
                            if padIsOnDeviceInDualScreen { sendPadTouch(value.location, down: false) }
                        }
                )
                .touchLabTVScreen()
        } else {
            padScreen
        }
    }

    private var padIsOnDeviceInDualScreen: Bool {
        displayRouter.gamePadTouchOnDevice
    }

    private var padScreen: some View {
        PadMetalViewIOS()
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in sendPadTouch(value.location, down: true) }
                    .onEnded { value in sendPadTouch(value.location, down: false) }
            )
            .touchLabGamePadScreen()
    }

    private func sendPadTouch(_ location: CGPoint, down: Bool) {
        let scale = DisplayRouter.shared.padSurfaceScale
        cemu_bridge_set_pad_touch(Double(location.x) * scale, Double(location.y) * scale, down)
    }

    private var isPadViewVisible: Bool {
        visibleScreens.contains(false) || padIsOnDeviceInDualScreen
    }

    private func updateVisibleOutputs() {
        let both = displayRouter.placement == .dualScreen || screenLayout.showsBothScreens
        DisplayRouter.shared.updateLocalVisibleOutputs(showTV: both || !localSwapped, showPad: both || localSwapped)
        if !both && !localSwapped {
            cemu_bridge_set_pad_touch(0, 0, false)
        }
    }
    #endif
}

private struct HideSystemOverlaysIfAvailable: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 16.0, *) {
            content.persistentSystemOverlays(.hidden).statusBarHidden(true)
        } else {
            content
        }
    }
}

private extension View {
    func hidingSystemOverlaysDuringPlay() -> some View {
        modifier(HideSystemOverlaysIfAvailable())
    }
}

@MainActor private func sendPadButton(_ label: String, _ pressed: Bool) {
    PadDiagnostics.shared.recordInput(label, pressed)
    if label == "HOME" {
        HomeMenuRouter.shared.padHome(pressed: pressed)
        return
    }
    cemu_bridge_set_button_state(cemuBridgeButton(forLabel: label), pressed)
}

private func cemuBridgeButton(forLabel label: String) -> CemuBridgeButton {
    switch label {
    case "up":    return CEMU_BRIDGE_BUTTON_UP
    case "down":  return CEMU_BRIDGE_BUTTON_DOWN
    case "left":  return CEMU_BRIDGE_BUTTON_LEFT
    case "right": return CEMU_BRIDGE_BUTTON_RIGHT

    case "A": return CEMU_BRIDGE_BUTTON_A
    case "B": return CEMU_BRIDGE_BUTTON_B
    case "X": return CEMU_BRIDGE_BUTTON_X
    case "Y": return CEMU_BRIDGE_BUTTON_Y

    case "L":  return CEMU_BRIDGE_BUTTON_L
    case "R":  return CEMU_BRIDGE_BUTTON_R
    case "ZL": return CEMU_BRIDGE_BUTTON_ZL
    case "ZR": return CEMU_BRIDGE_BUTTON_ZR

    case "plus":  return CEMU_BRIDGE_BUTTON_PLUS
    case "minus": return CEMU_BRIDGE_BUTTON_MINUS

    case "L3": return CEMU_BRIDGE_BUTTON_STICK_L
    case "R3": return CEMU_BRIDGE_BUTTON_STICK_R

    case "HOME": return CEMU_BRIDGE_BUTTON_HOME

    default: return CEMU_BRIDGE_BUTTON_NONE
    }
}

struct BorderBottomModifier: ViewModifier {
    let width: CGFloat
    let color: Color

    func body(content: Content) -> some View {
        VStack(spacing: 0) {
            content
            Divider()
                .frame(height: width)
                .background(color)
        }
    }
}

extension View {
    func borderBottom(width: CGFloat, color: Color) -> some View {
        self.modifier(BorderBottomModifier(width: width, color: color))
    }
}

#Preview {
    ContentView()
}
