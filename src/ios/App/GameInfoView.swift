// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct GameInfoSections: View {
    let game: GameMetadata
    @ObservedObject var gameManager: GameManager
    @ObservedObject private var data = GameDataStore.shared
    @State private var showingPicker = false

    private var info: GameInfo? { data.info(for: game.id) }
    private var match: GameMatch? { data.match(for: game.id) }

    var body: some View {
        Group {
            if let info {
                summarySection(info)
                detailsSection(info)
                releasesSection(info)
                if !info.controls.isEmpty { controlsSection(info) }
                identitySection(info)
                matchSection(info)
                attributionSection(info)
            } else {
                emptySection
            }
        }
        .sheet(isPresented: $showingPicker) {
            GameMatchPickerView(game: game, gameManager: gameManager)
        }
    }

    private func summarySection(_ info: GameInfo) -> some View {
        Section {
            if let s = info.bestSynopsis {
                Text(s.text)
                    .font(.system(size: 14))
                    .fixedSize(horizontal: false, vertical: true)
                if s.language != GameInfo.preferredLanguages.first {
                    Text("Summary language: \(s.language)")
                        .font(.system(size: 12)).foregroundColor(EspressUTheme.secondaryText)
                }
            } else {
                Text("GameTDB has no summary for this game.")
                    .font(.system(size: 13)).foregroundColor(EspressUTheme.secondaryText)
            }
        } header: {
            SettingsSectionHeader(info.displayTitle, icon: "text.alignleft", accent: .content)
        }
    }

    private func detailsSection(_ info: GameInfo) -> some View {
        Section {
            if let p = info.publisher { SettingsRow(label: "Publisher", value: p, icon: "building.2") }
            if let d = info.developer { SettingsRow(label: "Developer", value: d, icon: "hammer") }
            if !info.genres.isEmpty { SettingsRow(label: "Genre", value: info.genres.joined(separator: ", "), icon: "tag") }
            if let players = playersText(info) { SettingsRow(label: "Players", value: players, icon: "person.2") }
            if let rating = ratingText(info) { SettingsRow(label: "Rating", value: rating, icon: "checkmark.shield") }
            if !info.languages.isEmpty { SettingsRow(label: "Languages", value: info.languages.joined(separator: ", "), icon: "character.bubble") }
            if let size = info.romSize { SettingsRow(label: "Size", value: ByteCountFormatter.string(fromByteCount: size, countStyle: .file), icon: "internaldrive") }
        } header: {
            SettingsSectionHeader("Details", icon: "list.bullet", accent: .content)
        }
    }

    private func releasesSection(_ info: GameInfo) -> some View {
        Section {
            let dated = info.releases.filter { $0.dateText != nil }
            if dated.isEmpty {
                Text("No release dates listed.").font(.system(size: 13)).foregroundColor(EspressUTheme.secondaryText)
            }
            ForEach(Array(dated.enumerated()), id: \.offset) { _, r in
                SettingsRow(label: RegionCode(rawValue: r.region)?.name ?? r.region, value: r.dateText ?? "", icon: "calendar")
            }
        } header: {
            SettingsSectionHeader("Released", icon: "calendar", accent: .content)
        }
    }

    private func controlsSection(_ info: GameInfo) -> some View {
        Section {
            ForEach(info.controls, id: \.self) { c in
                SettingsRow(label: c.title, value: c.required ? "Required" : "Supported", icon: "gamecontroller")
            }
        } header: {
            SettingsSectionHeader("Controllers", icon: "gamecontroller", accent: .content)
        }
    }

    private func identitySection(_ info: GameInfo) -> some View {
        Section {
            if let t = game.titleId { SettingsRow(label: "Title ID", value: GameMetadata.settingsKey(forTitleId: t), icon: "number") }
            SettingsRow(label: "Product code", value: info.productCode, icon: "barcode")
            SettingsRow(label: "GameTDB ID", value: info.id, icon: "number.square")
            if let region = game.region { SettingsRow(label: "This install", value: region, icon: "globe") }
        } header: {
            SettingsSectionHeader("Identity", icon: "number", accent: .content)
        }
    }

    private func matchSection(_ info: GameInfo) -> some View {
        Section {
            if let match {
                SettingsRow(label: "Matched by", value: match.methodText, icon: "link")
                SettingsRow(label: "Confidence", value: "\(match.confidenceText) (\(Int((match.confidence * 100).rounded()))%)", icon: "scope")
            }
            Button {
                showingPicker = true
            } label: {
                Label("Wrong game? Choose\u{2026}", systemImage: "magnifyingglass")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
        } header: {
            SettingsSectionHeader("Match", icon: "link", accent: .content)
        } footer: {
            InfoButton.footer("The match is for this install only. Another copy of the same game can match differently.")
        }
    }

    private func attributionSection(_ info: GameInfo) -> some View {
        Section {
            if let url = info.gameTdbURL {
                Link(destination: url) {
                    Label("Open this game on GameTDB", systemImage: "safari")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                }
            }
            Text("Game data and covers from GameTDB (gametdb.com). Box art \u{00A9} respective publishers.")
                .font(.system(size: 12)).foregroundColor(EspressUTheme.secondaryText)
        }
    }

    private var emptySection: some View {
        Section {
            ScreenStatusCallout(tone: .info, message: data.isScraping
                ? "Reading GameTDB's database\u{2026}"
                : "There is no game data for this install yet. Scrape it from Settings > Cover + data scraping, or here.")
            if data.isScraping {
                ProgressView(value: data.fraction)
            } else {
                Button {
                    data.scrape(games: gameManager.games)
                } label: {
                    Label("Scrape now", systemImage: "arrow.down.doc")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                }
                Button {
                    showingPicker = true
                } label: {
                    Label("Choose the game\u{2026}", systemImage: "magnifyingglass")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                }
            }
            if let error = data.scrapeError {
                Text(error).font(.system(size: 12)).foregroundColor(.red)
            }
        } header: {
            SettingsSectionHeader(game.cardName.name, icon: "info.circle", accent: .content)
        }
    }

    private func playersText(_ info: GameInfo) -> String? {
        var parts: [String] = []
        if let n = info.localPlayers { parts.append("Up to \(n) local") }
        if let n = info.onlinePlayers { parts.append("up to \(n) online") }
        else if !info.onlineFeatures.isEmpty { parts.append("online") }
        guard !parts.isEmpty else { return nil }
        let joined = parts.joined(separator: ", ")
        return joined.prefix(1).uppercased() + joined.dropFirst()
    }

    private func ratingText(_ info: GameInfo) -> String? {
        guard let type = info.ratingType else { return nil }
        var text = type
        if let v = info.ratingValue { text += " \(v)" }
        if !info.ratingDescriptors.isEmpty { text += " (\(info.ratingDescriptors.map { $0.capitalized }.joined(separator: ", ")))" }
        return text
    }
}

