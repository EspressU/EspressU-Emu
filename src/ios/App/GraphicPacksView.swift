// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

struct GraphicPacksView: View {
    var game: GameMetadata? = nil

    @ObservedObject private var store = GraphicPackStore.shared
    @State private var search = ""
    @State private var importMessage: GraphicPackStore.Notice?

    private var profile: GraphicPackDeviceProfile { .current }

    private var gamePacks: [GraphicPack] {
        guard let game else { return [] }
        return store.packs.filter { !$0.universal && $0.appliesTo(titleId: game.titleId) }
    }

    private var universalPacks: [GraphicPack] { store.packs.filter(\.universal) }

    private var sourceFirst: Bool { game == nil || store.installed == nil || store.updateAvailable || store.isBusy }

    private var list: some View {
        List {
            if sourceFirst { GraphicPackSourceSection(store: store) }
            if store.gameRunning { runningCallout }

            if let game {
                gameSections(game)
            } else {
                allGamesSection
            }

            if !sourceFirst { GraphicPackSourceSection(store: store) }
            importSection
            attributionSection
        }
        .navigationTitle("Graphic Packs")
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .onAppear { store.onOpen() }
        .refreshable { store.reload() }
    }

    @ViewBuilder var body: some View {
        if game == nil {
            list.searchable(text: $search, prompt: "Search games and packs")
        } else {
            list
        }
    }

    private var runningCallout: some View {
        Section {
            ScreenStatusCallout(tone: .info, message: "A game is running. You can browse packs, but they can only be changed after you close it.")
        }
        .listRowBackground(Color.clear)
    }

