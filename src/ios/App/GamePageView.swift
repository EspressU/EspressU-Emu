// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

enum LibraryTapAction: String, CaseIterable, Identifiable {
    case openPage
    case playImmediately

    static let storageKey = "espressu.library.tapAction"
    static let defaultValue = LibraryTapAction.openPage

    var id: String { rawValue }

    var title: String {
        switch self {
        case .openPage: return "Open its page"
        case .playImmediately: return "Play immediately"
        }
    }

    static func current(raw: String) -> LibraryTapAction { LibraryTapAction(rawValue: raw) ?? defaultValue }
}

struct GamePageView: View {
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
    @Environment(\.dismiss) private var dismiss
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass
    @Environment(\.verticalSizeClass) private var verticalSizeClass

    private enum PageSheet: String, Identifiable {
        case settings, cover, rename
        var id: String { rawValue }
    }

    @State private var sheet: PageSheet?
    @State private var sizeOnDisk: Int64?
    @State private var shaderBytes: Int64?
    @State private var confirmingRemoval = false

    private var live: GameMetadata { gameManager.games.first { $0.id == game.id } ?? game }

    private var installLine: String {
        if let label = live.installLabel { return label }
        return live.installKey.replacingOccurrences(of: "install:", with: "")
    }

    private var sideBySide: Bool { horizontalSizeClass == .regular || verticalSizeClass == .compact }

