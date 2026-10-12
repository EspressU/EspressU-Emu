// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

enum LibraryCardStyle: String, CaseIterable, Hashable, Identifiable {
    case standard
    case largeCovers
    case compact
    case list

    static let storageKey = "espressu.library.cardStyle"
    static let defaultValue: LibraryCardStyle = .standard

    var id: String { rawValue }

    var title: String {
        switch self {
        case .standard: return "Cards"
        case .largeCovers: return "Large covers"
        case .compact: return "Compact grid"
        case .list: return "List"
        }
    }

    var detail: String {
        switch self {
        case .standard: return "Cover, name and a Play button"
        case .largeCovers: return "Bigger covers, tap a cover to play"
        case .compact: return "Many small covers on screen at once"
        case .list: return "One game per row, with details"
        }
    }

    var systemImage: String {
        switch self {
        case .standard: return "square.grid.2x2"
        case .largeCovers: return "square.grid.2x2.fill"
        case .compact: return "square.grid.3x3"
        case .list: return "list.bullet"
        }
    }

    static let sizeStorageKey = "espressu.library.cardSize"

    static var choices: [LibraryCardStyle] { allCases }
    static let sizeRange: ClosedRange<Double> = 0.6...1.6
    static let sizeDefault = 1.0

    var columns: [GridItem] { columns(scale: 1) }

    func columns(scale: Double, available: CGFloat = .infinity) -> [GridItem] {
        let s = CGFloat(min(max(scale, Self.sizeRange.lowerBound), Self.sizeRange.upperBound))
        let cap = max(available, 64)
        func item(_ base: CGFloat, _ spacing: CGFloat) -> [GridItem] {
            [GridItem(.adaptive(minimum: min(base * s, cap)), spacing: spacing)]
        }
        switch self {
        case .standard: return item(140, 16)
        case .largeCovers: return item(220, 18)
        case .compact: return item(96, 12)
        case .list: return item(340, 12)
        }
    }

    var rowSpacing: CGFloat {
        switch self {
        case .standard: return 20
        case .largeCovers: return 22
        case .compact: return 14
        case .list: return 10
        }
    }

    static var current: LibraryCardStyle {
        LibraryCardStyle(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? defaultValue
    }
}

enum LibraryGrouping: String, CaseIterable, Hashable, Identifiable {
    case none
    case favorites
    case recentlyPlayed
    case alphabet

    static let storageKey = "espressu.library.grouping"
    static let defaultValue: LibraryGrouping = .none

    var id: String { rawValue }

    var title: String {
        switch self {
        case .none: return "No groups"
        case .favorites: return "Favourites and the rest"
        case .recentlyPlayed: return "Recently played"
        case .alphabet: return "A to Z"
        }
    }

    var systemImage: String {
        switch self {
        case .none: return "rectangle.grid.1x2"
        case .favorites: return "heart"
        case .recentlyPlayed: return "clock.arrow.circlepath"
        case .alphabet: return "textformat.abc"
        }
    }
}

enum LibraryFilter: String, CaseIterable, Hashable, Identifiable {
    case all
    case favorites
    case played
    case neverPlayed

    static let storageKey = "espressu.library.filter"
    static let defaultValue: LibraryFilter = .all

    var id: String { rawValue }

    var title: String {
        switch self {
        case .all: return "All games"
        case .favorites: return "Favourites only"
        case .played: return "Played before"
        case .neverPlayed: return "Never played"
        }
    }

    var systemImage: String {
        switch self {
        case .all: return "square.grid.2x2"
        case .favorites: return "heart"
        case .played: return "checkmark.circle"
        case .neverPlayed: return "sparkles"
        }
    }