    @ViewBuilder private func gameSections(_ game: GameMetadata) -> some View {
        if game.titleId == nil {
            Section {
                ScreenStatusCallout(tone: .info, message: "EspressU couldn't read this game's title ID, so it can't pick out its packs. Browse all packs instead.")
            }
            .listRowBackground(Color.clear)
        } else if gamePacks.isEmpty && universalPacks.isEmpty {
            Section {
                ScreenEmptyState(
                    systemImage: "square.stack.3d.up",
                    headline: store.packs.isEmpty ? "No packs yet" : "No packs for this game",
                    message: store.packs.isEmpty
                        ? "Download the community packs above, then come back."
                        : "The community packs don't include one for \(game.title). You can still import your own below."
                )
            }
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        } else {
            Section {
                ScreenStatusCallout(tone: .info, message: "Nothing turns on by itself. Higher resolutions and shadow options cost speed and memory, so raise them one step at a time. \(profile.summary)")
            }
            .listRowBackground(Color.clear)

            GraphicPackCategorySections(packs: gamePacks, store: store, profile: profile, suggestWorkarounds: true)

            if !universalPacks.isEmpty {
                GraphicPackCategorySections(packs: universalPacks, store: store, profile: profile,
                                            suggestWorkarounds: false, headerPrefix: "All games \u{00B7} ")
            }
        }

        Section {
            NavigationLink {
                GraphicPackGameListView()
            } label: {
                Label("Browse all packs", systemImage: "square.grid.2x2")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    @ViewBuilder private var allGamesSection: some View {
        if store.packs.isEmpty {
            Section {
                ScreenEmptyState(
                    systemImage: "square.stack.3d.up",
                    headline: store.isScanning ? "Looking for packs" : "No graphic packs yet",
                    message: "Download the community packs above, or import your own from Files."
                )
            }
            .listRowBackground(Color.clear)
            .listRowSeparator(.hidden)
        } else {
            GraphicPackGameListSection(packs: store.packs, search: $search)
        }
    }

    private var importSection: some View {
        Section {
            Button {
                DocumentImport.present(contentTypes: [.folder, .zip]) { result in
                    switch result {
                    case .success(let urls):
                        guard let url = urls.first else { return }
                        importMessage = nil
                        store.importPicked(url) { outcome in
                            switch outcome {
                            case .success(let done):
                                importMessage = .init(text: "Imported \(done.packCount) pack\(done.packCount == 1 ? "" : "s") from \(done.folderName). Changes apply the next time you launch a game.", isError: false)
                            case .failure(let error):
                                importMessage = .init(text: error.localizedDescription, isError: true)
                            }
                        }
                    case .failure(let error):
                        importMessage = .init(text: error.localizedDescription, isError: true)
                    }
                }
            } label: {
                Label("Import pack folder or zip\u{2026}", systemImage: "square.and.arrow.down")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .disabled(store.gameRunning)

            if let importMessage {
                ScreenStatusCallout(tone: importMessage.isError ? .warning : .info, message: importMessage.text)
            }
        } header: {
            SettingsSectionHeader("Your own packs", icon: "folder.badge.plus", accent: .content)
        } footer: {
            InfoButton.footer(
                "A folder with a rules.txt, a folder of packs, or a zip of either.",
                title: "Import packs",
                text: "Pick a pack folder, a folder that holds several packs, or a zip of either. EspressU checks that it really contains a pack (a rules.txt) before adding it.\n\nImported packs are kept apart from the community download, so updating the community packs never removes them. Importing the same folder name again replaces the earlier copy. To remove an imported pack, open it and choose Remove.\n\nYou can also drop pack folders into graphicPacks inside EspressU's Documents folder in Files."
            )
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var attributionSection: some View {
        Section {
            Link(destination: GraphicPackStore.repoPage) {
                Label("Cemu graphic packs on GitHub", systemImage: "link")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .tint(EspressUTheme.accentText)
        } header: {
            SettingsSectionHeader("Credits", icon: "heart", accent: .identity)
        } footer: {
            InfoButton.footer(
                "The community packs are made by the Cemu graphic packs project and its contributors.",
                title: "Where packs come from",
                text: "The downloadable packs are the Cemu graphic packs project's community collection (github.com/cemu-project/cemu_graphic_packs), made and maintained by its many contributors and released under the CC0 1.0 public domain dedication. EspressU downloads them straight from that project's releases and doesn't change them.\n\nPacks that replace shaders are written for OpenGL and Vulkan. The Metal renderer can't use those shaders, so such packs are marked. Packs made only of settings, presets and patches work with Metal."
            )
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct GraphicPackSourceSection: View {
    @ObservedObject var store: GraphicPackStore

    var body: some View {
        Section {
            SettingsRow(label: "Installed", value: installedText, icon: "shippingbox")

            if store.updateAvailable, let latest = store.latest {
                ScreenStatusCallout(
                    tone: .info,
                    message: store.installed == nil
                        ? "Community packs are available (\(latest.name), \(GraphicPackStorage.describe(latest.size)) to download)."
                        : "Update available: \(latest.name). Your choices are kept."
                )
            }

            progressOrActions

            if let notice = store.notice {
                ScreenStatusCallout(tone: notice.isError ? .warning : .info, message: notice.text)
            }
        } header: {
            SettingsSectionHeader("Community packs", icon: "arrow.down.circle", accent: .core)
        } footer: {
            InfoButton.footer(
                footerText,
                title: "Community packs",
                text: "The community packs are downloaded from the Cemu graphic packs project's latest release on GitHub. The download is verified (size and a checksum on every file) before it replaces anything, and it needs free space for the zip and the extracted copy. If it fails or is cancelled, the packs you already have stay as they were.\n\nEspressU checks for a newer release when you open this screen, at most once a day, or when you tap Check for updates. Pack choices (which packs are on and their options) are kept across updates.\n\nThe download keeps going if you leave the app."
            )
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var installedText: String {
        guard let installed = store.installed else { return "Not downloaded" }
        let count = installed.packCount > 0 ? " \u{00B7} \(installed.packCount) packs" : ""
        return installed.name.replacingOccurrences(of: "Cemu Graphic Packs: ", with: "") + count
    }

    private var footerText: String {
        var parts: [String] = []
        if let checked = store.lastChecked {
            parts.append("Last checked \(checked.formatted(date: .abbreviated, time: .shortened)).")
        }
        if let free = GraphicPackStorage.freeBytes() { parts.append("\(GraphicPackStorage.describe(free)) free on this device.") }
        parts.append("Changes to packs apply the next time you launch a game.")
        return parts.joined(separator: " ")
    }

    @ViewBuilder private var progressOrActions: some View {
        switch store.phase {
        case .checking:
            HStack(spacing: 10) {
                ProgressView()
                Text("Checking for updates\u{2026}")
                    .font(.system(size: 13, design: .rounded)).foregroundColor(EspressUTheme.secondaryText)
            }
        case .downloading(let fraction):
            progressRow(title: "Downloading", fraction: fraction)
        case .installing(let fraction):
            progressRow(title: "Installing", fraction: fraction)
        case .waitingForGameToClose:
            ScreenStatusCallout(tone: .info, message: "Downloaded. It installs when this screen is opened with no game running.")
        case .idle:
            actionButtons
        }
    }

    private func progressRow(title: String, fraction: Double) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("\(title)\u{2026}")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text("\(Int((fraction * 100).rounded()))%")
                    .font(.system(size: 13, design: .monospaced)).foregroundColor(EspressUTheme.secondaryText)
            }
            ProgressView(value: fraction).tint(EspressUTheme.accentText)
            Button(role: .cancel) { store.cancel() } label: {
                Text("Cancel").font(.system(size: 13, weight: .semibold, design: .rounded))
            }
            .buttonStyle(.borderless)
        }
        .padding(.vertical, 2)
    }

    @ViewBuilder private var actionButtons: some View {
        if store.updateAvailable && store.latest != nil {
            Button {
                store.startDownload()
            } label: {
                Label(store.installed == nil ? "Download community packs" : "Update community packs",
                      systemImage: "arrow.down.circle.fill")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .disabled(store.gameRunning)
        }
        Button {
            store.checkForUpdate(force: true)
        } label: {
            Label(store.latest == nil ? "Check for community packs" : "Check for updates", systemImage: "arrow.clockwise")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
    }
}

struct GraphicPackGameListSection: View {
    let packs: [GraphicPack]
    @Binding var search: String

    private struct GameGroup: Identifiable {
        let game: String
        let packs: [GraphicPack]
        var id: String { game }
    }

    private var groups: [GameGroup] {
        let grouped = Dictionary(grouping: packs, by: \.game)
        let needle = search.trimmingCharacters(in: .whitespaces).lowercased()
        return grouped
            .filter { entry in
                needle.isEmpty || entry.key.lowercased().contains(needle)
                    || entry.value.contains { $0.name.lowercased().contains(needle) }
            }
            .map { GameGroup(game: $0.key, packs: $0.value) }
            .sorted { a, b in
                if a.game == GraphicPack.allGames { return true }
                if b.game == GraphicPack.allGames { return false }
                return a.game.localizedCaseInsensitiveCompare(b.game) == .orderedAscending
            }
    }

    var body: some View {
        Section {
            ForEach(groups) { group in
                NavigationLink {
                    GraphicPackGameView(game: group.game, packs: group.packs)
                } label: {
                    HStack(spacing: 8) {
                        Text(group.game)
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                            .fixedSize(horizontal: false, vertical: true)
                        Spacer(minLength: 6)
                        let on = group.packs.filter(\.enabled).count
                        if on > 0 { ScreenChip(text: "\(on) on", isMuted: false) }
                        ScreenChip(text: "\(group.packs.count)")
                    }
                }
            }
        } header: {
            SettingsSectionHeader("All games", icon: "square.grid.2x2", accent: .content)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct GraphicPackGameListView: View {
    @ObservedObject private var store = GraphicPackStore.shared
    @State private var search = ""

    var body: some View {
        List {
            if store.packs.isEmpty {
                Section {
                    ScreenEmptyState(systemImage: "square.stack.3d.up", headline: "No graphic packs yet",
                                     message: "Download the community packs from the Graphic Packs screen.")
                }
                .listRowBackground(Color.clear)
            } else {
                GraphicPackGameListSection(packs: store.packs, search: $search)
            }
        }
        .searchable(text: $search, prompt: "Search games and packs")
        .navigationTitle("All Packs")
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}

struct GraphicPackGameView: View {
    let game: String
    let packs: [GraphicPack]
    @ObservedObject private var store = GraphicPackStore.shared

    private var livePacks: [GraphicPack] {
        let paths = Set(packs.map(\.path))
        return store.packs.filter { paths.contains($0.path) }
    }

    var body: some View {
        List {
            if store.gameRunning {
                Section {
                    ScreenStatusCallout(tone: .info, message: "A game is running. Packs can only be changed after you close it.")
                }
                .listRowBackground(Color.clear)
            }
            GraphicPackCategorySections(packs: livePacks, store: store, profile: .current, suggestWorkarounds: false)
        }
        .navigationTitle(game)
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}

struct GraphicPackCategorySections: View {
    let packs: [GraphicPack]
    @ObservedObject var store: GraphicPackStore
    let profile: GraphicPackDeviceProfile
    let suggestWorkarounds: Bool
    var headerPrefix: String = ""

    @State private var confirming: GraphicPack?
    private var api: RendererAPI {
        RendererAPI(rawValue: UserDefaults.standard.integer(forKey: RendererAPI.storageKey)) ?? RendererAPI.defaultValue
    }

    private var categories: [String] {
        Array(Set(packs.map(\.category)))
            .sorted { a, b in
                let ra = GraphicPack.categoryRank(a), rb = GraphicPack.categoryRank(b)
                return ra != rb ? ra < rb : a.localizedCaseInsensitiveCompare(b) == .orderedAscending
            }
    }

    var body: some View {
        Group {
            ForEach(categories, id: \.self) { category in
                Section {
                    ForEach(packs.filter { $0.category == category }.sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }) { pack in
                        GraphicPackRow(
                            pack: pack,
                            support: pack.support(on: api),
                            api: api,
                            suggested: suggestWorkarounds && pack.isWorkaround && !pack.enabled && pack.support(on: api).isFull,
                            locked: store.gameRunning,
                            onToggle: { wantOn in toggle(pack, wantOn) },
                            detail: AnyView(GraphicPackDetailView(packPath: pack.path, store: store, profile: profile))
                        )
                    }
                } header: {
                    SettingsSectionHeader(headerPrefix + category, icon: Self.icon(for: category), accent: Self.accent(for: category))
                } footer: {
                    if category == categories.last { footerNote }
                }
                .foregroundColor(EspressUTheme.brownDarkest)
            }
        }
        .alert(item: $confirming) { pack in
            Alert(
                title: Text(confirmTitle(for: pack)),
                message: Text(confirmMessage(for: pack)),
                primaryButton: .default(Text("Turn On Anyway")) { store.setEnabled(pack, true) },
                secondaryButton: .cancel()
            )
        }
    }

    private var footerNote: some View {
        InfoButton.footer("Changes apply the next time you launch a game.")
    }

    private func toggle(_ pack: GraphicPack, _ wantOn: Bool) {
        guard wantOn else { store.setEnabled(pack, false); return }
        if pack.support(on: api).isFull {
            store.setEnabled(pack, true)
        } else {
            confirming = pack
        }
    }

    private func confirmTitle(for pack: GraphicPack) -> String {
        if case .inactive = pack.support(on: api) { return "This pack won't activate" }
        return api == .metal ? "Not fully supported on Metal" : "Not fully supported here"
    }

    private func confirmMessage(for pack: GraphicPack) -> String {
        switch pack.support(on: api) {
        case .full: return ""
        case .partial(let why), .inactive(let why): return why
        }
    }

    static func icon(for category: String) -> String {
        switch category {
        case "Graphics": return "paintpalette"
        case "Enhancements": return "wand.and.stars"
        case "Mods": return "puzzlepiece.extension"
        case "Workarounds": return "wrench.and.screwdriver"
        default: return "square.stack.3d.up"
        }
    }

    static func accent(for category: String) -> SettingsSectionAccent {
        switch category {
        case "Graphics": return .core
        case "Enhancements": return .io
        case "Mods": return .content
        case "Workarounds": return .system
        default: return .system
        }
    }
}

struct GraphicPackRow: View {
    let pack: GraphicPack
    let support: GraphicPack.Support
    let api: RendererAPI
    let suggested: Bool
    let locked: Bool
    let onToggle: (Bool) -> Void
    let detail: AnyView

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Toggle(isOn: Binding(get: { pack.enabled }, set: onToggle)) {
                Text(pack.displayName)
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                    .fixedSize(horizontal: false, vertical: true)
            }
            .tint(EspressUTheme.accentText)
            .disabled(locked)

            if !pack.brief.isEmpty {
                Text(pack.brief)
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
            }

            chips

            // A push instead of a nested sheet: a sheet opened from inside the game's
            // settings sheet closed the whole settings menu along with it.
            NavigationLink(destination: detail) {
                Text(pack.presetCount > 0 ? "Options and details" : "Details")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(EspressUTheme.accentText)
            }
        }
        .padding(.vertical, 2)
    }

    private var chips: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 96), spacing: 6, alignment: .leading)], alignment: .leading, spacing: 6) {
            if suggested { PackChip(text: "Suggested fix", style: .good) }
            switch support {
            case .full: EmptyView()
            case .partial: PackChip(text: api == .metal ? "Shaders not supported on Metal" : "Some shaders skipped", style: .warning)
            case .inactive: PackChip(text: "Won't activate here", style: .warning)
            }
            if pack.canCostSpeed { PackChip(text: "Can cost speed", style: .neutral) }
            if pack.raisesFrameRate { PackChip(text: "Needs speed to spare", style: .neutral) }
            if pack.isImported { PackChip(text: "Imported", style: .neutral) }
        }
    }
}

struct PackChip: View {
    enum Style { case neutral, good, warning }
    let text: String
    let style: Style

    var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold, design: .rounded))
            .foregroundColor(foreground)
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(Capsule().fill(background))
            .fixedSize(horizontal: false, vertical: true)
    }

    private var foreground: Color {
        switch style {
        case .neutral: return EspressUTheme.brownMid
        case .good: return EspressUTheme.onAccent
        case .warning: return EspressUTheme.brownDarkest
        }
    }

    private var background: Color {
        switch style {
        case .neutral: return EspressUTheme.wrapper
        case .good: return EspressUTheme.pixelBlue
        case .warning: return EspressUTheme.blushPink.opacity(0.45)
        }
    }
}

struct GraphicPackDetailView: View {
    let packPath: String
    @ObservedObject var store: GraphicPackStore
    let profile: GraphicPackDeviceProfile
    var onDone: (() -> Void)? = nil

    @Environment(\.dismiss) private var dismiss
    @State private var details: GraphicPackDetails?
    @State private var confirmingRemove = false

    private var pack: GraphicPack? { store.packs.first { $0.path == packPath } }
    private var api: RendererAPI {
        RendererAPI(rawValue: UserDefaults.standard.integer(forKey: RendererAPI.storageKey)) ?? RendererAPI.defaultValue
    }

    var body: some View {
        ZStack {
            EspressUTheme.backgroundGradient.ignoresSafeArea()
            content
        }
        .navigationTitle(pack?.displayName ?? "Pack")
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .onAppear(perform: load)
    }

    @ViewBuilder private var content: some View {
        if let pack {
            Form {
                Section {
                    Toggle(isOn: Binding(get: { pack.enabled }, set: { wantOn in
                        store.setEnabled(pack, wantOn)
                    })) {
                        Text("Use this pack")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                    }
                    .tint(EspressUTheme.accentText)
                    .disabled(store.gameRunning)

                    switch pack.support(on: api) {
                    case .full: EmptyView()
                    case .partial(let why): ScreenStatusCallout(tone: .warning, message: why)
                    case .inactive(let why): ScreenStatusCallout(tone: .warning, message: why)
                    }
                    if store.gameRunning {
                        ScreenStatusCallout(tone: .info, message: "A game is running. Close it to change this pack.")
                    }
                } header: {
                    SettingsSectionHeader(pack.game == GraphicPack.allGames ? "Every game" : pack.game, icon: GraphicPackCategorySections.icon(for: pack.category), accent: GraphicPackCategorySections.accent(for: pack.category))
                } footer: {
                    InfoButton.footer("Changes apply the next time you launch a game.")
                }
                .foregroundColor(EspressUTheme.brownDarkest)

                if let text = details?.description, !text.isEmpty {
                    Section {
                        Text(text)
                            .font(.system(size: 14))
                            .foregroundColor(EspressUTheme.brownDarkest)
                            .fixedSize(horizontal: false, vertical: true)
                    } header: {
                        SettingsSectionHeader("About this pack", icon: "text.alignleft", accent: .system)
                    }
                }

                if let details {
                    ForEach(details.categories, id: \.self) { category in
                        optionSection(pack: pack, info: details, category: category)
                    }
                    if details.categories.contains(where: { details.isResolutionCategory($0) }) {
                        Section {
                            Text(profile.summary)
                                .font(.system(size: 13))
                                .foregroundColor(EspressUTheme.brownMid)
                                .fixedSize(horizontal: false, vertical: true)
                        } header: {
                            SettingsSectionHeader("Resolution on this device", icon: "ipad.and.iphone", accent: .core)
                        }
                    }
                }

                Section {
                    Button {
                        store.reset(pack) { details = $0 }
                    } label: {
                        Label("Reset to the pack's defaults", systemImage: "arrow.uturn.backward")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                    }
                    .disabled(store.gameRunning)

                    if pack.isImported {
                        Button(role: .destructive) {
                            confirmingRemove = true
                        } label: {
                            DestructiveSettingsLabel(title: "Remove imported pack", systemImage: "trash")
                        }
                        .disabled(store.gameRunning)
                    }
                }
                .foregroundColor(EspressUTheme.brownDarkest)
            }
            .confirmationDialog("Remove this imported pack?", isPresented: $confirmingRemove, titleVisibility: .visible) {
                Button("Remove", role: .destructive) {
                    store.removeImported(pack)
                    onDone?()
                    dismiss()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Its folder is deleted from EspressU's graphicPacks/imported folder.")
            }
        } else {
            ScreenEmptyState(systemImage: "questionmark.folder", headline: "Pack not found",
                             message: "It was removed or the packs were rescanned.")
        }
    }

    @ViewBuilder private func optionSection(pack: GraphicPack, info: GraphicPackDetails, category: String) -> some View {
        let choices = info.visiblePresets(in: category)
        if choices.count >= 1 {
            let isResolution = info.isResolutionCategory(category)
            let suggestion = isResolution ? profile.suggestedPreset(in: choices) : nil
            let selected = choices.first(where: \.active)?.name ?? ""
            Section {
                Picker(selection: Binding(
                    get: { selected },
                    set: { name in store.setPreset(pack, category: category, preset: name) { details = $0 } }
                ), label: Text(category.isEmpty ? "Option" : category).font(.system(size: 15, weight: .semibold, design: .rounded))) {
                    ForEach(choices, id: \.name) { preset in
                        Text(label(for: preset, isResolution: isResolution, suggestion: suggestion)).tag(preset.name)
                    }
                }
                .pickerStyle(.menu)
                .tint(EspressUTheme.accentText)
                .disabled(store.gameRunning)

                if isResolution, let suggestion, suggestion.name != selected {
                    Button {
                        store.setPreset(pack, category: category, preset: suggestion.name) { details = $0 }
                    } label: {
                        Label("Use the suggested setting (\(suggestion.name))", systemImage: "checkmark.circle")
                            .font(.system(size: 13, weight: .semibold, design: .rounded))
                    }
                    .disabled(store.gameRunning)
                }
            } header: {
                SettingsSectionHeader(category.isEmpty ? "Options" : category, icon: isResolution ? "arrow.up.left.and.arrow.down.right" : "slider.horizontal.3", accent: .core)
            } footer: {
                if isResolution {
                    InfoButton.footer("Higher resolutions cost speed and memory.")
                }
            }
            .foregroundColor(EspressUTheme.brownDarkest)
        }
    }

    private func label(for preset: GraphicPackPreset, isResolution: Bool, suggestion: GraphicPackPreset?) -> String {
        guard isResolution else { return preset.name }
        var text = preset.name
        if let height = preset.height, height > profile.suggestedMaxHeight { text += "  \u{2022} heavy here" }
        if preset == suggestion { text += "  \u{2022} suggested" }
        return text
    }

    private func load() {
        guard let pack else { return }
        store.details(for: pack) { details = $0 }
    }
}
