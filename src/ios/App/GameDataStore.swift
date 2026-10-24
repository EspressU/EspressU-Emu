// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import SwiftUI

struct GameTDBRelease: Codable, Hashable {
    var region: String
    var gameID: String
    var year: Int?
    var month: Int?
    var day: Int?

    var dateText: String? {
        guard let year, year > 0 else { return nil }
        var comps = DateComponents()
        comps.year = year
        if let month, month > 0 { comps.month = month }
        if let day, day > 0, month != nil { comps.day = day }
        guard let date = Calendar(identifier: .gregorian).date(from: comps) else { return String(year) }
        let f = DateFormatter()
        f.locale = Locale.current
        f.timeZone = TimeZone(identifier: "UTC")
        if comps.day != nil { f.dateStyle = .long; f.timeStyle = .none }
        else if comps.month != nil { f.setLocalizedDateFormatFromTemplate("MMMM y") }
        else { return String(year) }
        return f.string(from: date)
    }

    var sortKey: Int { (year ?? 9999) * 10000 + (month ?? 0) * 100 + (day ?? 0) }
}

struct GameTDBControl: Codable, Hashable {
    var type: String
    var required: Bool

    var title: String {
        switch type {
        case "pad": return "Wii U GamePad"
        case "procontroller": return "Wii U Pro Controller"
        case "wiimote": return "Wii Remote"
        case "nunchuk": return "Nunchuk"
        case "classiccontroller": return "Classic Controller"
        case "balanceboard": return "Wii Balance Board"
        case "motionplus": return "Wii MotionPlus"
        case "wheel": return "Wii Wheel"
        case "zapper": return "Wii Zapper"
        case "mii": return "Mii"
        case "keyboard": return "Keyboard"
        case "microphone": return "Microphone"
        case "camera": return "Camera"
        default: return type.capitalized
        }
    }
}

struct GameInfo: Codable {
    var id: String
    var name: String
    var productCode: String
    var region: String
    var languages: [String] = []
    var titles: [String: String] = [:]
    var synopsis: [String: String] = [:]
    var developer: String?
    var publisher: String?
    var genres: [String] = []
    var releases: [GameTDBRelease] = []
    var localPlayers: Int?
    var onlinePlayers: Int?
    var onlineFeatures: [String] = []
    var ratingType: String?
    var ratingValue: String?
    var ratingDescriptors: [String] = []
    var controls: [GameTDBControl] = []
    var romSize: Int64?

    var gameTdbURL: URL? { URL(string: "https://www.gametdb.com/WiiU/\(id)") }

    static var preferredLanguages: [String] {
        let first = Locale.preferredLanguages.first ?? "en"
        let lower = first.lowercased()
        var device = (lower.split(separator: "-").first.map(String.init) ?? "en").uppercased()
        if lower.hasPrefix("zh") { device = lower.contains("hant") || lower.contains("tw") || lower.contains("hk") ? "ZHTW" : "ZHCN" }
        return device == "EN" ? ["EN"] : [device, "EN"]
    }

    var bestSynopsis: (language: String, text: String)? {
        for lang in Self.preferredLanguages {
            if let t = synopsis[lang], !t.isEmpty { return (lang, t) }
        }
        return synopsis.sorted { $0.key < $1.key }.first { !$0.value.isEmpty }.map { ($0.key, $0.value) }
    }

    var displayTitle: String {
        for lang in Self.preferredLanguages { if let t = titles[lang], !t.isEmpty { return t } }
        return titles["EN"] ?? name
    }
}

struct GameMatch: Codable, Equatable {
    var tdbID: String
    var confidence: Double
    var method: String
    var manual: Bool { method == "manual" }

    var methodText: String {
        switch method {
        case "titleId": return "Exact title ID"
        case "productCode": return "Product code"
        case "title": return "Title and region"
        case "manual": return "Chosen by you"
        default: return method
        }
    }
    var confidenceText: String {
        switch confidence {
        case 0.9...: return "High"
        case 0.75..<0.9: return "Good"
        default: return "Low"
        }
    }
}

struct TitleIndexRecord: Codable {
    var id: String
    var region: String
    var titles: [String]
}

struct ScrapeTarget {
    let gameID: String
    let derivedID: String?
    let manualID: String?
    let regions: Set<RegionCode>
    let titles: [String]
}