    func apply(_ games: [GameMetadata], stats: LibraryPlayStats) -> [GameMetadata] {
        switch self {
        case .all: return games
        case .favorites: return games.filter { $0.isFavorite }
        case .played: return games.filter { stats.entry(for: $0.id) != nil }
        case .neverPlayed: return games.filter { stats.entry(for: $0.id) == nil }
        }
    }
}

struct LibrarySection: Identifiable {
    let id: String
    let title: String?
    let games: [GameMetadata]
}

extension LibraryGrouping {
    func sections(for games: [GameMetadata], stats: LibraryPlayStats) -> [LibrarySection] {
        switch self {
        case .none:
            return [LibrarySection(id: "all", title: nil, games: games)]
        case .favorites:
            let favs = games.filter { $0.isFavorite }
            let rest = games.filter { !$0.isFavorite }
            return [
                LibrarySection(id: "favorites", title: "Favourites", games: favs),
                LibrarySection(id: "rest", title: favs.isEmpty ? nil : "Other games", games: rest),
            ].filter { !$0.games.isEmpty }
        case .recentlyPlayed:
            let recentIDs = Set(stats.recentIDs(limit: 8))
            let recent = games.filter { recentIDs.contains($0.id) }
                .sorted { (stats.entry(for: $0.id)?.last ?? .distantPast) > (stats.entry(for: $1.id)?.last ?? .distantPast) }
            let rest = games.filter { !recentIDs.contains($0.id) }
            return [
                LibrarySection(id: "recent", title: "Recently played", games: recent),
                LibrarySection(id: "rest", title: recent.isEmpty ? nil : "Everything else", games: rest),
            ].filter { !$0.games.isEmpty }
        case .alphabet:
            var order: [String] = []
            var buckets: [String: [GameMetadata]] = [:]
            for game in games {
                let letter = Self.letter(for: game)
                if buckets[letter] == nil { order.append(letter) }
                buckets[letter, default: []].append(game)
            }
            let sortedLetters = order.sorted { a, b in
                if a == "#" { return false }
                if b == "#" { return true }
                return a < b
            }
            return sortedLetters.map { LibrarySection(id: "az-\($0)", title: $0, games: buckets[$0] ?? []) }
        }
    }

    private static func letter(for game: GameMetadata) -> String {
        let folded = game.sortTitle.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.diacriticInsensitive, .caseInsensitive], locale: .current)
        guard let first = folded.unicodeScalars.first,
              CharacterSet.letters.contains(first) else { return "#" }
        return String(first).uppercased()
    }
}

final class LibraryCustomNames: ObservableObject {
    static let shared = LibraryCustomNames()
    private static let defaultsKey = "espressu.library.customNames"

    @Published private(set) var names: [String: String]

    private init() {
        if let data = UserDefaults.standard.data(forKey: Self.defaultsKey),
           let decoded = try? JSONDecoder().decode([String: String].self, from: data) {
            names = decoded
        } else {
            names = [:]
        }
    }

    func name(for key: String) -> String? { names[key] }

    func migrateLegacyKeys(games: [GameMetadata]) {
        let legacy = names.keys.filter { !$0.hasPrefix("install:") }
        guard !legacy.isEmpty, !games.isEmpty else { return }
        var changed = false
        for key in legacy {
            let matches = games.filter { $0.settingsKey == key }
            if matches.isEmpty { continue }
            if matches.count == 1, let value = names[key], names[matches[0].installKey] == nil {
                names[matches[0].installKey] = value
            }
            names.removeValue(forKey: key)
            changed = true
        }
        if changed, let data = try? JSONEncoder().encode(names) {
            UserDefaults.standard.set(data, forKey: Self.defaultsKey)
        }
    }

    func set(_ name: String?, for key: String) {
        let trimmed = name?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if trimmed.isEmpty { names.removeValue(forKey: key) } else { names[key] = trimmed }
        if let data = try? JSONEncoder().encode(names) {
            UserDefaults.standard.set(data, forKey: Self.defaultsKey)
        }
    }
}

struct LibraryRenameSheet: View {
    let game: GameMetadata
    @Environment(\.presentationMode) private var presentationMode
    @State private var text: String

    init(game: GameMetadata) {
        self.game = game
        _text = State(initialValue: LibraryCustomNames.shared.name(for: game.installKey) ?? game.cardName.name)
    }

    private var originalTitle: String { game.displayTitle ?? game.title }
    private var installDescription: String { game.installLabel ?? "" }

    var body: some View {
        NavigationView {
            Form {
                Section {
                    TextField("Name", text: $text)
                        .autocapitalization(.words)
                } footer: {
                    Text("Only changes how the game is named in your library. Original title: \(originalTitle)"
                         + (installDescription.isEmpty ? "" : "\nInstall: \(installDescription)"))
                }
                if LibraryCustomNames.shared.name(for: game.installKey) != nil {
                    Section {
                        Button("Reset to Original Title") {
                            LibraryCustomNames.shared.set(nil, for: game.installKey)
                            presentationMode.wrappedValue.dismiss()
                        }
                    }
                }
            }
            .navigationTitle("Rename")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { presentationMode.wrappedValue.dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        let same = text.trimmingCharacters(in: .whitespacesAndNewlines) == originalTitle
                        LibraryCustomNames.shared.set(same ? nil : text, for: game.installKey)
                        presentationMode.wrappedValue.dismiss()
                    }
                }
            }
        }
        .navigationViewStyle(.stack)
    }
}

