// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct GameOverrides: Codable, Equatable {
    var preCompileShaders: Bool?

    var favourAccuracy: Bool?

    var coreMode: String?

    var renderScale: String?
    var upscaleFilter: Int?
    var downscaleFilter: Int?
    var favourPerformance: Bool?
    var fullSpeedRenders: Bool?
    var fullSpeedShaderMode: Int?
    var oneCoreMode: Bool?
    var screenLayout: String?
    var autoHideControls: Bool?
    var performanceOverlay: Bool?

    static let identity = GameOverrides()
    var isIdentity: Bool { self == GameOverrides.identity }
}

final class PerGameSettingsStore: ObservableObject {
    static let shared = PerGameSettingsStore()
    static let storageKey = "espressu.perGame.overrides"

    @Published private(set) var overridesByGame: [String: GameOverrides]
    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let data = defaults.data(forKey: Self.storageKey),
           let decoded = try? JSONDecoder().decode([String: GameOverrides].self, from: data) {
            overridesByGame = decoded
        } else {
            overridesByGame = [:]
        }
    }

    func overrides(for gameID: String) -> GameOverrides {
        overridesByGame[gameID] ?? .identity
    }

    func effectivePreCompileShaders(for gameID: String) -> Bool {
        overrides(for: gameID).preCompileShaders ?? globalPreCompileShaders
    }

    var globalPreCompileShaders: Bool {
        defaults.object(forKey: "espressu.shaders.asyncCompile") as? Bool ?? true
    }

    var globalFavourAccuracy: Bool {
        defaults.object(forKey: "espressu.cpu.favourAccuracy") as? Bool ?? false
    }

    func setPreCompileShaders(_ value: Bool?, for gameID: String) {
        var next = overrides(for: gameID)
        next.preCompileShaders = value
        write(next, for: gameID)
    }

    func effectiveFavourAccuracy(for gameID: String) -> Bool {
        activeOverrides(for: gameID).favourAccuracy ?? globalFavourAccuracy
    }

    func setFavourAccuracy(_ value: Bool?, for gameID: String) {
        var next = overrides(for: gameID)
        next.favourAccuracy = value
        write(next, for: gameID)
    }

    func effectiveCoreMode(for gameID: String) -> CoreMode {
        if let raw = activeOverrides(for: gameID).coreMode, let mode = CoreMode(rawValue: raw) {
            return mode
        }
        return CoreMode.current
    }

    func setCoreMode(_ value: CoreMode?, for gameID: String) {
        var next = overrides(for: gameID)
        next.coreMode = value?.rawValue
        write(next, for: gameID)
    }

    func adoptTitleIDKey(for game: GameMetadata) {
        let key = game.settingsKey
        guard key != game.id, let legacy = overridesByGame[game.id], overridesByGame[key] == nil else { return }
        overridesByGame[key] = legacy
        overridesByGame.removeValue(forKey: game.id)
        persist()
    }

    func replaceOverrides(_ next: [String: GameOverrides]) {
        overridesByGame = next.filter { !$0.value.isIdentity }
        persist()
    }

    func setOverrides(_ value: GameOverrides, for gameID: String) {
        write(value, for: gameID)
    }

    func clearOverrides(for gameID: String) {
        write(.identity, for: gameID)
    }

    func removeAllOverrides() {
        overridesByGame = [:]
        persist()
    }

    private func write(_ value: GameOverrides, for gameID: String) {
        if value.isIdentity {
            overridesByGame.removeValue(forKey: gameID)
        } else {
            overridesByGame[gameID] = value
        }
        persist()
    }

    private func persist() {
        guard let data = try? JSONEncoder().encode(overridesByGame) else { return }
        defaults.set(data, forKey: Self.storageKey)
    }
}

enum PerGameKeyMigration {
    @MainActor static func run(games: [GameMetadata]) {
        for game in games where game.titleId != nil && game.settingsKey != game.id {
            PerGameSettingsStore.shared.adoptTitleIDKey(for: game)
            AutoCoreHistory.adoptTitleIDKey(from: game.id, to: game.settingsKey)
        }
    }
}

