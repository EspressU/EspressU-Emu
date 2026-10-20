// SPDX-License-Identifier: MPL-2.0

import SwiftUI

struct EspressUGamePage: View {
    let game: GameMetadata
    @ObservedObject var store: PerGameSettingsStore
    @ObservedObject var gameManager: GameManager
    let onPlay: () -> Void
    let onDecrypt: () -> Void
    let onImportDLC: () -> Void
    let onImportUpdate: () -> Void
    let onRemoveDLC: () -> Void
    let onRemoveUpdate: () -> Void
    let onRemoveGame: () -> Void

    @ObservedObject private var stats = LibraryPlayStats.shared
    @ObservedObject private var data = GameDataStore.shared
    @ObservedObject private var names = LibraryCustomNames.shared
    @StateObject private var input = ControllerMenuInput()
    @Environment(\.dismiss) private var dismiss

    private enum PageSheet: String, Identifiable {
        case cover, rename, packs, match
        var id: String { rawValue }
    }

    @State private var sheet: PageSheet?
    @State private var focus: String? = "play"
    @State private var armed: String?
    @State private var dropdown: SettingsDropdownState?
    @State private var isWide = true
    @State private var sizeOnDisk: Int64?
    @State private var shaderBytes: Int64?
    @State private var showsWholeSummary = false
    @State private var openSections: Set<String> = []
    @StateObject private var files = GameFilesController()
    @State private var lastRowFocus: String?
    @State private var lastHeroFocus = "play"