struct GameMatchPickerView: View {
    let game: GameMetadata
    @ObservedObject var gameManager: GameManager
    @ObservedObject private var data = GameDataStore.shared
    @Environment(\.dismiss) private var dismiss
    @State private var query = ""

    private var regions: Set<RegionCode> { RegionCode.codes(in: game.region) }
    private var results: [TitleIndexRecord] { data.search(query, preferring: regions) }

    var body: some View {
        NavigationView {
            List {
                if !data.hasTitleIndex {
                    Section {
                        ScreenStatusCallout(tone: .info, message: "The title list comes from GameTDB's database. Scrape once to download it.")
                        if data.isScraping {
                            ProgressView(value: data.fraction)
                        } else {
                            Button("Scrape now") { data.scrape(games: gameManager.games) }
                        }
                    }
                } else {
                    if data.manualOverride(for: game.id) != nil {
                        Section {
                            Button {
                                choose(nil)
                            } label: {
                                Label("Go back to automatic matching", systemImage: "arrow.uturn.backward")
                                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                            }
                        }
                    }
                    Section {
                        ForEach(results, id: \.id) { rec in
                            Button { choose(rec.id) } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(rec.titles.first ?? rec.id)
                                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                                        .foregroundColor(EspressUTheme.brownDarkest)
                                    Text("\(RegionCode(rawValue: rec.region)?.name ?? rec.region), \(rec.id)")
                                        .font(.system(size: 12)).foregroundColor(EspressUTheme.secondaryText)
                                }
                            }
                        }
                    } footer: {
                        if query.trimmingCharacters(in: .whitespaces).isEmpty {
                            Text("Type the game's name. Entries for this install's region are listed first.")
                        } else if results.isEmpty {
                            Text("Nothing in GameTDB matches that.")
                        }
                    }
                }
            }
            .searchable(text: $query, prompt: "Search GameTDB titles")
            .navigationTitle("Choose the game")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
            }
            .onAppear { if query.isEmpty { query = game.displayTitle ?? game.cardName.name } }
        }
        .navigationViewStyle(.stack)
    }

    private func choose(_ tdbID: String?) {
        data.setManualOverride(tdbID, for: game.id)
        if let roms = gameManager.romsDirectoryURL { CoverArtFetcher.clearCache(gameID: game.id, in: roms) }
        data.scrape(games: gameManager.games)
        NotificationCenter.default.post(name: .espressuCoverArtSourcesChanged, object: nil)
        dismiss()
    }
}