private struct GameDataFile: Codable {
    var version = 1
    var scrapedAt: Date?
    var databaseVersion: String?
    var matches: [String: GameMatch] = [:]
    var info: [String: GameInfo] = [:]
}

final class GameDataStore: ObservableObject {
    static let shared = GameDataStore()

    static let databaseURL = URL(string: "https://www.gametdb.com/wiiutdb.zip")!

    @Published private(set) var isScraping = false
    @Published private(set) var phase = ""
    @Published private(set) var fraction = 0.0
    @Published private(set) var scrapeError: String?
    @Published private(set) var lastScrape: Date?
    @Published private(set) var revision = 0
    @Published private(set) var lastSummary: String?

    private let lock = NSLock()
    private var file = GameDataFile()
    private var titleIndex: [TitleIndexRecord] = []

    private func storeScrape(_ completedFile: GameDataFile, index: [TitleIndexRecord]) {
        lock.lock()
        defer { lock.unlock() }
        file = completedFile
        titleIndex = index
    }

    private var cancelRequested = false
    private static let overridesKey = "espressu.gamedata.manualOverrides"
    private static let lastAttemptKey = "espressu.gamedata.lastAutoAttempt"

    private static var dataURL: URL { ArtLocations.dataDirectory.appendingPathComponent("gamedata.json") }
    private static var indexURL: URL { ArtLocations.dataDirectory.appendingPathComponent("titleindex.json") }

    private init() {
        if let data = try? Data(contentsOf: Self.dataURL), let f = try? JSONDecoder.iso.decode(GameDataFile.self, from: data) {
            file = f
            lastScrape = f.scrapedAt
        }
        if let data = try? Data(contentsOf: Self.indexURL), let idx = try? JSONDecoder().decode([TitleIndexRecord].self, from: data) {
            titleIndex = idx
        }
    }

    func match(for gameID: String) -> GameMatch? {
        lock.lock(); defer { lock.unlock() }
        return file.matches[gameID]
    }

    func info(for gameID: String) -> GameInfo? {
        lock.lock(); defer { lock.unlock() }
        guard let m = file.matches[gameID] else { return nil }
        return file.info[m.tdbID]
    }

    var hasTitleIndex: Bool { lock.lock(); defer { lock.unlock() }; return !titleIndex.isEmpty }

    var matchedCount: Int { lock.lock(); defer { lock.unlock() }; return file.matches.count }

    func manualOverride(for gameID: String) -> String? {
        (UserDefaults.standard.dictionary(forKey: Self.overridesKey) as? [String: String])?[gameID]
    }

    func coverTdbID(gameID: String, derived: String?) -> String? {
        if let m = manualOverride(for: gameID) { return m }
        if let derived { return derived }
        if let m = match(for: gameID), m.confidence >= 0.65 { return m.tdbID }
        return nil
    }

    func search(_ query: String, preferring regions: Set<RegionCode>, limit: Int = 40) -> [TitleIndexRecord] {
        let q = ArtTitleNormalizer.normalize(query)
        guard !q.isEmpty else { return [] }
        let words = q.split(separator: " ").map(String.init)
        lock.lock(); let index = titleIndex; lock.unlock()
        var scored: [(Int, TitleIndexRecord)] = []
        for rec in index {
            var best = 0
            for t in rec.titles {
                let n = ArtTitleNormalizer.normalize(t)
                var s = 0
                if n == q { s = 100 } else if n.hasPrefix(q) { s = 80 } else if n.contains(q) { s = 60 }
                else if words.count > 1, words.allSatisfy({ n.contains($0) }) { s = 40 }
                best = max(best, s)
            }
            if best == 0 && rec.id.lowercased().hasPrefix(query.lowercased()) && query.count >= 3 { best = 50 }
            guard best > 0 else { continue }
            if !regions.isEmpty, !RegionCode.codes(in: rec.region).isDisjoint(with: regions) { best += 5 }
            scored.append((best, rec))
        }
        return scored.sorted { $0.0 != $1.0 ? $0.0 > $1.0 : ($0.1.titles.first ?? "") < ($1.1.titles.first ?? "") }
            .prefix(limit).map { $0.1 }
    }

    func setManualOverride(_ tdbID: String?, for gameID: String) {
        var dict = (UserDefaults.standard.dictionary(forKey: Self.overridesKey) as? [String: String]) ?? [:]
        dict[gameID] = tdbID
        UserDefaults.standard.set(dict, forKey: Self.overridesKey)
    }