final class LibraryPlayStats: ObservableObject {
    static let shared = LibraryPlayStats()
    private static let defaultsKey = "espressu.library.playStats"

    struct Entry: Codable {
        var last: Date
        var count: Int
    }

    @Published private(set) var entries: [String: Entry]

    private init() {
        if let data = UserDefaults.standard.data(forKey: Self.defaultsKey),
           let decoded = try? JSONDecoder().decode([String: Entry].self, from: data) {
            entries = decoded
        } else {
            entries = [:]
        }
    }

    func entry(for gameID: String) -> Entry? { entries[gameID] }

    func recordLaunch(of gameID: String) {
        var entry = entries[gameID] ?? Entry(last: Date(), count: 0)
        entry.last = Date()
        entry.count += 1
        entries[gameID] = entry
        if let data = try? JSONEncoder().encode(entries) {
            UserDefaults.standard.set(data, forKey: Self.defaultsKey)
        }
    }

    func recentIDs(limit: Int) -> [String] {
        entries.sorted { $0.value.last > $1.value.last }.prefix(limit).map { $0.key }
    }
}

struct LibraryGameCollection<Card: View, Lead: View>: View {
    let sections: [LibrarySection]
    let style: LibraryCardStyle
    @ViewBuilder let lead: () -> Lead
    @ViewBuilder let card: (GameMetadata) -> Card
    @AppStorage(LibraryCardStyle.sizeStorageKey) private var cardSize = 1.0

    var body: some View {
        GeometryReader { geo in
            ScrollView(showsIndicators: false) {
                LazyVGrid(columns: style.columns(scale: cardSize, available: geo.size.width - 32),
                          spacing: style.rowSpacing, pinnedViews: [.sectionHeaders]) {
                    Section { lead() }
                    ForEach(sections) { section in
                        Section {
                            ForEach(section.games) { game in
                                card(game)
                            }
                        } header: {
                            if let title = section.title {
                                LibrarySectionHeader(title: title, count: section.games.count)
                            }
                        }
                    }
                }
                .padding(16)
            }
        }
    }
}

struct LibrarySectionHeader: View {
    let title: String
    let count: Int

    var body: some View {
        HStack(spacing: 8) {
            Text(title)
                .font(.system(size: 15, weight: .bold, design: .rounded))
                .foregroundColor(EspressUTheme.brownDarkest)
            Text("\(count)")
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
            Spacer()
        }
        .padding(.vertical, 6)
        .padding(.horizontal, 4)
        .frame(maxWidth: .infinity)
        .background(EspressUTheme.cream)
        .accessibilityAddTraits(.isHeader)
    }
}

struct LibraryCoverWell: View {
    let game: GameMetadata
    var radius: CGFloat = 14
    var glyphSize: CGFloat = 24

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: radius, style: .continuous)
                .fill(EspressUTheme.cremaTopGradient)
            if let path = game.coverPath {
                CoverImage(path: path, padding: 8)
            } else {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: glyphSize))
                    .foregroundColor(EspressUTheme.onCremaTop)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
    }
}

private struct LibraryHeartButton: View {
    let isFavorite: Bool
    let size: CGFloat
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: isFavorite ? "heart.fill" : "heart")
                .font(.system(size: size * 0.45, weight: .semibold))
                .foregroundColor(isFavorite ? EspressUTheme.alertOnDark : EspressUTheme.sparkleCream)
                .frame(width: size, height: size)
                .background(Color.black.opacity(0.4))
                .cornerRadius(10)
                .frame(minWidth: 44, minHeight: 44)
                .contentShape(Rectangle())
        }
        .accessibilityLabel(isFavorite ? "Remove from favourites" : "Add to favourites")
    }
}

private struct LibraryOptionsButton<Options: View>: View {
    let name: String
    let size: CGFloat
    let options: Options

    var body: some View {
        Menu {
            options
        } label: {
            Image(systemName: "ellipsis")
                .font(.system(size: size * 0.45, weight: .semibold))
                .foregroundColor(EspressUTheme.sparkleCream)
                .frame(width: size, height: size)
                .background(Color.black.opacity(0.4))
                .cornerRadius(10)
                .frame(minWidth: 44, minHeight: 44)
                .contentShape(Rectangle())
        }
        .accessibilityLabel("More options for \(name)")
    }
}