struct GameContextMenu: View {
    let game: GameMetadata
    @ObservedObject var gameManager: GameManager
    let onPlay: () -> Void
    let onOpenPage: () -> Void
    let onChangeCoverArt: () -> Void
    let onRename: () -> Void

    var body: some View {
        if let installLabel = game.installLabel {
            Text("\(game.cardName.name) \u{2022} \(installLabel)")
            Divider()
        }
        Button(action: onPlay) {
            Label("Play", systemImage: "play.fill")
        }
        Button(action: onOpenPage) {
            Label("Game page", systemImage: "info.circle")
        }
        Button {
            gameManager.toggleFavorite(game)
        } label: {
            Label(game.isFavorite ? "Unfavourite" : "Favourite",
                  systemImage: game.isFavorite ? "heart.slash" : "heart")
        }
        Button(action: onRename) {
            Label("Rename\u{2026}", systemImage: "pencil")
        }
        Button(action: onChangeCoverArt) {
            Label("Change cover\u{2026}", systemImage: "photo")
        }
    }
}

private struct OverridePickerRow<Selection: Hashable, Options: View>: View {
    let title: String
    let caption: String
    let selection: Binding<Selection>
    let isDisabled: Bool
    let options: Options

    init(title: String, caption: String, selection: Binding<Selection>, isDisabled: Bool = false,
         @ViewBuilder options: () -> Options) {
        self.title = title
        self.caption = caption
        self.selection = selection
        self.isDisabled = isDisabled
        self.options = options()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Picker(selection: selection) {
                options
            } label: {
                Text(title)
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .pickerStyle(.menu)
            .tint(EspressUTheme.accentText)
            .disabled(isDisabled)
            Text(caption)
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

struct GameOptionsView: View {
    let game: GameMetadata
    @ObservedObject var store: PerGameSettingsStore
    var libraryGames: [GameMetadata] = []
    var showsOverrides = true
    @Environment(\.dismiss) private var dismiss
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    @State private var saveTransferMessage: String?
    @State private var saveTransferFailed = false
    @State private var showingImportConfirmation = false

    @State private var cacheImportMessage: String?
    @State private var cacheImportFailed = false
    @State private var cacheImportBusy = false
    @State private var pendingPipeline: PendingPipeline?

    private struct PendingPipeline: Identifiable {
        let id = UUID()
        let staged: URL
        let name: String
    }

    private enum TriState: String, CaseIterable, Identifiable {
        case useGlobalDefault, on, off
        var id: String { rawValue }
        var title: String {
            switch self {
            case .useGlobalDefault: return "Use Global Default"
            case .on: return "On"
            case .off: return "Off"
            }
        }
    }

    private func binding(for keyPath: WritableKeyPath<GameOverrides, Bool?>,
                         set setter: @escaping (Bool?) -> Void) -> Binding<TriState> {
        Binding(
            get: {
                switch store.overrides(for: game.settingsKey)[keyPath: keyPath] {
                case .none: return .useGlobalDefault
                case .some(true): return .on
                case .some(false): return .off
                }
            },
            set: { choice in
                switch choice {
                case .useGlobalDefault: setter(nil)
                case .on: setter(true)
                case .off: setter(false)
                }
            })
    }

    private var shaderChoice: Binding<TriState> {
        binding(for: \.preCompileShaders) { store.setPreCompileShaders($0, for: game.settingsKey) }
    }

    private var favourAccuracyChoice: Binding<TriState> {
        binding(for: \.favourAccuracy) { store.setFavourAccuracy($0, for: game.settingsKey) }
    }

    private var coreModeChoice: Binding<String> {
        Binding(
            get: {
                guard let raw = store.overrides(for: game.settingsKey).coreMode, CoreMode(rawValue: raw) != nil else { return "" }
                return raw
            },
            set: { store.setCoreMode(CoreMode(rawValue: $0), for: game.settingsKey) })
    }

    private func caption(pinned: Bool, settingsValue: String) -> String {
        pinned
            ? "Set for this game only. Settings has it \(settingsValue)."
            : "Follows Settings, which has it \(settingsValue)."
    }

    private var shaderCaption: String {
        caption(pinned: store.overrides(for: game.settingsKey).preCompileShaders != nil,
                settingsValue: store.globalPreCompileShaders ? "on" : "off")
    }

    private var favourAccuracyCaption: String {
        caption(pinned: store.overrides(for: game.settingsKey).favourAccuracy != nil,
                settingsValue: store.globalFavourAccuracy ? "on" : "off")
    }

    private var coreModeCaption: String {
        guard DeviceCapabilities.current.multicoreViable else { return DeviceCapabilities.oneCoreOnlyText }
        let base = caption(pinned: store.overrides(for: game.settingsKey).coreMode != nil,
                           settingsValue: "set to \(CoreMode.current.title)")
        guard store.effectiveCoreMode(for: game.settingsKey) != .single else { return base }
        if store.effectiveFavourAccuracy(for: game.settingsKey) {
            return base + " Favour accuracy is on for this game, so it runs on one core whatever this says."
        }
        if OneCoreMode.isEnabled {
            return base + " One-core mode is on in Settings, so games run on one core whatever this says."
        }
        return base
    }

    private var overridesSection: some View {
        Section {
            OverridePickerRow(title: "Compile shaders in the background", caption: shaderCaption, selection: shaderChoice) {
                ForEach(TriState.allCases) { choice in
                    Text(choice.title).tag(choice)
                }
            }
            if SettingsMode.isAdvanced(raw: settingsModeRaw) {
                OverridePickerRow(title: "Favour accuracy", caption: favourAccuracyCaption, selection: favourAccuracyChoice) {
                    ForEach(TriState.allCases) { choice in
                        Text(choice.title).tag(choice)
                    }
                }
                OverridePickerRow(title: "CPU cores", caption: coreModeCaption, selection: coreModeChoice,
                                  isDisabled: !DeviceCapabilities.current.multicoreViable) {
                    Text("Use Global Default").tag("")
                    ForEach(CoreMode.allCases) { mode in
                        Text(mode.title).tag(mode.rawValue)
                    }
                }
            }
            if !store.overrides(for: game.settingsKey).isIdentity {
                Button {
                    store.clearOverrides(for: game.settingsKey)
                } label: {
                    Label("Use Global Defaults for All", systemImage: "arrow.uturn.backward")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                }
            }
        } header: {
            SettingsSectionHeader("Overrides", icon: "slider.horizontal.3", accent: .core)
        } footer: {
            InfoButton.footer(
                "\"Use Global Default\" follows Settings; On or Off sets this game only. Changes apply the next time you start the game.",
                title: "Overrides",
                text: "Compile shaders in the background builds shaders while the game keeps running. Most games want this on; Nano Assault Neo breaks with it, so it can be set per game.\n\nFavour accuracy is slower but more accurate, and can fix a game that glitches or crashes. It also keeps the game on one CPU core. See Settings > CPU.\n\nCPU cores picks how many cores run the game. Auto decides for each game and device.\n\n\"Use Global Default\" follows the matching setting in Settings, even if you change it later. On or Off sets this game only.\n\nChanges apply the next time you start the game."
            )
        }
    }

    private var graphicPacksSection: some View {
        Section {
            NavigationLink {
                GraphicPacksView(game: game)
            } label: {
                Label("Graphic Packs", systemImage: "wand.and.stars")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
        } header: {
            SettingsSectionHeader("Graphic packs", icon: "paintpalette", accent: .core)
        } footer: {
            InfoButton.footer(
                "Resolution, frame rate, fixes and mods for this game. They apply the next time you start it.",
                title: "Graphic packs",
                text: "Graphic packs change how a game looks or plays: higher resolutions, frame-rate patches, fixes for known glitches and mods. Download the community packs, turn on the ones you want for this game, and pick their options. Nothing is turned on automatically.\n\nHigher resolutions cost speed and memory, and what is reasonable depends on the device, so the options show a suggestion for this one.\n\nPacks listed under All games apply to every game, not just this one. Packs apply the next time you start the game."
            )
        }
    }

    private var gameSavesSection: some View {
        Section {
            exportSaveButton
            importSaveButton
            if let saveTransferMessage {
                Text(saveTransferMessage)
                    .font(.system(size: 12))
                    .foregroundColor(saveTransferFailed ? .red : EspressUTheme.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } header: {
            SettingsSectionHeader("Game saves", icon: "externaldrive", accent: .io)
        } footer: {
            InfoButton.footer(
                "The save the game itself writes. Export is available once the game has saved.",
                title: "Game saves",
                text: "This is the save the game itself writes. It uses the Wii U\'s own format, so it can move between EspressU, desktop Cemu and a real console.\n\nExport writes a folder named after the game and title ID. Import accepts that folder, a folder named after the title ID from another Cemu install, or its \'user\' folder. Importing replaces the current save; the old one is first copied to save-backups in EspressU\'s Documents folder."
            )
        }
    }

    private var exportSaveButton: some View {
        Button {
            GameSaveTransfer.export(game) { result in
                switch result {
                case .success(let note):
                    saveTransferMessage = note
                    saveTransferFailed = false
                case .failure(let error):
                    saveTransferMessage = error.localizedDescription
                    if case GameSaveTransfer.TransferError.cancelled = error {
                        saveTransferFailed = false
                    } else {
                        saveTransferFailed = true
                    }
                }
            }
        } label: {
            Label("Export game saves", systemImage: "square.and.arrow.up")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .disabled(!GameSaveTransfer.hasSave(for: game))
    }

    private var importSaveButton: some View {
        Button(role: .destructive) {
            showingImportConfirmation = true
        } label: {
            Label("Import game save folder", systemImage: "square.and.arrow.down")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
    }

    private func pickSaveFolder() {
        DocumentImport.present(contentTypes: [.folder]) { result in
            switch result {
            case .success(let urls):
                guard let picked = urls.first else { return }
                do {
                    saveTransferMessage = try GameSaveTransfer.importSave(game, from: picked)
                    saveTransferFailed = false
                } catch {
                    saveTransferMessage = error.localizedDescription
                    saveTransferFailed = true
                }
            case .failure(let error):
                saveTransferMessage = error.localizedDescription
                saveTransferFailed = true
            }
        }
    }

    private var shaderCachesSection: some View {
        Section {
            Button {
                pickCacheFile(.shaders)
            } label: {
                Label("Import learned shaders", systemImage: "square.and.arrow.down")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .disabled(cacheImportBusy)
            Button {
                pickCacheFile(.pipeline)
            } label: {
                Label("Import shader pipeline caches", systemImage: "square.and.arrow.down.on.square")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .disabled(cacheImportBusy)
            if cacheImportBusy {
                HStack(spacing: 8) {
                    ProgressView()
                    Text("Importing\u{2026}")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            if let cacheImportMessage {
                Text(cacheImportMessage)
                    .font(.system(size: 12))
                    .foregroundColor(cacheImportFailed ? .red : EspressUTheme.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } header: {
            SettingsSectionHeader("Shader caches", icon: "hammer", accent: .core)
        } footer: {
            InfoButton.footer(
                "Add a shader cache from desktop Cemu or another EspressU, so this game starts with its shaders already learned.",
                title: "Shader caches",
                text: "Learned shaders are what a game has revealed by drawing with them. Importing adds them to this game's own file: anything the game already has stays as it is, and the file is never replaced.\n\nThe file has to be this game's: EspressU checks the version stamp inside it, not the name. Desktop Cemu's files work: learned shaders are named like 000500001010ec00_shaders.bin and are converted the first time the game starts. EspressU's are named ..._mtlshaders.bin. Desktop caches from before Cemu 1.16 use another format and can't be converted.\n\nPipeline caches are named ..._vkpipeline.bin (desktop Cemu, Vulkan) or ..._mtlpipeline.bin (EspressU, Metal). A Vulkan one only helps the Vulkan renderer and a Metal one only the Metal renderer; it's stored either way and used when you switch. Each also needs the game's learned shaders. Import with the game closed. The sizes in Settings update the next time you open it."
            )
        }
    }

    private func pickCacheFile(_ kind: ShaderCacheImport.Kind) {
        DocumentImport.present(contentTypes: ShaderCacheImport.pickerTypes) { result in
            switch result {
            case .success(let urls):
                guard let picked = urls.first else { return }
                let name = picked.lastPathComponent
                cacheImportBusy = true
                cacheImportMessage = nil
                DispatchQueue.global(qos: .userInitiated).async {
                    let staged: URL
                    do {
                        staged = try ShaderCacheImport.stage(picked)
                    } catch {
                        DispatchQueue.main.async {
                            finishCacheImport(ShaderCacheImport.Outcome(message: "Couldn't read \"\(name)\": \(error.localizedDescription)", failed: true))
                        }
                        return
                    }
                    if kind == .pipeline, ShaderCacheImport.pipelineIsVulkan(fileName: name) == nil {
                        DispatchQueue.main.async {
                            cacheImportBusy = false
                            pendingPipeline = PendingPipeline(staged: staged, name: name)
                        }
                        return
                    }
                    runCacheImport(kind, staged: staged, name: name, pipelineIsVulkan: ShaderCacheImport.pipelineIsVulkan(fileName: name))
                }
            case .failure(let error):
                finishCacheImport(ShaderCacheImport.Outcome(message: error.localizedDescription, failed: true))
            }
        }
    }

    private func runCacheImport(_ kind: ShaderCacheImport.Kind, staged: URL, name: String, pipelineIsVulkan: Bool?) {
        let outcome = ShaderCacheImport.run(kind: kind, game: game, library: libraryGames, staged: staged,
                                            originalName: name, pipelineIsVulkan: pipelineIsVulkan)
        DispatchQueue.main.async { finishCacheImport(outcome) }
    }

    private func finishCacheImport(_ outcome: ShaderCacheImport.Outcome) {
        cacheImportBusy = false
        cacheImportMessage = outcome.message
        cacheImportFailed = outcome.failed
    }

    private func answerPipelineRenderer(_ pending: PendingPipeline, vulkan: Bool) {
        pendingPipeline = nil
        cacheImportBusy = true
        DispatchQueue.global(qos: .userInitiated).async {
            runCacheImport(.pipeline, staged: pending.staged, name: pending.name, pipelineIsVulkan: vulkan)
        }
    }

    private func discardPending(_ pending: PendingPipeline) {
        try? FileManager.default.removeItem(at: pending.staged)
    }

    private var optionsForm: some View {
        Form {
            if showsOverrides {
                overridesSection
                AdvancedGameOptionsSection(game: game, store: store)
            }
            GameShaderCacheSection(game: game)
            graphicPacksSection
            gameSavesSection
            shaderCachesSection
        }
    }

    var body: some View {
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient
                    .ignoresSafeArea()

                optionsForm
            }
            .confirmationDialog("Import a save folder?", isPresented: $showingImportConfirmation, titleVisibility: .visible) {
                Button("Choose folder", role: .destructive, action: pickSaveFolder)
                Button("Cancel", role: .cancel) { }
            } message: {
                Text("This replaces \(game.title)\'s current save. The one you have now is backed up first, and the game should be closed before you do this.")
            }
            .confirmationDialog("Which renderer made this pipeline cache?", isPresented: Binding(
                get: { pendingPipeline != nil },
                set: { if !$0 { pendingPipeline = nil } }
            ), titleVisibility: .visible, presenting: pendingPipeline) { pending in
                Button("Vulkan (desktop Cemu)") { answerPipelineRenderer(pending, vulkan: true) }
                Button("Metal (EspressU)") { answerPipelineRenderer(pending, vulkan: false) }
                Button("Cancel", role: .cancel) { discardPending(pending) }
            } message: { pending in
                Text("\"\(pending.name)\" doesn't say. Desktop Cemu's are named ..._vkpipeline.bin and EspressU's ..._mtlpipeline.bin. It's stored under the matching name either way.")
            }
            .navigationTitle(showsOverrides ? game.title : "Packs, saves and caches")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .navigationViewStyle(.stack)
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}