    func cancelScrape() {
        lock.lock(); cancelRequested = true; lock.unlock()
    }

    static func targets(for games: [GameMetadata]) -> [ScrapeTarget] {
        games.map { game in
            let hold = (game.isExternal && !game.isUnavailable) ? game.externalLocationID.flatMap { ExternalLibrary.shared.acquire($0) } : nil
            defer { if let hold { ExternalLibrary.shared.release(hold) } }
            let derived = CoverArtFetcher.deriveGameTdbId(romPath: game.romPath)
            var regions = RegionCode.codes(in: game.region)
            if regions.isEmpty, let derived, let r = RegionCode.code(forGameTdbId: derived) { regions = [r] }
            var seen = Set<String>()
            let titles = [game.displayTitle, game.title, game.cardName.name].compactMap { $0 }
                .map(ArtTitleNormalizer.normalize).filter { !$0.isEmpty && seen.insert($0).inserted }
            return ScrapeTarget(gameID: game.id, derivedID: derived, manualID: shared.manualOverride(for: game.id),
                                regions: regions, titles: titles)
        }
    }

    func scrapeIfNeeded(games: [GameMetadata]) {
        guard !isScraping, !games.isEmpty else { return }
        let now = Date().timeIntervalSince1970
        let unmatched = games.contains { match(for: $0.id) == nil }
        let last = UserDefaults.standard.double(forKey: Self.lastAttemptKey)
        let wait: TimeInterval = unmatched ? 6 * 3600 : 7 * 24 * 3600
        guard now - last > wait else { return }
        UserDefaults.standard.set(now, forKey: Self.lastAttemptKey)
        scrape(games: games)
    }

    func scrape(games: [GameMetadata]) {
        guard !isScraping else { return }
        isScraping = true
        scrapeError = nil
        lastSummary = nil
        phase = "Starting"
        fraction = 0
        lock.lock(); cancelRequested = false; lock.unlock()
        Task.detached(priority: .utility) { [weak self] in
            guard let self else { return }
            let targets = Self.targets(for: games)
            do {
                let summary = try await self.runScrape(targets: targets)
                await MainActor.run {
                    self.isScraping = false
                    self.lastSummary = summary
                    self.revision += 1
                    NotificationCenter.default.post(name: .espressuCoverArtSourcesChanged, object: nil)
                }
            } catch {
                await MainActor.run {
                    self.isScraping = false
                    self.scrapeError = error is CancellationError ? "Cancelled." : error.localizedDescription
                }
            }
        }
    }

    private func report(_ phase: String, _ fraction: Double) {
        DispatchQueue.main.async { self.phase = phase; self.fraction = fraction }
    }

    private var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelRequested }

    private func runScrape(targets: [ScrapeTarget]) async throws -> String {
        let fm = FileManager.default
        report("Downloading GameTDB database", 0.02)
        var request = URLRequest(url: Self.databaseURL)
        request.setValue("EspressU", forHTTPHeaderField: "User-Agent")
        request.timeoutInterval = 60
        let (tmp, response) = try await URLSession.shared.download(for: request)
        defer { try? fm.removeItem(at: tmp) }
        if let http = response as? HTTPURLResponse, http.statusCode != 200 { throw GameDataError.http(http.statusCode) }
        if isCancelled { throw CancellationError() }

        report("Unpacking", 0.2)
        let archive = try MiniZip(url: tmp)
        guard let entry = archive.entries.first(where: { ($0.name as NSString).lastPathComponent == "wiiutdb.xml" }) else {
            throw GameDataError.failed("The GameTDB download didn't contain wiiutdb.xml.")
        }
        let xmlURL = fm.temporaryDirectory.appendingPathComponent("wiiutdb-\(UUID().uuidString).xml")
        try archive.read(entry).write(to: xmlURL, options: .atomic)
        defer { try? fm.removeItem(at: xmlURL) }
        if isCancelled { throw CancellationError() }

        report("Reading entries", 0.25)
        let matcher = GameTDBMatcher(targets: targets)
        let reader = WiiUTDBReader { [weak self] game, count, total in
            matcher.consume(game)
            if count % 100 == 0 { self?.report("Reading entries", 0.25 + 0.7 * Double(count) / Double(max(total, 1))) }
        }
        guard let stream = InputStream(url: xmlURL) else { throw GameDataError.failed("Couldn't open the GameTDB database.") }
        let parser = XMLParser(stream: stream)
        parser.delegate = reader
        reader.shouldAbort = { [weak self] in self?.isCancelled ?? false }
        let ok = parser.parse()
        if isCancelled { throw CancellationError() }
        if !ok && reader.gamesRead == 0 { throw GameDataError.failed("The GameTDB database couldn't be read.") }

        report("Saving", 0.97)
        let result = matcher.finish()
        var newFile = GameDataFile()
        newFile.scrapedAt = Date()
        newFile.databaseVersion = reader.databaseVersion
        newFile.matches = result.matches
        newFile.info = result.info
        try JSONEncoder.iso.encode(newFile).write(to: Self.dataURL, options: .atomic)
        try JSONEncoder().encode(result.titleIndex).write(to: Self.indexURL, options: .atomic)
        let completedFile = newFile
        storeScrape(completedFile, index: result.titleIndex)
        await MainActor.run { self.lastScrape = completedFile.scrapedAt }

        let matched = result.matches.count
        return "Matched \(matched) of \(targets.count) games from \(reader.gamesRead) database entries."
    }
}