    var body: some View {
        EspressUNavigation {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()
                Form {
                    heroSection
                    GameInfoSections(game: live, gameManager: gameManager)
                    yourPlaySection
                    manageSection
                    filesSection
                    GameContentSection(game: live)
                    GameShaderCacheSection(game: live)
                    removeSection
                }
            }
            .navigationTitle(live.cardName.name)
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
            }
            .sheet(item: $sheet) { which in
                switch which {
                case .settings:
                    GameOptionsView(game: live, store: store, libraryGames: gameManager.games)
                case .cover:
                    CoverArtPickerView(game: live, gameManager: gameManager)
                case .rename:
                    LibraryRenameSheet(game: live)
                }
            }
            .confirmationDialog(live.removeConfirmTitle, isPresented: $confirmingRemoval, titleVisibility: .visible) {
                Button(live.removeConfirmButton, role: .destructive) { closeThen(onRemoveGame) }
                Button("Cancel", role: .cancel) { }
            } message: {
                Text(live.removeConfirmMessage)
            }
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear {
            loadSizes()
        }
    }

    private var heroSection: some View {
        Section {
            VStack(spacing: 18) {
                if sideBySide {
                    HStack(alignment: .center, spacing: 24) {
                        cover(width: 150)
                        VStack(alignment: .leading, spacing: 14) {
                            titleBlock(alignment: .leading)
                            playButtons
                        }
                    }
                } else {
                    cover(width: 190)
                    titleBlock(alignment: .center)
                    playButtons
                }
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 8)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 8, leading: 16, bottom: 8, trailing: 16))
        }
    }

    private func cover(width: CGFloat) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 18, style: .continuous).fill(EspressUTheme.cremaTopGradient)
            if let path = live.coverPath {
                CoverImage(path: path, padding: 8)
            } else {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: width * 0.3))
                    .foregroundColor(EspressUTheme.onCremaTop)
            }
        }
        .frame(width: width, height: width * 4 / 3)
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 14, x: 0, y: 8)
        .accessibilityHidden(true)
    }

    private func titleBlock(alignment: HorizontalAlignment) -> some View {
        VStack(alignment: alignment, spacing: 5) {
            Text(live.cardName.name)
                .font(.title2.weight(.bold))
                .multilineTextAlignment(alignment == .center ? .center : .leading)
                .fixedSize(horizontal: false, vertical: true)
            if let info = data.info(for: game.id), let by = info.publisher ?? info.developer {
                Text(by)
                    .font(.subheadline)
                    .foregroundColor(EspressUTheme.brownMid)
            }
            Label(live.region ?? "Region unknown", systemImage: "globe")
                .font(.footnote)
                .foregroundColor(EspressUTheme.brownMid)
            Text(installLine)
                .font(.caption)
                .foregroundColor(EspressUTheme.secondaryText)
                .multilineTextAlignment(alignment == .center ? .center : .leading)
                .lineLimit(2)
        }
    }

    private var playButtons: some View {
        Button {
            closeThen(onPlay)
        } label: {
            HStack(spacing: 10) {
                Image(systemName: "play.fill").font(.system(size: 18, weight: .bold))
                Text("Play").font(.title3.weight(.semibold))
            }
            .frame(maxWidth: .infinity, minHeight: 36)
        }
        .buttonStyle(EspressUPrimaryButtonStyle())
        .frame(maxWidth: 340)
        .accessibilityLabel("Play \(live.cardName.name)")
    }

    private var yourPlaySection: some View {
        Section {
            SettingsRow(label: "Last played", value: lastPlayedText, icon: "clock.arrow.circlepath")
            SettingsRow(label: "Times played", value: "\(stats.entry(for: game.id)?.count ?? 0)", icon: "number")
            SettingsRow(label: "Size on disk", value: sizeOnDisk.map { ByteCountFormatter.string(fromByteCount: $0, countStyle: .file) } ?? "\u{2026}",
                        icon: "internaldrive")
            if live.titleId != nil {
                SettingsRow(label: "Shader cache", value: shaderBytes.map { ShaderCacheInfo.formatBytes($0) } ?? "\u{2026}",
                            icon: "externaldrive")
            }
        } header: {
            SettingsSectionHeader("Your play", icon: "clock.arrow.circlepath", accent: .content)
        }
    }

    private var lastPlayedText: String {
        guard let entry = stats.entry(for: game.id) else { return "Not yet" }
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .full
        return formatter.localizedString(for: entry.last, relativeTo: Date())
    }

    private var manageSection: some View {
        let key = live.settingsKey
        return Section {
            Button { sheet = .settings } label: {
                rowLabel("Game settings", systemImage: "slider.horizontal.3", chevron: true)
            }
            Toggle(isOn: Binding(
                get: { live.isFavorite },
                set: { _ in gameManager.toggleFavorite(live) }
            )) {
                rowLabel("Favourite", systemImage: live.isFavorite ? "heart.fill" : "heart")
            }
            .tint(EspressUTheme.accentText)
            Toggle(isOn: Binding(
                get: { store.effectivePreCompileShaders(for: key) },
                set: { store.setPreCompileShaders($0, for: key) }
            )) {
                rowLabel("Compile shaders in the background", systemImage: "bolt.fill")
            }
            .tint(EspressUTheme.accentText)
            if store.overrides(for: key).preCompileShaders != nil {
                Button { store.setPreCompileShaders(nil, for: key) } label: {
                    rowLabel("Use the global shader setting", systemImage: "arrow.uturn.backward")
                }
            }
            Button { sheet = .cover } label: {
                rowLabel("Change cover", systemImage: "photo")
            }
            if gameManager.hasManualCoverOverride(forGameID: game.id) {
                Button(role: .destructive) {
                    gameManager.removeManualCover(forGameID: game.id)
                } label: {
                    DestructiveSettingsLabel(title: "Remove custom cover", systemImage: "photo.badge.minus")
                }
            }
            Button { sheet = .rename } label: {
                rowLabel("Rename", systemImage: "pencil")
            }
            if names.name(for: live.installKey) != nil {
                Button { names.set(nil, for: live.installKey) } label: {
                    rowLabel("Reset to original title", systemImage: "arrow.uturn.backward")
                }
            }
        } header: {
            SettingsSectionHeader("Manage", icon: "slider.horizontal.3", accent: .core)
        }
    }

    private var filesSection: some View {
        let installed = DlcUpdateImport.installedContent(for: live)
        return Section {
            Button { closeThen(onImportUpdate) } label: {
                rowLabel("Import update", systemImage: "arrow.triangle.2.circlepath")
            }
            Button { closeThen(onImportDLC) } label: {
                rowLabel("Import DLC", systemImage: "shippingbox")
            }
            if installed.hasUpdate {
                Button(role: .destructive) { closeThen(onRemoveUpdate) } label: {
                    DestructiveSettingsLabel(title: "Remove update", systemImage: "trash")
                }
            }
            if installed.hasDLC {
                Button(role: .destructive) { closeThen(onRemoveDLC) } label: {
                    DestructiveSettingsLabel(title: "Remove DLC", systemImage: "trash")
                }
            }
            if gameSupportsDecryptToFiles(romPath: live.romPath) {
                Button { closeThen(onDecrypt) } label: {
                    rowLabel("Decrypt", systemImage: "lock.open")
                }
            }
        } header: {
            SettingsSectionHeader("Updates and files", icon: "shippingbox", accent: .io)
        }
    }

    private var removeSection: some View {
        Section {
            Button(role: .destructive) {
                confirmingRemoval = true
            } label: {
                DestructiveSettingsLabel(title: live.removeActionTitle, systemImage: "trash")
            }
        } footer: {
            Text(live.isExternal
                 ? "Only takes the game out of your library. Its file stays where it is. Your saves and options are kept."
                 : "Frees up the space the game uses. Your saves and options are kept.")
        }
    }

    private func rowLabel(_ title: String, systemImage: String, chevron: Bool = false) -> some View {
        HStack {
            Label(title, systemImage: systemImage)
                .font(.subheadline.weight(.semibold))
            if chevron {
                Spacer(minLength: 8)
                Image(systemName: "chevron.right")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        }
    }

    private func closeThen(_ action: @escaping () -> Void) {
        dismiss()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { action() }
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