    private var live: GameMetadata { gameManager.games.first { $0.id == game.id } ?? game }
    private var info: GameInfo? { data.info(for: game.id) }
    private var key: String { live.settingsKey }

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width >= 700
            ZStack {
                EspressUBackground()
                    .ignoresSafeArea()
                if wide {
                    HStack(alignment: .top, spacing: 36) {
                        ScrollView(showsIndicators: false) {
                            hero(coverSide: min(320, geo.size.width * 0.3))
                                .padding(.top, 34)
                                .padding(.bottom, 24)
                        }
                        .frame(width: min(360, geo.size.width * 0.34))
                        cards(bottomRoom: 32)
                    }
                    .padding(.horizontal, 40)
                } else {
                    cards(bottomRoom: 28, header: AnyView(
                        hero(coverSide: min(geo.size.width - 120, 280))
                            .padding(.top, 54)
                            .padding(.bottom, 6)))
                        .padding(.horizontal, 16)
                }
            }
            .overlay(alignment: .topTrailing) {
                EspressUCloseButton(showsHint: input.showsFocus, hintSymbol: input.backSymbol, action: close)
                    .padding(.top, wide ? 22 : 12)
                    .padding(.trailing, wide ? 28 : 16)
            }
            .overlay { SettingsDropdownOverlay(state: $dropdown, groups: groups, showsFocus: input.showsFocus) }
            .animation(.easeOut(duration: 0.15), value: dropdown)
            .animation(.easeOut(duration: 0.2), value: input.showsFocus)
            .onAppear { isWide = wide }
            .onChange(of: wide) { isWide = $0 }
        }
        .onAppear(perform: appear)
        .onDisappear { input.stop() }
        .sheet(item: $sheet, onDismiss: { input.resume() }) { which in
            switch which {
            case .cover:
                EspressUCoverPicker(game: live, gameManager: gameManager)
            case .rename:
                EspressURenameView(game: live)
            case .packs:
                EspressUGamePacksView(game: live)
            case .match:
                GameMatchPickerView(game: live, gameManager: gameManager)
            }
        }
    }

    private func appear() {
        input.onCommand = handle
        input.isExclusive = true
        input.start()
        loadSizes()
        cemu_bridge_log_line("Game page: opened")
    }

    private func close() {
        input.stop()
        dismiss()
    }

    private func closeThen(_ action: @escaping () -> Void) {
        close()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { action() }
    }

    private func open(_ next: PageSheet) {
        if next == .cover || next == .rename || next == .packs { input.pause() }
        sheet = next
    }

    private func hero(coverSide: CGFloat) -> some View {
        VStack(spacing: 18) {
            EspressUCover(game: live, radius: 30)
                .frame(width: coverSide, height: coverSide)
                .shadow(color: EspressUTheme.shadow.opacity(0.35), radius: 22, y: 10)
                .accessibilityHidden(true)

            VStack(spacing: 6) {
                Text(live.cardName.name)
                    .font(.system(size: isWide ? 28 : 25, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                if let by = info?.publisher ?? info?.developer {
                    Text(by)
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                }
                chips
            }

            VStack(spacing: 12) {
                Button { closeThen(onPlay) } label: {
                    HStack(spacing: 8) {
                        Image(systemName: "play.fill")
                        Text("Play")
                    }
                    .font(.system(size: 19, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.onCremaTop)
                    .frame(maxWidth: 300, minHeight: 52)
                    .background(EspressUTheme.cremaTopGradient, in: Capsule())
                }
                .buttonStyle(.plain)
                .espressUFocusRing(input.showsFocus && focus == "play")
                .accessibilityLabel("Play \(live.cardName.name)")

                Button { gameManager.toggleFavorite(live) } label: {
                    HStack(spacing: 8) {
                        Image(systemName: live.isFavorite ? "heart.fill" : "heart")
                            .foregroundColor(live.isFavorite ? EspressUTheme.alertText : EspressUTheme.brownDark)
                        Text(live.isFavorite ? "Favourite" : "Add to favourites")
                            .foregroundColor(EspressUTheme.brownDarkest)
                    }
                    .font(.system(size: 16, weight: .bold, design: .rounded))
                    .frame(maxWidth: 300, minHeight: 46)
                    .espressUSurface(Capsule())
                }
                .buttonStyle(.plain)
                .espressUFocusRing(input.showsFocus && focus == "favourite")
            }
        }
        .frame(maxWidth: .infinity)
    }

    private var chips: some View {
        var items: [(String, String)] = []
        items.append(("globe", live.region ?? "Region unknown"))
        if let players = info?.localPlayers { items.append(("person.2.fill", players == 1 ? "1 player" : "1–\(players) players")) }
        if let genre = info?.genres.first { items.append(("tag.fill", genre.capitalized)) }
        return HStack(spacing: 8) {
            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                Label(item.1, systemImage: item.0)
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .lineLimit(1)
                    .padding(.horizontal, 11)
                    .padding(.vertical, 6)
                    .background(EspressUTheme.wrapper, in: Capsule())
            }
        }
        .padding(.top, 4)
    }

    private func cards(bottomRoom: CGFloat, header: AnyView? = nil) -> some View {
        ScrollViewReader { proxy in
            ScrollView(showsIndicators: false) {
                VStack(alignment: .leading, spacing: 18) {
                    if let header {
                        header.id("play")
                    }
                    statsCard
                    if info?.bestSynopsis != nil || info == nil {
                        aboutCard
                    }
                    EspressURowList(groups: groups, focus: $focus, armed: $armed,
                                    showsFocus: input.showsFocus, stacked: !isWide, dropdown: $dropdown)
                }
                .padding(.top, isWide ? 34 : 0)
                .padding(.bottom, bottomRoom)
                .frame(maxWidth: 760)
                .frame(maxWidth: .infinity)
            }
            .onChange(of: focus) { id in
                guard let id else { return }
                withAnimation(.easeOut(duration: 0.2)) {
                    if id == "play" || id == "favourite" {
                        proxy.scrollTo("play", anchor: .top)
                    } else {
                        proxy.scrollTo(id, anchor: .center)
                    }
                }
            }
        }
    }

    private var statsCard: some View {
        var tiles: [(String, String, String)] = [
            ("clock", "Last played", lastPlayedText),
            ("number", "Times played", "\(stats.entry(for: game.id)?.count ?? 0)"),
            ("internaldrive", "Size on disk", sizeOnDisk.map { ByteCountFormatter.string(fromByteCount: $0, countStyle: .file) } ?? "…"),
        ]
        if live.titleId != nil {
            tiles.append(("square.stack.3d.up", "Shader cache", shaderBytes.map { ShaderCacheInfo.formatBytes($0) } ?? "…"))
        }
        return LazyVGrid(columns: [GridItem(.adaptive(minimum: 130), spacing: 10, alignment: .top)], alignment: .leading, spacing: 10) {
            ForEach(Array(tiles.enumerated()), id: \.offset) { _, stat in
                VStack(alignment: .leading, spacing: 4) {
                    Label(stat.1, systemImage: stat.0)
                        .font(.system(size: 12, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .lineLimit(1)
                    Text(stat.2)
                        .font(.system(size: 17, weight: .heavy, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 12)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
            }
        }
    }

    private var aboutCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("ABOUT")
                .font(.system(size: 13, weight: .heavy, design: .rounded))
                .tracking(0.6)
                .foregroundColor(EspressUTheme.brownMid)
            if let text = info?.bestSynopsis?.text {
                Text(text)
                    .font(.system(size: 16, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .lineLimit(showsWholeSummary ? nil : 4)
                    .fixedSize(horizontal: false, vertical: true)
                Button(showsWholeSummary ? "Less" : "More") {
                    withAnimation(.easeOut(duration: 0.2)) { showsWholeSummary.toggle() }
                }
                .font(.system(size: 15, weight: .bold, design: .rounded))
                .foregroundColor(EspressUTheme.accentText)
            } else {
                Text("GameTDB has nothing on this game yet. Choose the right entry under Game info to get its summary and details.")
                    .font(.system(size: 15, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
    }

    private var groups: [SettingsGroup] {
        [settingsGroup, filesGroup, contentGroup, infoGroup, manageGroup].filter { !$0.items.isEmpty }
    }

    private func toggleSection(_ id: String) {
        withAnimation(.easeOut(duration: 0.18)) {
            if openSections.contains(id) { openSections.remove(id) } else { openSections.insert(id) }
        }
    }

    private func section(_ id: String, _ title: String, summary: String, items: [SettingsItem]) -> [SettingsItem] {
        let open = openSections.contains(id)
        let header = SettingsItem(id: "section.\(id)", title: title, detail: summary,
                                  control: .action(open ? "Hide" : "Show", destructive: false, confirm: false,
                                                   run: { toggleSection(id) }))
        return [header] + (open ? items : [])
    }

    private func overrideSummary(_ count: Int) -> String {
        count == 0 ? "Follows your settings" : (count == 1 ? "1 set for this game" : "\(count) set for this game")
    }

    private var settingsGroup: SettingsGroup {
        let o = store.overrides(for: key)
        let speedCount = [o.coreMode != nil, o.favourPerformance != nil, o.favourAccuracy != nil, o.fullSpeedRenders != nil,
                          o.fullSpeedShaderMode != nil, o.oneCoreMode != nil, o.preCompileShaders != nil].filter { $0 }.count
        let pictureCount = [o.renderScale != nil, o.upscaleFilter != nil, o.downscaleFilter != nil].filter { $0 }.count
        let screenCount = [o.screenLayout != nil, o.autoHideControls != nil, o.performanceOverlay != nil].filter { $0 }.count
        var items: [SettingsItem] = []
        items += section("speed", "Speed", summary: overrideSummary(speedCount), items: speedGroup.items)
        items += section("picture", "Picture", summary: overrideSummary(pictureCount), items: pictureGroup.items)
        items += section("screen", "Screen and controls", summary: overrideSummary(screenCount), items: screenGroup.items)
        if !o.isIdentity {
            items.append(SettingsItem(id: "set.reset", title: "Use the global settings",
                                      detail: "Removes everything set for this game only.",
                                      control: .action("Reset", destructive: true, confirm: true,
                                                       run: { store.clearOverrides(for: key) })))
        }
        return SettingsGroup(id: "settings", title: "Game settings", items: items)
    }

    private var speedGroup: SettingsGroup {
        let o = store.overrides(for: key)
        let steadyOn = o.fullSpeedRenders ?? store.globalFullSpeedRenders
        var items: [SettingsItem] = [
            choice("set.cores", "CPU cores", nil,
                   current: o.coreMode,
                   options: CoreMode.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                   global: CoreMode.current.title) { value in store.updateOverrides(for: key) { $0.coreMode = value } },
            onOff("set.performance", "Favour performance", "Faster, a little less exact.",
                  current: o.favourPerformance, global: store.globalFavourPerformance) { value in
                store.updateOverrides(for: key) { $0.favourPerformance = value } },
            onOff("set.accuracy", "Favour accuracy", "For games that glitch otherwise; slower.",
                  current: o.favourAccuracy, global: store.globalFavourAccuracy) { value in
                store.updateOverrides(for: key) { $0.favourAccuracy = value } },
            onOff("set.steady", "Steady frame rate", "Shows every frame at the game's own speed.",
                  current: o.fullSpeedRenders, global: store.globalFullSpeedRenders) { value in
                store.updateOverrides(for: key) { $0.fullSpeedRenders = value } },
        ]
        if steadyOn {
            items.append(intChoice("set.steadyShader", "When a new effect appears", nil,
                                   current: o.fullSpeedShaderMode,
                                   options: FullSpeedRenders.ShaderMode.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) },
                                   global: store.globalFullSpeedShaderMode.title) { value in
                store.updateOverrides(for: key) { $0.fullSpeedShaderMode = value } })
        }
        items.append(contentsOf: [
            onOff("set.oneCore", "One-core mode", "One CPU core, whatever CPU cores says.",
                  current: o.oneCoreMode, global: store.globalOneCoreMode) { value in
                store.updateOverrides(for: key) { $0.oneCoreMode = value } },
            onOff("set.shaders", "Compile shaders in the background", "Off for games that break with it on.",
                  current: o.preCompileShaders, global: store.globalPreCompileShaders) { value in
                store.updateOverrides(for: key) { $0.preCompileShaders = value } },
        ])
        return SettingsGroup(id: "speed", title: "Speed", items: items)
    }

    private var pictureGroup: SettingsGroup {
        let o = store.overrides(for: key)
        let filters = ScaleFilter.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }
        let items: [SettingsItem] = [
            choice("set.res", "Resolution", "Sharper pictures cost more speed.",
                   current: o.renderScale,
                   options: RenderScale.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                   global: RenderScale.storedChoice.title) { value in store.updateOverrides(for: key) { $0.renderScale = value } },
            intChoice("set.upscale", "Filter when enlarging", nil, current: o.upscaleFilter, options: filters,
                      global: store.globalUpscaleFilter.title) { value in store.updateOverrides(for: key) { $0.upscaleFilter = value } },
            intChoice("set.downscale", "Filter when shrinking", nil, current: o.downscaleFilter, options: filters,
                      global: store.globalDownscaleFilter.title) { value in store.updateOverrides(for: key) { $0.downscaleFilter = value } },
        ]
        return SettingsGroup(id: "picture", title: "Picture", items: items)
    }

    private var screenGroup: SettingsGroup {
        let o = store.overrides(for: key)
        let items: [SettingsItem] = [
            choice("set.layout", "Screen layout", nil,
                   current: o.screenLayout,
                   options: ScreenLayout.allCases.map { SettingsOption(id: $0.rawValue, title: $0.string) },
                   global: store.globalScreenLayout.string) { value in store.updateOverrides(for: key) { $0.screenLayout = value } },
            onOff("set.autoHide", "Hide touch controls with a controller", nil,
                  current: o.autoHideControls, global: store.globalAutoHideControls) { value in
                store.updateOverrides(for: key) { $0.autoHideControls = value } },
            onOff("set.overlay", "Performance overlay", nil,
                  current: o.performanceOverlay, global: store.globalOverlayPosition != .disabled) { value in
                store.updateOverrides(for: key) { $0.performanceOverlay = value } },
        ]
        return SettingsGroup(id: "screen", title: "Screen and controls", items: items)
    }

    private var filesGroup: SettingsGroup {
        let packsOn = live.titleId == nil ? 0
            : GraphicPackStore.shared.packs.filter { $0.enabled && ($0.universal || $0.appliesTo(titleId: live.titleId)) && !$0.universal }.count
        let busy = files.busy
        var items: [SettingsItem] = [
            SettingsItem(id: "files.packs", title: "Graphic packs",
                         detail: packsOn > 0 ? "\(packsOn) on for this game." : "Resolution, frame rate, fixes and mods.",
                         control: .action("Open", destructive: false, confirm: false, run: { open(.packs) })),
            SettingsItem(id: "files.exportSave", title: "Export game saves",
                         detail: "Writes the save in the Wii U's own format.",
                         control: .action("Export", destructive: false, confirm: false, run: { files.exportSave(for: live) }),
                         enabled: GameSaveTransfer.hasSave(for: live)),
            SettingsItem(id: "files.importSave", title: "Import a game save folder",
                         detail: "Replaces the current save. The old one is backed up first. Close the game before you do this.",
                         control: .action("Import", destructive: true, confirm: true, run: { files.importSave(for: live) })),
            SettingsItem(id: "files.shaders", title: "Import learned shaders",
                         detail: "From desktop Cemu or another EspressU. Adds to this game's shaders, never replaces them.",
                         control: .action("Import", destructive: false, confirm: false,
                                          run: { files.importCache(.shaders, for: live, library: gameManager.games) }),
                         enabled: !busy),
            SettingsItem(id: "files.pipeline", title: "Import shader pipeline caches",
                         detail: "A Vulkan cache only helps the Vulkan renderer and a Metal one only Metal. It is kept either way.",
                         control: .action("Import", destructive: false, confirm: false,
                                          run: { files.importCache(.pipeline, for: live, library: gameManager.games) }),
                         enabled: !busy),
        ]
        if files.pending != nil {
            items.append(SettingsItem(id: "files.pendingVulkan", title: "Which renderer made this pipeline cache?",
                                      detail: "The file name doesn't say. Desktop Cemu's are ..._vkpipeline.bin, EspressU's ..._mtlpipeline.bin.",
                                      control: .action("Vulkan", destructive: false, confirm: false,
                                                       run: { files.answerPending(vulkan: true, for: live, library: gameManager.games) })))
            items.append(SettingsItem(id: "files.pendingMetal", title: "Made by EspressU (Metal)",
                                      control: .action("Metal", destructive: false, confirm: false,
                                                       run: { files.answerPending(vulkan: false, for: live, library: gameManager.games) })))
            items.append(SettingsItem(id: "files.pendingCancel", title: "Cancel the import",
                                      control: .action("Cancel", destructive: true, confirm: false, run: { files.discardPending() })))
        }
        if busy {
            items.append(SettingsItem(id: "files.busy", title: "Importing…", control: .info("", nil)))
        }
        if let message = files.message {
            items.append(SettingsItem(id: "files.result", title: files.failed ? "That didn't work" : "Done",
                                      detail: message, control: .info("", nil)))
        }
        return SettingsGroup(id: "files", title: "Packs, saves and caches", items: items)
    }

    private func choice(_ id: String, _ title: String, _ detail: String?, current: String?,
                        options: [SettingsOption], global: String,
                        set: @escaping (String?) -> Void) -> SettingsItem {
        let all = [SettingsOption(id: "default", title: "Default (\(global))")] + options
        let binding = Binding<String>(
            get: { current.flatMap { value in options.contains { $0.id == value } ? value : nil } ?? "default" },
            set: { set($0 == "default" ? nil : $0) })
        return SettingsItem(id: id, title: title, detail: detail, control: .choice(all, binding))
    }

    private func intChoice(_ id: String, _ title: String, _ detail: String?, current: Int?,
                           options: [SettingsOption], global: String,
                           set: @escaping (Int?) -> Void) -> SettingsItem {
        choice(id, title, detail, current: current.map(String.init), options: options, global: global) { value in
            set(value.flatMap(Int.init))
        }
    }

    private func onOff(_ id: String, _ title: String, _ detail: String?, current: Bool?, global: Bool,
                       set: @escaping (Bool?) -> Void) -> SettingsItem {
        let options = [SettingsOption(id: "on", title: "On"), SettingsOption(id: "off", title: "Off")]
        return choice(id, title, detail, current: current.map { $0 ? "on" : "off" }, options: options,
                      global: global ? "On" : "Off") { value in set(value.map { $0 == "on" }) }
    }

    private var lastPlayedText: String {
        guard let entry = stats.entry(for: game.id) else { return "Not yet" }
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .full
        return formatter.localizedString(for: entry.last, relativeTo: Date())
    }

    private var contentGroup: SettingsGroup {
        let installed = DlcUpdateImport.installedContent(for: live)
        var items: [SettingsItem] = [
            SettingsItem(id: "content.update", title: "Update",
                         detail: installed.hasUpdate ? nil : "Add one from Add on the home screen.",
                         control: .info(installed.hasUpdate ? "Installed" : "Not installed", nil)),
            SettingsItem(id: "content.dlc", title: "DLC",
                         detail: installed.hasDLC ? nil : "Add one from Add on the home screen.",
                         control: .info(installed.hasDLC ? "Installed" : "Not installed", nil))
        ]
        if installed.hasUpdate {
            items.append(SettingsItem(id: "content.removeUpdate", title: "Remove the update",
                                      control: .action("Remove", destructive: true, confirm: false, run: { closeThen(onRemoveUpdate) })))
        }
        if installed.hasDLC {
            items.append(SettingsItem(id: "content.removeDLC", title: "Remove the DLC",
                                      control: .action("Remove", destructive: true, confirm: false, run: { closeThen(onRemoveDLC) })))
        }
        if gameSupportsDecryptToFiles(romPath: live.romPath) {
            items.append(SettingsItem(id: "content.decrypt", title: "Decrypt", detail: "Writes a decrypted copy to Files.",
                                      control: .action("Start", destructive: false, confirm: false, run: { closeThen(onDecrypt) })))
        }
        return SettingsGroup(id: "content", title: "Updates and DLC", items: items)
    }

    private var infoGroup: SettingsGroup {
        var items: [SettingsItem] = []
        if let info {
            if let p = info.publisher { items.append(SettingsItem(id: "info.publisher", title: "Publisher", control: .info(p, nil))) }
            if let d = info.developer { items.append(SettingsItem(id: "info.developer", title: "Developer", control: .info(d, nil))) }
            if !info.genres.isEmpty {
                items.append(SettingsItem(id: "info.genre", title: "Genre",
                                          control: .info(info.genres.map { $0.capitalized }.joined(separator: ", "), nil)))
            }
            if !info.languages.isEmpty {
                items.append(SettingsItem(id: "info.languages", title: "Languages",
                                          control: .info(info.languages.joined(separator: ", "), nil)))
            }
        }
        items.append(SettingsItem(id: "info.install", title: "Install",
                                  control: .info(live.installLabel ?? live.installKey.replacingOccurrences(of: "install:", with: ""), nil)))
        items.append(SettingsItem(id: "info.match", title: info == nil ? "Find this game on GameTDB" : "Wrong game?",
                                  detail: "Choose the right GameTDB entry for the summary and details.",
                                  control: .action("Choose", destructive: false, confirm: false, run: { open(.match) })))
        let summary = [info?.publisher ?? info?.developer, info?.genres.first?.capitalized].compactMap { $0 }.joined(separator: " · ")
        return SettingsGroup(id: "info", title: "About this game",
                             items: section("info", "Game info", summary: summary.isEmpty ? "Publisher, genre, languages and install" : summary, items: items))
    }

    private var manageGroup: SettingsGroup {
        var items: [SettingsItem] = [
            SettingsItem(id: "manage.cover", title: "Cover", detail: "Photos, Files or GameTDB.",
                         control: .action("Change", destructive: false, confirm: false, run: { open(.cover) }))
        ]
        if gameManager.hasManualCoverOverride(forGameID: game.id) {
            items.append(SettingsItem(id: "manage.removeCover", title: "Use the automatic cover",
                                      control: .action("Remove", destructive: true, confirm: true,
                                                       run: { gameManager.removeManualCover(forGameID: game.id) })))
        }
        items.append(SettingsItem(id: "manage.rename", title: "Name", detail: live.cardName.name,
                                  control: .action("Rename", destructive: false, confirm: false, run: { open(.rename) })))
        if names.name(for: live.installKey) != nil {
            items.append(SettingsItem(id: "manage.resetName", title: "Use the original title",
                                      control: .action("Reset", destructive: false, confirm: false,
                                                       run: { names.set(nil, for: live.installKey) })))
        }
        items.append(SettingsItem(id: "manage.remove", title: live.removeActionTitle,
                                  detail: live.isExternal
                                      ? "Only takes it out of your library. The file stays where it is. Saves and settings are kept."
                                      : "Frees up the space it uses. Saves and settings are kept.",
                                  control: .action("Remove", destructive: true, confirm: true, run: { closeThen(onRemoveGame) })))
        return SettingsGroup(id: "manage", title: "Manage", items: items)
    }

    private func handle(_ command: ControllerMenuCommand) {
        if let sheet {
            if sheet == .match { ControllerScreenCursor.shared.handle(command) }
            return
        }
        if dropdown != nil {
            SettingsDropdown.handle(command, state: $dropdown, groups: groups)
            return
        }
        let current = groups
        let order = ["play", "favourite"] + EspressURowLogic.focusOrder(current)
        switch command {
        case .up, .down:
            focus = EspressURowLogic.step(focus, in: order, by: command == .down ? 1 : -1)
        case .left, .right:
            let onHero = focus == "play" || focus == "favourite"
            if onHero {
                if isWide && command == .right {
                    lastHeroFocus = focus ?? "play"
                    let rows = EspressURowLogic.focusOrder(current)
                    focus = lastRowFocus.flatMap { rows.contains($0) ? $0 : nil } ?? rows.first
                } else if !isWide {
                    focus = command == .left ? "play" : "favourite"
                }
            } else if let item = EspressURowLogic.item(focus, in: current) {
                if command == .left && isWide && !canMoveLeft(item) {
                    lastRowFocus = focus
                    focus = lastHeroFocus
                } else {
                    _ = EspressURowLogic.adjust(item, by: command == .right ? 1 : -1)
                }
            }
        case .confirm:
            switch focus {
            case "play": closeThen(onPlay)
            case "favourite": gameManager.toggleFavorite(live)
            default:
                if let item = EspressURowLogic.item(focus, in: current) {
                    EspressURowLogic.activate(item, armed: $armed, dropdown: $dropdown)
                } else {
                    focus = "play"
                }
            }
        case .alternate, .select:
            gameManager.toggleFavorite(live)
        case .previousPage, .nextPage:
            focus = jumpGroup(from: focus, in: current, forward: command == .nextPage)
        case .back, .menu:
            close()
        }
    }

    private func canMoveLeft(_ item: SettingsItem) -> Bool {
        switch item.control {
        case .choice(let options, let binding):
            return (options.firstIndex { $0.id == binding.wrappedValue } ?? 0) > 0
        case .slider(let binding, let range, _, _):
            return binding.wrappedValue > range.lowerBound
        default:
            return false
        }
    }

    private func jumpGroup(from id: String?, in groups: [SettingsGroup], forward: Bool) -> String? {
        let firsts = groups.compactMap { $0.items.first(where: \.isFocusable)?.id }
        guard !firsts.isEmpty else { return id }
        let groupIndex = groups.firstIndex { group in group.items.contains { $0.id == id } }
        guard let groupIndex else { return forward ? firsts.first : "play" }
        let target = groupIndex + (forward ? 1 : -1)
        if target < 0 { return "play" }
        let candidates = groups.indices.filter { forward ? $0 >= target : $0 <= target }
        let pick = forward ? candidates.first { groups[$0].items.contains(where: \.isFocusable) }
                           : candidates.last { groups[$0].items.contains(where: \.isFocusable) }
        return pick.flatMap { groups[$0].items.first(where: \.isFocusable)?.id } ?? id
    }

    private func loadSizes() {
        let path = live.dumpDirectoryPath ?? live.romPath
        let titleId = live.titleId
        let locationID = live.isUnavailable ? nil : live.externalLocationID
        let isExternal = live.isExternal
        DispatchQueue.global(qos: .utility).async {
            let hold = locationID.flatMap { ExternalLibrary.shared.acquire($0) }
            defer { if let hold { ExternalLibrary.shared.release(hold) } }
            let bytes = (isExternal && hold == nil) ? nil : Self.byteCount(atPath: path)
            var shader: Int64?
            if let titleId {
                var learned: Int64 = 0
                var compiled: Int64 = 0
                _ = cemu_bridge_shader_cache_stats(titleId, &learned, &compiled)
                shader = learned + compiled
            }
            DispatchQueue.main.async {
                sizeOnDisk = bytes
                shaderBytes = shader
            }
        }
    }

    private static func byteCount(atPath path: String) -> Int64 {
        let fm = FileManager.default
        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: path, isDirectory: &isDirectory) else { return 0 }
        if !isDirectory.boolValue {
            return ((try? fm.attributesOfItem(atPath: path))?[.size] as? NSNumber)?.int64Value ?? 0
        }
        let keys: [URLResourceKey] = [.totalFileAllocatedSizeKey, .fileSizeKey]
        guard let walker = fm.enumerator(at: URL(fileURLWithPath: path), includingPropertiesForKeys: keys) else { return 0 }
        var total: Int64 = 0
        for case let url as URL in walker {
            let values = try? url.resourceValues(forKeys: Set(keys))
            total += Int64(values?.totalFileAllocatedSize ?? values?.fileSize ?? 0)
        }
        return total
    }
}