final class GameTDBMatcher {
    private struct Best {
        var score: Int
        var info: GameInfo
        var confidence: Double
        var method: String
    }

    private let targets: [ScrapeTarget]
    private var best: [String: Best] = [:]
    private var releasesByCode: [String: [GameTDBRelease]] = [:]
    private var titleIndex: [TitleIndexRecord] = []

    init(targets: [ScrapeTarget]) { self.targets = targets }

    func consume(_ game: GameInfo) {
        releasesByCode[game.productCode, default: []].append(contentsOf: game.releases)
        let allTitles = Array(Set(game.titles.values.filter { !$0.isEmpty } + [game.name]))
        titleIndex.append(TitleIndexRecord(id: game.id, region: game.region, titles: allTitles))

        let entryRegions = RegionCode.codes(in: game.region)
        var normalized: Set<String>?
        for target in targets {
            let regionMatch = !target.regions.isEmpty && !target.regions.isDisjoint(with: entryRegions)
            var score = 0, confidence = 0.0, method = ""
            if let manual = target.manualID {
                guard game.id == manual else { continue }
                score = 1000; confidence = 1; method = "manual"
            } else if let derived = target.derivedID, game.id == derived {
                score = 100; confidence = 1; method = "titleId"
            } else if let derived = target.derivedID, game.productCode == String(derived.prefix(4)) {
                score = regionMatch ? 95 : 90; confidence = regionMatch ? 0.92 : 0.85; method = "productCode"
            } else {
                if normalized == nil { normalized = Set(allTitles.map(ArtTitleNormalizer.normalize).filter { !$0.isEmpty }) }
                guard let set = normalized, target.titles.contains(where: { set.contains($0) }) else { continue }
                score = regionMatch ? 78 : 70; confidence = regionMatch ? 0.78 : 0.65; method = "title"
            }
            if best[target.gameID].map({ score > $0.score }) ?? true {
                best[target.gameID] = Best(score: score, info: game, confidence: confidence, method: method)
            }
        }
    }

    func finish() -> (matches: [String: GameMatch], info: [String: GameInfo], titleIndex: [TitleIndexRecord]) {
        var matches: [String: GameMatch] = [:]
        var info: [String: GameInfo] = [:]
        for (gameID, b) in best {
            var entry = b.info
            var releases = Set(releasesByCode[entry.productCode] ?? [])
            releases.formUnion(entry.releases)
            entry.releases = releases.sorted { $0.sortKey < $1.sortKey }
            matches[gameID] = GameMatch(tdbID: entry.id, confidence: b.confidence, method: b.method)
            info[entry.id] = entry
        }
        return (matches, info, titleIndex)
    }
}

final class WiiUTDBReader: NSObject, XMLParserDelegate {
    private let onGame: (GameInfo, Int, Int) -> Void
    var shouldAbort: () -> Bool = { false }
    private(set) var gamesRead = 0
    private(set) var databaseVersion: String?
    private var totalGames = 0