private func libraryCardChrome<V: View>(_ view: V, radius: CGFloat = 16) -> some View {
    view
        .background(EspressUTheme.cream)
        .cornerRadius(radius)
        .overlay(
            RoundedRectangle(cornerRadius: radius, style: .continuous)
                .stroke(EspressUTheme.wrapper, lineWidth: 1)
        )
        .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 8, x: 0, y: 4)
}

struct LibraryCard<Options: View>: View {
    let game: GameMetadata
    let style: LibraryCardStyle
    let onTap: () -> Void
    let onFavoriteTap: () -> Void
    var onPlay: (() -> Void)?
    let options: Options

    init(game: GameMetadata, style: LibraryCardStyle, onTap: @escaping () -> Void,
         onFavoriteTap: @escaping () -> Void, onPlay: (() -> Void)? = nil,
         @ViewBuilder options: () -> Options) {
        self.game = game
        self.style = style
        self.onTap = onTap
        self.onFavoriteTap = onFavoriteTap
        self.onPlay = onPlay
        self.options = options()
    }

    var body: some View {
        card
            .opacity(game.isUnavailable ? 0.55 : 1)
    }

    @ViewBuilder private var card: some View {
        switch style {
        case .standard:
            GameCardOptimized(game: game, onTap: onTap, onFavoriteTap: onFavoriteTap, onPlay: onPlay, options: { options })
        case .largeCovers:
            LibraryLargeCoverCard(game: game, onTap: onTap, onFavoriteTap: onFavoriteTap, options: options)
        case .compact:
            LibraryCompactCard(game: game, onTap: onTap, onFavoriteTap: onFavoriteTap, options: options)
        case .list:
            LibraryListRow(game: game, onTap: onTap, onFavoriteTap: onFavoriteTap, options: options)
        }
    }
}

struct LibraryLargeCoverCard<Options: View>: View {
    let game: GameMetadata
    let onTap: () -> Void
    let onFavoriteTap: () -> Void
    let options: Options

    var body: some View {
        let name = game.cardName
        libraryCardChrome(
            VStack(spacing: 0) {
                ZStack(alignment: .topTrailing) {
                    Button(action: onTap) {
                        LibraryCoverWell(game: game, radius: 0, glyphSize: 44)
                            .aspectRatio(3 / 4, contentMode: .fit)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Play \(name.name)")
                    LibraryHeartButton(isFavorite: game.isFavorite, size: 34, action: onFavoriteTap)
                        .padding(4)
                }
                .overlay(alignment: .topLeading) {
                    LibraryOptionsButton(name: name.name, size: 34, options: options)
                        .padding(4)
                }
                VStack(alignment: .leading, spacing: 4) {
                    Text(name.name)
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .lineLimit(2)
                        .foregroundColor(EspressUTheme.brownDarkest)
                    if let region = game.region {
                        Label(region, systemImage: "globe")
                            .font(.system(size: 12, weight: .regular, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                    if let label = game.installLabel { LibraryInstallLine(text: label, size: 12) }
                    LibraryExternalLine(game: game, size: 12)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(12)
            }
        )
        .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        .onTapGesture(perform: onTap)
    }
}

struct LibraryCompactCard<Options: View>: View {
    let game: GameMetadata
    let onTap: () -> Void
    let onFavoriteTap: () -> Void
    let options: Options

    var body: some View {
        let name = game.cardName
        VStack(spacing: 6) {
            ZStack(alignment: .topTrailing) {
                Button(action: onTap) {
                    LibraryCoverWell(game: game, radius: 12, glyphSize: 22)
                        .aspectRatio(3 / 4, contentMode: .fit)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Play \(name.name)")
                LibraryHeartButton(isFavorite: game.isFavorite, size: 26, action: onFavoriteTap)
                    .padding(.top, -6)
                    .padding(.trailing, -6)
            }
            .overlay(alignment: .topLeading) {
                LibraryOptionsButton(name: name.name, size: 26, options: options)
                    .padding(.top, -6)
                    .padding(.leading, -6)
            }
            Text(name.name)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .lineLimit(2)
                .multilineTextAlignment(.center)
                .foregroundColor(EspressUTheme.brownDarkest)
                .frame(maxWidth: .infinity)
            if let label = game.installLabel { LibraryInstallLine(text: label, size: 10, centered: true) }
            LibraryExternalLine(game: game, size: 10, centered: true)
        }
        .shadow(color: EspressUTheme.shadow.opacity(0.12), radius: 4, x: 0, y: 2)
        .contentShape(Rectangle())
        .onTapGesture(perform: onTap)
    }
}

struct LibraryListRow<Options: View>: View {
    let game: GameMetadata
    let onTap: () -> Void
    let onFavoriteTap: () -> Void
    let options: Options
    @ObservedObject private var stats = LibraryPlayStats.shared
    @AppStorage(LibraryCardStyle.sizeStorageKey) private var cardSize = 1.0

    private var listScale: CGFloat {
        CGFloat(min(max(cardSize, LibraryCardStyle.sizeRange.lowerBound), LibraryCardStyle.sizeRange.upperBound))
    }

    private var detailLine: String {
        var parts: [String] = []
        if let region = game.region { parts.append(region) }
        if let entry = stats.entry(for: game.id) {
            let relative = RelativeDateTimeFormatter()
            relative.unitsStyle = .short
            parts.append("Played " + relative.localizedString(for: entry.last, relativeTo: Date()))
            if entry.count > 1 { parts.append("\(entry.count) launches") }
        } else {
            parts.append("Not played yet")
        }
        return parts.joined(separator: "  \u{2022}  ")
    }

    var body: some View {
        let name = game.cardName
        libraryCardChrome(
            HStack(spacing: 12) {
                Button(action: onTap) {
                    HStack(spacing: 12) {
                        LibraryCoverWell(game: game, radius: 10, glyphSize: 18)
                            .frame(width: 56 * listScale, height: 74 * listScale)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(name.name)
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                                .lineLimit(2)
                                .multilineTextAlignment(.leading)
                                .foregroundColor(EspressUTheme.brownDarkest)
                            if let label = game.installLabel { LibraryInstallLine(text: label, size: 12) }
                            LibraryExternalLine(game: game, size: 12)
                            Text(detailLine)
                                .font(.system(size: 12, weight: .regular, design: .rounded))
                                .lineLimit(2)
                                .multilineTextAlignment(.leading)
                                .foregroundColor(EspressUTheme.brownMid)
                            if let idText = name.titleIdText {
                                Text(idText)
                                    .font(.system(size: 10, weight: .regular, design: .monospaced))
                                    .foregroundColor(EspressUTheme.brownMid)
                                    .lineLimit(1)
                                    .minimumScaleFactor(0.8)
                            }
                        }
                        Spacer(minLength: 0)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Play \(name.name)")

                Menu {
                    options
                } label: {
                    Image(systemName: "ellipsis")
                        .font(.system(size: 17, weight: .semibold))
                        .foregroundColor(EspressUTheme.brownMid)
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .accessibilityLabel("More options for \(name.name)")

                Button(action: onFavoriteTap) {
                    Image(systemName: game.isFavorite ? "heart.fill" : "heart")
                        .font(.system(size: 17, weight: .semibold))
                        .foregroundColor(game.isFavorite ? EspressUTheme.alertText : EspressUTheme.brownMid)
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .accessibilityLabel(game.isFavorite ? "Remove from favourites" : "Add to favourites")
            }
            .padding(.leading, 10)
            .padding(.trailing, 4)
            .padding(.vertical, 8),
            radius: 14
        )
    }
}

struct LibraryViewMenu: View {
    @AppStorage(LibraryCardStyle.storageKey) private var styleRaw = LibraryCardStyle.defaultValue.rawValue
    @AppStorage(LibraryGrouping.storageKey) private var groupingRaw = LibraryGrouping.defaultValue.rawValue
    @AppStorage(LibraryFilter.storageKey) private var filterRaw = LibraryFilter.defaultValue.rawValue
    @AppStorage(SettingsMode.storageKey) private var modeRaw = SettingsMode.defaultValue.rawValue

    var body: some View {
        Menu {
            Section("Show games as") {
                ForEach(LibraryCardStyle.choices) { style in
                    Button { styleRaw = style.rawValue } label: {
                        Label(style.title, systemImage: styleRaw == style.rawValue ? "checkmark" : style.systemImage)
                    }
                }
            }
            if SettingsMode.isAdvanced(raw: modeRaw) {
                Section("Group by") {
                    ForEach(LibraryGrouping.allCases) { grouping in
                        Button { groupingRaw = grouping.rawValue } label: {
                            Label(grouping.title, systemImage: groupingRaw == grouping.rawValue ? "checkmark" : grouping.systemImage)
                        }
                    }
                }
                Section("Show") {
                    ForEach(LibraryFilter.allCases) { filter in
                        Button { filterRaw = filter.rawValue } label: {
                            Label(filter.title, systemImage: filterRaw == filter.rawValue ? "checkmark" : filter.systemImage)
                        }
                    }
                }
            }
        } label: {
            Image(systemName: (LibraryCardStyle(rawValue: styleRaw) ?? .standard).systemImage)
                .font(.system(size: 20, weight: .semibold))
                .foregroundColor(EspressUTheme.brownMid)
                .frame(width: 44, height: 44)
        }
        .accessibilityLabel("Library layout")
    }
}

struct LibraryInstallLine: View {
    let text: String
    let size: CGFloat
    var centered = false

    var body: some View {
        Text(text)
            .font(.system(size: size, weight: .medium, design: .rounded))
            .foregroundColor(EspressUTheme.brownMid)
            .lineLimit(2)
            .multilineTextAlignment(centered ? .center : .leading)
            .frame(maxWidth: centered ? .infinity : nil, alignment: centered ? .center : .leading)
    }
}

struct LibraryExternalLine: View {
    let game: GameMetadata
    let size: CGFloat
    var centered = false

    var body: some View {
        if game.isExternal {
            Label(game.isUnavailable ? ExternalLibrary.unavailableMessage : "External",
                  systemImage: game.isUnavailable ? "externaldrive.badge.exclamationmark" : "externaldrive")
                .font(.system(size: size, weight: .medium, design: .rounded))
                .foregroundColor(game.isUnavailable ? EspressUTheme.alertText : EspressUTheme.brownMid)
                .lineLimit(2)
                .multilineTextAlignment(centered ? .center : .leading)
                .frame(maxWidth: centered ? .infinity : nil, alignment: centered ? .center : .leading)
                .accessibilityLabel(game.isUnavailable ? ExternalLibrary.unavailableMessage : "Stored outside EspressU, played in place")
        }
    }
}

final class DuplicateInstalls {
    static let shared = DuplicateInstalls()
    private var labels: [String: String] = [:]

    func label(for id: String) -> String? { labels[id] }

    private static func format(of game: GameMetadata) -> String {
        let ext = (game.romPath as NSString).pathExtension.lowercased()
        switch ext {
        case "wua": return "WUA"
        case "wud": return "WUD"
        case "wux": return "WUX"
        case "rpx", "tmd": return "Folder"
        default: return ext.isEmpty ? "Folder" : ext.uppercased()
        }
    }

    private static func fileName(of game: GameMetadata) -> String {
        String(game.installKey.dropFirst("install:".count))
    }

    func update(_ games: [GameMetadata]) {
        var byTitle: [UInt64: [GameMetadata]] = [:]
        var byName: [String: [GameMetadata]] = [:]
        for game in games {
            if let t = game.titleId { byTitle[t, default: []].append(game) }
            let name = (game.displayTitle ?? game.title).lowercased()
            byName[name, default: []].append(game)
        }
        var result: [String: String] = [:]
        for game in games {
            var group: [String: GameMetadata] = [game.id: game]
            if let t = game.titleId { byTitle[t]?.forEach { group[$0.id] = $0 } }
            byName[(game.displayTitle ?? game.title).lowercased()]?.forEach { group[$0.id] = $0 }
            guard group.count > 1 else { continue }
            let peers = Array(group.values)
            var parts: [String] = []
            if Set(peers.map { $0.region ?? "" }).count > 1, let region = game.region { parts.append(region) }
            if Set(peers.map(Self.format(of:))).count > 1 { parts.append(Self.format(of: game)) }
            let line = parts.joined(separator: ", ")
            result[game.id] = line.isEmpty ? Self.fileName(of: game) : line
        }
        var counts: [String: Int] = [:]
        for (id, line) in result { counts[(games.first { $0.id == id }?.titleId.map(String.init) ?? "") + "|" + line, default: 0] += 1 }
        for game in games {
            guard let line = result[game.id] else { continue }
            let key = (game.titleId.map(String.init) ?? "") + "|" + line
            if (counts[key] ?? 0) > 1, line != Self.fileName(of: game) {
                result[game.id] = line + ", " + Self.fileName(of: game)
            }
        }
        labels = result
    }
}