    private var cur: GameInfo?
    private var curName = ""
    private var text = ""
    private var locale: String?
    private var inRating = false
    private var inWifi = false
    private var entryDate: (Int?, Int?, Int?) = (nil, nil, nil)
    private var entryRegion = ""

    init(onGame: @escaping (GameInfo, Int, Int) -> Void) { self.onGame = onGame }

    func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?,
                qualifiedName qName: String?, attributes a: [String: String] = [:]) {
        text = ""
        switch elementName {
        case "WiiUTDB":
            databaseVersion = a["version"]
            totalGames = Int(a["games"] ?? "") ?? 0
        case "game":
            curName = a["name"] ?? ""
            cur = GameInfo(id: "", name: curName, productCode: "", region: "")
            entryDate = (nil, nil, nil)
            entryRegion = ""
            inRating = false; inWifi = false; locale = nil
        case "locale": locale = a["lang"]
        case "date": entryDate = (Int(a["year"] ?? ""), Int(a["month"] ?? ""), Int(a["day"] ?? ""))
        case "rating":
            inRating = true
            if let t = a["type"], !t.isEmpty { cur?.ratingType = t }
            if let v = a["value"], !v.isEmpty { cur?.ratingValue = v }
        case "wi-fi":
            inWifi = true
            if let p = Int(a["players"] ?? ""), p > 0 { cur?.onlinePlayers = p }
        case "input":
            if let p = Int(a["players"] ?? ""), p > 0 { cur?.localPlayers = p }
        case "control":
            if let t = a["type"] { cur?.controls.append(GameTDBControl(type: t, required: a["required"] == "true")) }
        case "rom":
            if let s = a["size"], let n = Int64(s), n > 0 { cur?.romSize = n }
        default: break
        }
    }

    func parser(_ parser: XMLParser, foundCharacters string: String) { text += string }

    func parser(_ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?, qualifiedName qName: String?) {
        let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
        switch elementName {
        case "id": cur?.id = value; cur?.productCode = String(value.prefix(4))
        case "region": entryRegion = value
        case "languages": cur?.languages = value.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        case "title": if let l = locale, !value.isEmpty { cur?.titles[l] = value }
        case "synopsis": if let l = locale, !value.isEmpty { cur?.synopsis[l] = value }
        case "locale": locale = nil
        case "developer": if !value.isEmpty { cur?.developer = value }
        case "publisher": if !value.isEmpty { cur?.publisher = value }
        case "genre": cur?.genres = value.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces).capitalized }.filter { !$0.isEmpty }
        case "descriptor": if inRating, !value.isEmpty { cur?.ratingDescriptors.append(value) }
        case "feature": if inWifi, !value.isEmpty { cur?.onlineFeatures.append(value) }
        case "rating": inRating = false
        case "wi-fi": inWifi = false
        case "game":
            guard var game = cur, !game.id.isEmpty else { cur = nil; return }
            let regions = RegionCode.codes(in: entryRegion)
            let code = regions.first ?? RegionCode.code(forGameTdbId: game.id)
            game.region = code?.rawValue ?? entryRegion
            if game.name.isEmpty || game.name.contains("(") { game.name = game.titles["EN"] ?? game.titles.values.first ?? game.name }
            let rel = GameTDBRelease(region: game.region, gameID: game.id, year: entryDate.0, month: entryDate.1, day: entryDate.2)
            game.releases = rel.year == nil ? [] : [rel]
            if game.releases.isEmpty { game.releases = [GameTDBRelease(region: game.region, gameID: game.id)] }
            cur = nil
            gamesRead += 1
            onGame(game, gamesRead, totalGames)
            if shouldAbort() { parser.abortParsing() }
        default: break
        }
        text = ""
    }
}

extension JSONDecoder {
    static var iso: JSONDecoder { let d = JSONDecoder(); d.dateDecodingStrategy = .iso8601; return d }
}
extension JSONEncoder {
    static var iso: JSONEncoder { let e = JSONEncoder(); e.dateEncodingStrategy = .iso8601; return e }
}

enum GameDataError: LocalizedError {
    case http(Int)
    case failed(String)

    var errorDescription: String? {
        switch self {
        case .http(let code): return "The download server answered with error \(code)."
        case .failed(let message): return message
        }
    }
}

extension Notification.Name {
    static let espressuCoverArtSourcesChanged = Notification.Name("espressu.coverArtSourcesChanged")
}
