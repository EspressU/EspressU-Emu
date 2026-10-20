import Foundation
import SwiftUI
#if os(iOS)
import UIKit
#endif

struct GameMetadata: Codable, Identifiable {
    let id: String
    let title: String
    let romPath: String
    var coverPath: String?
    var region: String?
    let releaseDate: String
    let genre: String
    var isFavorite: Bool = false
    var titleId: UInt64?
    var dumpDirectoryPath: String?
    var displayTitle: String?
    var addedDate: Date? = nil
    var externalLocationID: String? = nil
    var isUnavailable: Bool = false
    var isExternal: Bool { externalLocationID != nil }

    var sortTitle: String { LibraryCustomNames.shared.name(for: installKey) ?? title }

    var installKey: String {
        isExternal ? "install:" + id : Self.installKey(forRomPath: romPath, fallback: id)
    }

    var removeActionTitle: String { isExternal ? "Remove from library\u{2026}" : "Remove game\u{2026}" }
    var removeConfirmTitle: String { isExternal ? "Remove this game from your library?" : "Remove this game?" }
    var removeConfirmButton: String { isExternal ? "Remove from library" : "Remove game" }
    var removeConfirmMessage: String {
        if isExternal {
            return "\"\(cardName.name)\" is only linked, so its file stays where it is and isn't deleted. Your saves and options are kept. You can bring it back from Settings > Library."
        }
        return "This deletes \"\(cardName.name)\" from EspressU to free up space. Your saves and options are kept. To play it again, add the game back."
    }

    static func installKey(forRomPath romPath: String, fallback: String) -> String {
        let marker = "/Roms/"
        guard let range = romPath.range(of: marker, options: .backwards) else { return "install:" + fallback }
        let rest = romPath[range.upperBound...]
        let first = rest.split(separator: "/", maxSplits: 1, omittingEmptySubsequences: true).first.map(String.init)
        return "install:" + (first ?? fallback)
    }

    var installLabel: String? { DuplicateInstalls.shared.label(for: id) }

    var settingsKey: String {
        titleId.map(Self.settingsKey(forTitleId:)) ?? id
    }

    static func settingsKey(forTitleId titleId: UInt64) -> String {
        String(format: "%016llX", titleId)
    }

    var cardName: (name: String, titleIdText: String?) {
        let name = LibraryCustomNames.shared.name(for: installKey) ?? displayTitle ?? title
        guard let titleId else { return (name, nil) }
        let hex = Self.settingsKey(forTitleId: titleId)
        return CardNameCache.shared.cardName(for: name, hex: hex)
    }

    enum CodingKeys: String, CodingKey {
        case id, title, romPath, coverPath, region, releaseDate, genre, titleId, dumpDirectoryPath, displayTitle
    }
}

final class CardNameCache: @unchecked Sendable {
    static let shared = CardNameCache()

    private final class Entry {
        let name: String
        let titleIdText: String?
        init(name: String, titleIdText: String?) { self.name = name; self.titleIdText = titleIdText }
    }

    private let entries = NSCache<NSString, Entry>()
    private let separators = CharacterSet.whitespaces.union(CharacterSet(charactersIn: "-\u{2013}\u{2014}_.,:;|"))

    func cardName(for name: String, hex: String) -> (name: String, titleIdText: String?) {
        let key = "\(hex)|\(name)" as NSString
        if let hit = entries.object(forKey: key) { return (hit.name, hit.titleIdText) }
        let result = compute(name: name, hex: hex)
        entries.setObject(Entry(name: result.name, titleIdText: result.titleIdText), forKey: key)
        return result
    }

    private func compute(name: String, hex: String) -> (name: String, titleIdText: String?) {
        let pattern = "[\\[({]?\\s*(?<![0-9A-Fa-f])" + String(hex.prefix(8)) + "[-_ ]?" + String(hex.suffix(8)) + "(?![0-9A-Fa-f])\\s*[\\])}]?"
        guard let regex = try? NSRegularExpression(pattern: pattern, options: [.caseInsensitive]),
              let match = regex.firstMatch(in: name, range: NSRange(name.startIndex..., in: name)),
              let range = Range(match.range, in: name) else { return (name, nil) }
        var before = String(name[..<range.lowerBound])
        var after = String(name[range.upperBound...])
        while let last = before.unicodeScalars.last, separators.contains(last) { before.unicodeScalars.removeLast() }
        while let first = after.unicodeScalars.first, separators.contains(first) { after.unicodeScalars.removeFirst() }
        let removed = name[range]
        for (open, close) in [("(", ")"), ("[", "]"), ("{", "}")] {
            let opens = before.filter { String($0) == open }.count
            let closes = before.filter { String($0) == close }.count
            if opens > closes, removed.contains(close) { before += close }
        }
        let cleaned: String
        if before.isEmpty { cleaned = after } else if after.isEmpty { cleaned = before } else { cleaned = before + " " + after }
        return cleaned.isEmpty ? (name, nil) : (cleaned, hex)
    }
}

enum ImportState: Equatable {
    case idle
    case copying(name: String)
}

enum LibraryMetadataCache {
    private static let regionKey = "espressu.library.regionByGameID"
    private static let titleNameKey = "espressu.library.titleNameByGameID"
    private static let versionKey = "espressu.library.metadataCacheVersion"
    private static let titleIdKey = "espressu.library.titleIdByGameID"
    private static let currentVersion = 2

    static func discardEntriesFromBeforeVersion2() {
        let defaults = UserDefaults.standard
        guard defaults.integer(forKey: versionKey) < currentVersion else { return }
        defaults.removeObject(forKey: regionKey)
        defaults.removeObject(forKey: titleNameKey)
        defaults.set(currentVersion, forKey: versionKey)
    }

    static func cachedRegion(for gameID: String) -> String? {
        (UserDefaults.standard.dictionary(forKey: regionKey) as? [String: String])?[gameID]
    }

    static func setCachedRegion(_ region: String?, for gameID: String) {
        var stored = (UserDefaults.standard.dictionary(forKey: regionKey) as? [String: String]) ?? [:]
        stored[gameID] = region ?? ""
        UserDefaults.standard.set(stored, forKey: regionKey)
    }

    static func cachedTitleId(for gameID: String) -> UInt64? {
        ((UserDefaults.standard.dictionary(forKey: titleIdKey) as? [String: String])?[gameID]).flatMap { UInt64($0, radix: 16) }
    }

    static func setCachedTitleId(_ titleId: UInt64, for gameID: String) {
        var stored = (UserDefaults.standard.dictionary(forKey: titleIdKey) as? [String: String]) ?? [:]
        let text = GameMetadata.settingsKey(forTitleId: titleId)
        guard stored[gameID] != text else { return }
        stored[gameID] = text
        UserDefaults.standard.set(stored, forKey: titleIdKey)
    }

    static func cachedTitleName(for gameID: String) -> String? {
        (UserDefaults.standard.dictionary(forKey: titleNameKey) as? [String: String])?[gameID]
    }

    static func setCachedTitleName(_ name: String?, for gameID: String) {
        var stored = (UserDefaults.standard.dictionary(forKey: titleNameKey) as? [String: String]) ?? [:]
        stored[gameID] = name ?? ""
        UserDefaults.standard.set(stored, forKey: titleNameKey)
    }
}

@MainActor
class GameManager: ObservableObject {
    @Published private(set) var isQuitting = false
    @Published var games: [GameMetadata] = [] {
        didSet {
            DuplicateInstalls.shared.update(games)
            LibraryCustomNames.shared.migrateLegacyKeys(games: games)
        }
    }
    @Published var favorites: [GameMetadata] = []
    @Published var isLoading = false
    @Published var currentGame: GameMetadata?
    @Published var emulationState: EmulationState = .idle {
        didSet { ScreenBrightness.shared.setGameRunning(emulationState == .running) }
    }
    @Published var lastStatusMessage: String = ""
    @Published var launchNotice: String?
    @Published private(set) var needsCleanRestart = false
    @Published private(set) var titleEndedByEngine = false
    @Published private(set) var frameRate: Int = 0
    @Published private(set) var videoStalled = false
    @Published private(set) var videoStallKind = 0
    @Published private(set) var progress = EmulatorProgress()
    @Published private(set) var importState: ImportState = .idle
    var confirmOverwrite: ((String) async -> Bool)?
    private var frameRateTimer: Timer?
    private var externalHold: ExternalLibrary.Hold?
    private var driveCheckInFlight = false
    @Published var launchBlockedMessage: String?
    @Published private(set) var driveWasLost = false

    private let romsDirectory = "Roms"
    private var didSweepStaging = false
    private let gameListFile = "games.json"
    private var emulationEngine: EmulationEngine?
    private var surfaceRegistered = false
    private static let favoriteIDsKey = "espressu.library.favoriteGameIDs"

    init() {
        cemu_bridge_set_title_switch_callback { titleId in
            TitleSwitchSettings.apply(titleId: titleId)
        }
        ActiveGameSettings.end()
        emulationEngine = EmulationEngine()
        ThermalMonitor.shared.onNotice = { [weak self] text in
            guard let self, self.emulationState == .running else { return }
            self.showLaunchNotice(text)
        }
        Task {
            await loadGames()
        }
        coverSourcesObserver = NotificationCenter.default.addObserver(
            forName: .espressuCoverArtSourcesChanged, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor [weak self] in self?.refreshAllCoverPaths() }
        }
    }

    private var coverSourcesObserver: NSObjectProtocol?

    func refreshAllCoverPaths() {
        for game in games { refreshCoverPath(forGameID: game.id) }
        enrichMissingCoverArt()
    }

    func loadGames(showSpinner: Bool = true) async {
        if showSpinner { isLoading = true }
        defer { if showSpinner { isLoading = false } }

        let fileManager = FileManager.default
        guard let documentsPath = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first else {
            return
        }

        let romsPath = documentsPath.appendingPathComponent(romsDirectory)

        try? fileManager.createDirectory(at: romsPath, withIntermediateDirectories: true)

        WiiUKeys.ensureDirectoryExists()

        let sweepStaging = !didSweepStaging
        didSweepStaging = true
        let discovered = await Task.detached(priority: .userInitiated) {
            Self.scanRoms(romsPath: romsPath, sweepStaging: sweepStaging)
        }.value
        guard let discovered else {
            print("Error scanning Roms directory")
            return
        }
        let sortedDiscovered = discovered.sorted { $0.title < $1.title }
        if !showSpinner, sortedDiscovered.map({ $0.id + "|" + $0.romPath + "|" + ($0.isUnavailable ? "x" : "") }) == games.map({ $0.id + "|" + $0.romPath + "|" + ($0.isUnavailable ? "x" : "") }) { return }
        self.games = sortedDiscovered
        PerGameKeyMigration.run(games: self.games)
        self.favorites = self.games.filter { $0.isFavorite }
        enrichMissingCoverArt()
    }

    func rescanIfIdle() async {
        guard emulationState == .idle, !isLoading, importState == .idle else { return }
        await loadGames(showSpinner: false)
    }

    func linkLocation(_ picked: URL) async throws {
        _ = try await Task.detached { try ExternalLibrary.shared.link(picked) }.value
        await loadGames(showSpinner: false)
    }

    func unlinkLocation(_ id: String) async {
        guard emulationState == .idle else { return }
        ExternalLibrary.shared.unlink(id)
        await loadGames(showSpinner: false)
    }

    func restoreHiddenGames(in id: String) async {
        ExternalLibrary.shared.restoreHidden(id)
        await loadGames(showSpinner: false)
    }

    enum RemoveGameError: LocalizedError {
        case notInLibrary
        case failed(Error)

        var errorDescription: String? {
            switch self {
            case .notInLibrary: return "Couldn't find that game's files."
            case .failed(let error): return "Couldn't remove the game: \(error.localizedDescription)"
            }
        }
    }

    func removeGame(_ game: GameMetadata) async throws {
        guard emulationState == .idle, importState == .idle else { return }
        if let locationID = game.externalLocationID {
            ExternalLibrary.shared.removeFromLibrary(locationID: locationID, gameID: game.id)
            await loadGames(showSpinner: false)
            return
        }
        guard let roms = romsDirectoryURL?.resolvingSymlinksInPath() else { throw RemoveGameError.notInLibrary }
        var item = URL(fileURLWithPath: game.romPath).resolvingSymlinksInPath()
        while item.deletingLastPathComponent().path != roms.path {
            let parent = item.deletingLastPathComponent()
            guard parent.path != item.path, parent.path.hasPrefix(roms.path) else { throw RemoveGameError.notInLibrary }
            item = parent
        }
        guard item.path != roms.path, FileManager.default.fileExists(atPath: item.path) else { throw RemoveGameError.notInLibrary }
        let target = item
        do {
            try await Task.detached { try FileManager.default.removeItem(at: target) }.value
        } catch {
            throw RemoveGameError.failed(error)
        }
        await loadGames(showSpinner: false)
    }

    private nonisolated static func scanRoms(romsPath: URL, sweepStaging: Bool) -> [GameMetadata]? {
        let fileManager = FileManager.default
        LibraryMetadataCache.discardEntriesFromBeforeVersion2()

        if sweepStaging {
            let staging = romsPath.appendingPathComponent(stagingDirectoryName)
            try? fileManager.removeItem(at: staging)
            let dlcStaging = romsPath.deletingLastPathComponent().appendingPathComponent("mlc").appendingPathComponent(".incoming-dlcupdate")
            try? fileManager.removeItem(at: dlcStaging)
        }

        DlcUpdateImport.migrateMisplacedContentIfNeeded()
        GameSaveTransfer.migrateMisplacedSavesIfNeeded()

        guard let contents = try? fileManager.contentsOfDirectory(at: romsPath, includingPropertiesForKeys: nil) else {
            return nil
        }
        let mlcFolder = romsPath.deletingLastPathComponent().appendingPathComponent("mlc").path
        mlcFolder.withCString { cemu_bridge_prepare_keys_before_init($0) }
        _ = cemu_bridge_reload_and_count_keys()

        let sortedContents = contents.sorted { $0.lastPathComponent < $1.lastPathComponent }

        struct ScanItem { let url: URL; let locationID: String?; let root: URL? }
        var work: [ScanItem] = sortedContents.map { ScanItem(url: $0, locationID: nil, root: nil) }
        var unavailableLocations: [LinkedLocation] = []
        var holds: [ExternalLibrary.Hold] = []
        defer { holds.forEach { ExternalLibrary.shared.release($0) } }
        var seenExternalItems = Set<String>()
        var availability: [String: Bool] = [:]
        for location in ExternalLibrary.shared.snapshot() {
            guard let hold = ExternalLibrary.shared.acquire(location.id, reresolve: true) else {
                unavailableLocations.append(location)
                availability[location.id] = false
                continue
            }
            guard let items = ExternalLibrary.shared.discover(location, root: hold.url) else {
                ExternalLibrary.shared.release(hold)
                unavailableLocations.append(location)
                availability[location.id] = false
                continue
            }
            holds.append(hold)
            availability[location.id] = true
            for item in items {
                let rel = ExternalLibrary.relativePath(of: item, under: hold.url)
                if location.hidden.contains(rel) { continue }
                guard seenExternalItems.insert(item.path).inserted else { continue }
                work.append(ScanItem(url: item, locationID: location.id, root: hold.url))
            }
        }
        ExternalLibrary.shared.setAvailability(availability)
        var externalRecords: [String: [ExternalGameRecord]] = [:]

        var discoveredGames: [GameMetadata] = []
        var usedIDs = Set<String>()
        for scanItem in work {
            let item = scanItem.url
            var gameID: String
            let bootPath: String
            let dumpDirectory: URL?

            var isDirectory: ObjCBool = false
            _ = fileManager.fileExists(atPath: item.path, isDirectory: &isDirectory)

            if isDirectory.boolValue {
                if Self.looksLikeWiiUDump(item), let rpx = Self.executableInDump(item) {
                    gameID = item.lastPathComponent
                    bootPath = rpx.path
                    dumpDirectory = item
                } else if let tmd = Self.nusBaseTitleTmd(in: item) {
                    gameID = item.lastPathComponent
                    bootPath = tmd.path
                    dumpDirectory = tmd.deletingLastPathComponent()
                } else {
                    continue
                }
            } else {
                let pathExtension = item.pathExtension.lowercased()
                guard Self.supportedROMExtensions.contains(pathExtension) else { continue }
                gameID = item.deletingPathExtension().lastPathComponent
                bootPath = item.path
                dumpDirectory = nil
            }

            if !usedIDs.insert(gameID).inserted {
                var candidate = item.lastPathComponent
                var copy = 2
                while usedIDs.contains(candidate) {
                    candidate = "\(item.lastPathComponent) (\(copy))"
                    copy += 1
                }
                gameID = candidate
                usedIDs.insert(gameID)
            }

            let addedDate = (try? fileManager.attributesOfItem(atPath: item.path))?[.creationDate] as? Date

            let derivedTitleId = Self.deriveBaseTitleId(romPath: bootPath)
            if let derivedTitleId { LibraryMetadataCache.setCachedTitleId(derivedTitleId, for: gameID) }

            let gameMetadata = GameMetadata(
                id: gameID,
                title: gameID,
                romPath: bootPath,
                coverPath: Self.findCover(for: gameID, romPath: bootPath, in: romsPath),
                region: Self.nonEmptyOrNil(LibraryMetadataCache.cachedRegion(for: gameID)),
                releaseDate: "Unknown",
                genre: "Game",
                titleId: derivedTitleId ?? LibraryMetadataCache.cachedTitleId(for: gameID),
                dumpDirectoryPath: dumpDirectory?.path,
                displayTitle: Self.nonEmptyOrNil(LibraryMetadataCache.cachedTitleName(for: gameID)),
                addedDate: addedDate,
                externalLocationID: scanItem.locationID
            )

            discoveredGames.append(gameMetadata)
            if let locationID = scanItem.locationID, let root = scanItem.root {
                externalRecords[locationID, default: []].append(ExternalGameRecord(
                    id: gameID,
                    itemRel: ExternalLibrary.relativePath(of: item, under: root),
                    bootRel: ExternalLibrary.relativePath(of: URL(fileURLWithPath: bootPath), under: root),
                    titleId: gameMetadata.titleId))
            }
        }

        for location in ExternalLibrary.shared.snapshot() where availability[location.id] == true {
            ExternalLibrary.shared.updateRecords(location.id, externalRecords[location.id] ?? [], path: holds.first { $0.locationID == location.id }?.url.path ?? location.lastPath)
        }

        for location in unavailableLocations {
            for record in location.games where !location.hidden.contains(record.itemRel) {
                var gameID = record.id
                if usedIDs.contains(gameID) { gameID = (record.itemRel as NSString).lastPathComponent }
                guard usedIDs.insert(gameID).inserted else { continue }
                let bootPath = location.kind == .file ? location.lastPath : (location.lastPath as NSString).appendingPathComponent(record.bootRel)
                discoveredGames.append(GameMetadata(
                    id: gameID,
                    title: gameID,
                    romPath: bootPath,
                    coverPath: Self.findCover(for: gameID, romPath: bootPath, in: romsPath),
                    region: Self.nonEmptyOrNil(LibraryMetadataCache.cachedRegion(for: gameID)),
                    releaseDate: "Unknown",
                    genre: "Game",
                    titleId: record.titleId ?? LibraryMetadataCache.cachedTitleId(for: gameID),
                    dumpDirectoryPath: nil,
                    displayTitle: Self.nonEmptyOrNil(LibraryMetadataCache.cachedTitleName(for: gameID)),
                    addedDate: nil,
                    externalLocationID: location.id,
                    isUnavailable: true
                ))
            }
        }

        let favoriteIDs = Self.loadFavoriteIDs()
        for index in discoveredGames.indices {
            discoveredGames[index].isFavorite = favoriteIDs.contains(discoveredGames[index].id)
        }

        return discoveredGames
    }

    nonisolated static func looksLikeWiiUDump(_ directory: URL) -> Bool {
        let fileManager = FileManager.default
        var isDirectory: ObjCBool = false

        for required in ["code", "meta"] {
            let sub = directory.appendingPathComponent(required)
            guard fileManager.fileExists(atPath: sub.path, isDirectory: &isDirectory),
                  isDirectory.boolValue else {
                return false
            }
        }
        return true
    }

    nonisolated static func executableInDump(_ directory: URL) -> URL? {
        let codePath = directory.appendingPathComponent("code")
        let entries = (try? FileManager.default.contentsOfDirectory(
            at: codePath,
            includingPropertiesForKeys: nil
        )) ?? []

        return entries
            .filter { $0.pathExtension.lowercased() == "rpx" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
            .first
    }

    nonisolated static func titleTmdInDump(_ directory: URL) -> URL? {
        let entries = (try? FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: nil
        )) ?? []
        return entries.first { $0.lastPathComponent.caseInsensitiveCompare("title.tmd") == .orderedSame }
    }

    enum NUSFolderKind {
        case base
        case update
        case dlc

        var titleIdHighWord: UInt32 {
            switch self {
            case .base: return 0x00050000
            case .update: return 0x0005000E
            case .dlc: return 0x0005000C
            }
        }
    }

    nonisolated static func nusKind(ofTmd tmd: URL) -> NUSFolderKind {
        var titleId: UInt64 = 0
        let ok = tmd.path.withCString { cemu_bridge_read_tmd_title_id($0, &titleId) }
        guard ok else { return .base }
        switch UInt32(truncatingIfNeeded: titleId >> 32) {
        case NUSFolderKind.update.titleIdHighWord: return .update
        case NUSFolderKind.dlc.titleIdHighWord: return .dlc
        default: return .base
        }
    }

    nonisolated static func nusTitleFolders(in directory: URL, maxDepth: Int = 2) -> [(tmd: URL, kind: NUSFolderKind)] {
        var found: [(tmd: URL, kind: NUSFolderKind)] = []
        if let tmd = titleTmdInDump(directory) {
            found.append((tmd, nusKind(ofTmd: tmd)))
            return found
        }
        guard maxDepth > 0 else { return found }
        let subfolders = ((try? FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: [.isDirectoryKey]
        )) ?? [])
            .filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        for subfolder in subfolders {
            found.append(contentsOf: nusTitleFolders(in: subfolder, maxDepth: maxDepth - 1))
        }
        return found
    }

    nonisolated static func nusBaseTitleTmd(in directory: URL) -> URL? {
        nusTitleFolders(in: directory).first { $0.kind == .base }?.tmd
    }

    nonisolated static func nusSubfolder(in directory: URL, kind: NUSFolderKind) -> URL? {
        guard titleTmdInDump(directory) == nil else { return nil }
        return nusTitleFolders(in: directory).first { $0.kind == kind }?.tmd.deletingLastPathComponent()
    }

    nonisolated static func looksLikeNUSDump(_ directory: URL) -> Bool {
        nusBaseTitleTmd(in: directory) != nil
    }

    nonisolated static func deriveBaseTitleId(romPath: String) -> UInt64? {
        var titleId: UInt64 = 0
        let ok = romPath.withCString { cPath in
            cemu_bridge_derive_title_id(cPath, &titleId)
        }
        guard ok else { return nil }
        return cemu_bridge_derive_base_title_id(titleId)
    }

    static let manualCoverExtensions = ["jpg", "jpeg", "png"]

    private nonisolated static func findCover(for gameID: String, romPath: String, in directory: URL) -> String? {
        let fileManager = FileManager.default

        for ext in Self.manualCoverExtensions {
            let coverPath = directory.appendingPathComponent("\(gameID)_cover.\(ext)")
            if fileManager.fileExists(atPath: coverPath.path) {
                return coverPath.path
            }
        }

        let names = [LibraryMetadataCache.cachedTitleName(for: gameID), gameID].compactMap { $0 }.filter { !$0.isEmpty }
        if let path = CoverSourceChain.cachedPath(CoverContext(
            gameID: gameID, romPath: romPath, dumpDirectoryPath: nil, libraryDirectory: directory, currentCoverPath: nil,
            region: LibraryMetadataCache.cachedRegion(for: gameID), titles: names
        )) {
            return path
        }

        return nil
    }

    private func enrichMissingCoverArt() {
        let fileManager = FileManager.default
        guard let documentsPath = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        let romsPath = documentsPath.appendingPathComponent(romsDirectory)

        let candidates = games
        guard !candidates.isEmpty else { return }

        Task.detached { [weak self, romsPath] in
            for game in candidates {
                if game.isUnavailable { continue }
                var externalHold: ExternalLibrary.Hold?
                if let locationID = game.externalLocationID {
                    guard let hold = ExternalLibrary.shared.acquire(locationID) else { continue }
                    externalHold = hold
                }
                defer { if let externalHold { ExternalLibrary.shared.release(externalHold) } }
                let context = CoverContext(
                    gameID: game.id, romPath: game.romPath, dumpDirectoryPath: game.dumpDirectoryPath,
                    libraryDirectory: romsPath, currentCoverPath: game.coverPath,
                    region: game.region, titles: [game.displayTitle, game.title].compactMap { $0 }
                )
                if await CoverSourceChain.acquireMissing(context) != nil {
                    await self?.refreshCoverPath(forGameID: game.id)
                }

                if LibraryMetadataCache.cachedRegion(for: game.id) == nil
                    || LibraryMetadataCache.cachedTitleName(for: game.id) == nil {
                    await self?.deriveAndApplyRegionAndTitleName(for: game)
                }
            }
            await MainActor.run { [weak self] in
                guard let self else { return }
                GameDataStore.shared.scrapeIfNeeded(games: self.games)
            }
        }
    }

    private func applyCoverPath(_ coverPath: String, forGameID gameID: String) {
        guard let index = games.firstIndex(where: { $0.id == gameID }) else { return }
        games[index].coverPath = coverPath
        if let favIndex = favorites.firstIndex(where: { $0.id == gameID }) {
            favorites[favIndex].coverPath = coverPath
        }
    }

    var romsDirectoryURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?
            .appendingPathComponent(romsDirectory)
    }

    func hasManualCoverOverride(forGameID gameID: String) -> Bool {
        guard let romsPath = romsDirectoryURL else { return false }
        return Self.manualCoverExtensions.contains {
            FileManager.default.fileExists(atPath: romsPath.appendingPathComponent("\(gameID)_cover.\($0)").path)
        }
    }

    private func refreshCoverPath(forGameID gameID: String) {
        guard let index = games.firstIndex(where: { $0.id == gameID }), let romsPath = romsDirectoryURL else { return }
        let newCoverPath = Self.findCover(for: gameID, romPath: games[index].romPath, in: romsPath)
        games[index].coverPath = newCoverPath
        if let favIndex = favorites.firstIndex(where: { $0.id == gameID }) {
            favorites[favIndex].coverPath = newCoverPath
        }
    }

    func setManualCover(imageData: Data, ext: String, forGameID gameID: String) throws {
        guard Self.manualCoverExtensions.contains(ext) else {
            throw CoverOverrideError.unsupportedExtension
        }
        guard let romsPath = romsDirectoryURL else {
            throw CoverOverrideError.noLibraryDirectory
        }
        for staleExt in Self.manualCoverExtensions where staleExt != ext {
            try? FileManager.default.removeItem(at: romsPath.appendingPathComponent("\(gameID)_cover.\(staleExt)"))
        }
        let destination = romsPath.appendingPathComponent("\(gameID)_cover.\(ext)")
        try imageData.write(to: destination, options: .atomic)
        refreshCoverPath(forGameID: gameID)
    }

    func removeManualCover(forGameID gameID: String) {
        guard let romsPath = romsDirectoryURL else { return }
        for ext in Self.manualCoverExtensions {
            try? FileManager.default.removeItem(at: romsPath.appendingPathComponent("\(gameID)_cover.\(ext)"))
        }
        refreshCoverPath(forGameID: gameID)
    }

    private nonisolated func deriveAndApplyRegionAndTitleName(for game: GameMetadata) async {
        var version: UInt16 = 0
        var regionBitmask: Int32 = 0
        var invalidReason: Int32 = 0
        let inspected = game.romPath.withCString { cPath in
            cemu_bridge_inspect_title(cPath, nil, &version, &regionBitmask, &invalidReason)
        }
        if !inspected && (invalidReason == 3 || invalidReason == 4 || invalidReason == 8) {
            return
        }
        let region = inspected ? Self.regionLabel(forBitmask: regionBitmask) : nil
        LibraryMetadataCache.setCachedRegion(region, for: game.id)

        var nameBuffer = [CChar](repeating: 0, count: 256)
        let hasName = game.romPath.withCString { cPath in
            nameBuffer.withUnsafeMutableBufferPointer { buffer in
                cemu_bridge_get_title_name(cPath, buffer.baseAddress, buffer.count)
            }
        }
        let titleName = hasName ? Self.nonEmptyOrNil(String(cString: nameBuffer)) : nil
        LibraryMetadataCache.setCachedTitleName(titleName, for: game.id)

        await MainActor.run { [weak self] in
            guard let self else { return }
            if let index = self.games.firstIndex(where: { $0.id == game.id }) {
                self.games[index].region = region
                self.games[index].displayTitle = titleName
            }
            if let favIndex = self.favorites.firstIndex(where: { $0.id == game.id }) {
                self.favorites[favIndex].region = region
                self.favorites[favIndex].displayTitle = titleName
            }
        }
    }

    private nonisolated static func regionLabel(forBitmask bitmask: Int32) -> String? {
        var labels: [String] = []
        if bitmask & 0x1  != 0 { labels.append("JPN") }
        if bitmask & 0x2  != 0 { labels.append("USA") }
        if bitmask & 0x4  != 0 { labels.append("EUR") }
        if bitmask & 0x8  != 0 { labels.append("CHN") }
        if bitmask & 0x10 != 0 { labels.append("KOR") }
        if bitmask & 0x20 != 0 { labels.append("TWN") }
        return labels.isEmpty ? nil : labels.joined(separator: "/")
    }

    private nonisolated static func nonEmptyOrNil(_ value: String?) -> String? {
        guard let value, !value.isEmpty else { return nil }
        return value
    }

    enum ROMImportError: LocalizedError {
        case invalidROM
        case notAWiiUDump(String)
        case accessDenied
        case copyFailed(Error)

        var errorDescription: String? {
            switch self {
            case .invalidROM:
                return "This isn't a Wii U game file EspressU can read. It accepts .wua, .wux, .wud, .iso, .rpx, .elf and .wuhb files, or a game folder. Encrypted games also need keys.txt."
            case .notAWiiUDump(let name):
                return "\"\(name)\" isn't a Wii U game dump. A dump folder needs code, content and meta folders inside it, or an encrypted game folder: title.tmd and title.tik next to its .app files."
            case .accessDenied:
                return "Couldn't access that file."
            case .copyFailed(let error):
                return "Couldn't copy the ROM: \(error.localizedDescription)"
            }
        }
    }

    enum CoverOverrideError: LocalizedError {
        case unsupportedExtension
        case noLibraryDirectory

        var errorDescription: String? {
            switch self {
            case .unsupportedExtension:
                return "That image couldn't be saved in a supported format."
            case .noLibraryDirectory:
                return "Couldn't find the game library folder."
            }
        }
    }

    static let supportedROMExtensions: Set<String> = ["wux", "wud", "wua", "iso", "rpx", "wuhb", "elf"]

    private static let stagingDirectoryName = ".incoming"

    private nonisolated static func fileMagic(at url: URL, count: Int = 4) -> Data? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: count), data.count == count else {
            return nil
        }
        return data
    }

    nonisolated static func isValidROMFile(at url: URL) -> Bool {
        let ext = url.pathExtension.lowercased()
        guard supportedROMExtensions.contains(ext) else { return false }

        switch ext {
        case "rpx", "elf":
            return fileMagic(at: url) == Data([0x7F, 0x45, 0x4C, 0x46])
        case "wux":
            return fileMagic(at: url) == Data([0x57, 0x55, 0x58, 0x30])
        case "wuhb":
            return fileMagic(at: url) == Data([0x57, 0x55, 0x48, 0x42])
        default:
            return true
        }
    }

    func importROM(from source: URL) async throws {
        let scoped = source.startAccessingSecurityScopedResource()
        guard scoped || FileManager.default.isReadableFile(atPath: source.path) else {
            throw ROMImportError.accessDenied
        }
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }

        let fileManager = FileManager.default

        var isDirectory: ObjCBool = false
        let exists = fileManager.fileExists(atPath: source.path, isDirectory: &isDirectory)
        guard exists else { throw ROMImportError.accessDenied }

        guard let documentsPath = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first else {
            throw ROMImportError.accessDenied
        }

        let romsPath = documentsPath.appendingPathComponent(romsDirectory)
        try? fileManager.createDirectory(at: romsPath, withIntermediateDirectories: true)

        let destination = romsPath.appendingPathComponent(source.lastPathComponent)

        if fileManager.fileExists(atPath: destination.path) {
            let shouldReplace = await confirmOverwrite?(destination.lastPathComponent) ?? false
            guard shouldReplace else { return }
        }

        if isDirectory.boolValue {
            guard Self.looksLikeWiiUDump(source) || Self.looksLikeNUSDump(source) else {
                throw ROMImportError.notAWiiUDump(source.lastPathComponent)
            }

            let stagingPath = romsPath.appendingPathComponent(Self.stagingDirectoryName)
            try? fileManager.createDirectory(at: stagingPath, withIntermediateDirectories: true)

            importState = .copying(name: source.lastPathComponent)
            defer { importState = .idle }

            try await Task.detached {
                try Self.stageAndPromoteDirectory(source: source, destination: destination, stagingPath: stagingPath)
            }.value

            await loadGames(showSpinner: games.isEmpty)
            return
        }

        let stagingPath = romsPath.appendingPathComponent(Self.stagingDirectoryName)
        try? fileManager.createDirectory(at: stagingPath, withIntermediateDirectories: true)

        importState = .copying(name: source.lastPathComponent)
        defer { importState = .idle }

        try await Task.detached {
            try Self.stageAndPromoteFile(source: source, destination: destination, stagingPath: stagingPath)
        }.value

        await loadGames(showSpinner: games.isEmpty)
    }

    private nonisolated static func stageAndPromoteDirectory(source: URL, destination: URL, stagingPath: URL) throws {
        let fileManager = FileManager.default
        let stagedDirectory = stagingPath.appendingPathComponent(source.lastPathComponent)

        do {
            if fileManager.fileExists(atPath: stagedDirectory.path) {
                try fileManager.removeItem(at: stagedDirectory)
            }
            try fileManager.copyItem(at: source, to: stagedDirectory)
        } catch {
            try? fileManager.removeItem(at: stagedDirectory)
            throw ROMImportError.copyFailed(error)
        }

        let copyIsComplete = (looksLikeWiiUDump(stagedDirectory) && executableInDump(stagedDirectory) != nil)
            || looksLikeNUSDump(stagedDirectory)
        guard copyIsComplete else {
            try? fileManager.removeItem(at: stagedDirectory)
            throw ROMImportError.copyFailed(CocoaError(.fileReadCorruptFile))
        }

        do {
            if fileManager.fileExists(atPath: destination.path) {
                try fileManager.removeItem(at: destination)
            }
            try fileManager.moveItem(at: stagedDirectory, to: destination)
        } catch {
            try? fileManager.removeItem(at: stagedDirectory)
            throw ROMImportError.copyFailed(error)
        }
    }

    private nonisolated static func stageAndPromoteFile(source: URL, destination: URL, stagingPath: URL) throws {
        let fileManager = FileManager.default
        let staged = stagingPath.appendingPathComponent(source.lastPathComponent)

        do {
            if fileManager.fileExists(atPath: staged.path) {
                try fileManager.removeItem(at: staged)
            }
            try fileManager.copyItem(at: source, to: staged)
        } catch {
            try? fileManager.removeItem(at: staged)
            throw ROMImportError.copyFailed(error)
        }

        guard isValidROMFile(at: staged) else {
            try? fileManager.removeItem(at: staged)
            throw ROMImportError.invalidROM
        }

        do {
            if fileManager.fileExists(atPath: destination.path) {
                try fileManager.removeItem(at: destination)
            }
            try fileManager.moveItem(at: staged, to: destination)
        } catch {
            try? fileManager.removeItem(at: staged)
            throw ROMImportError.copyFailed(error)
        }
    }

    func toggleFavorite(_ game: GameMetadata) {
        if let index = games.firstIndex(where: { $0.id == game.id }) {
            games[index].isFavorite.toggle()

            if games[index].isFavorite {
                favorites.append(games[index])
            } else {
                favorites.removeAll { $0.id == game.id }
            }

            var favoriteIDs = Self.loadFavoriteIDs()
            if games[index].isFavorite {
                favoriteIDs.insert(game.id)
            } else {
                favoriteIDs.remove(game.id)
            }
            Self.saveFavoriteIDs(favoriteIDs)
        }
    }

    private nonisolated static func loadFavoriteIDs() -> Set<String> {
        Set(UserDefaults.standard.stringArray(forKey: favoriteIDsKey) ?? [])
    }

    private nonisolated static func saveFavoriteIDs(_ ids: Set<String>) {
        UserDefaults.standard.set(Array(ids), forKey: favoriteIDsKey)
    }

    private var launchToken = UUID()

    func launchGame(_ game: GameMetadata) {
        guard emulationState == .idle else { return }
        LibraryPlayStats.shared.recordLaunch(of: game.id)
        launchToken = UUID()
        currentGame = game
        surfaceRegistered = false
        driveWasLost = false

        releaseExternalHold()
        if let locationID = game.externalLocationID {
            guard !game.isUnavailable, let hold = ExternalLibrary.shared.acquire(locationID),
                  ExternalLibrary.isReachable(path: game.romPath) else {
                lastStatusMessage = ExternalLibrary.unavailableMessage + ". \"\(game.cardName.name)\" is stored on a drive or folder EspressU can't reach right now."
                emulationState = .error
                return
            }
            externalHold = hold
        }

        if cemu_bridge_clean_start_required() {
            needsCleanRestart = true
            let reason = String(cString: cemu_bridge_clean_start_reason())
            lastStatusMessage = "EspressU has to be closed and reopened before it can start another game."
                + (reason.isEmpty ? "" : "\n\nWhat happened: \(reason).")
            emulationState = .error
            return
        }
        needsCleanRestart = false
        titleEndedByEngine = false
        ActiveGameSettings.begin(gameID: game.settingsKey)
        emulationState = .loading

        guard let engine = emulationEngine else {
            emulationState = .error
            return
        }

        guard engine.coreAvailable else {
            lastStatusMessage = engine.statusText
            emulationState = .error
            return
        }

    }

    #if os(iOS)
    @discardableResult
    func registerRenderSurface(uiView: UIView, width: Int32, height: Int32, dpiScale: Double) -> Bool {
        guard emulationState == .loading, !surfaceRegistered,
              let game = currentGame, let engine = emulationEngine else { return false }
        surfaceRegistered = true

        let surfacePtr = Unmanaged.passRetained(uiView).toOpaque()
        cemu_bridge_register_render_surface(surfacePtr, width, height, dpiScale)

        GameControlHints.applyBeforeLaunch(titleId: game.titleId)

        let romPath = game.romPath
        let gameID = game.settingsKey
        let token = launchToken
        Task.detached(priority: .userInitiated) { [weak self] in
            guard let documentsPath = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
            let mlcPath = documentsPath.appendingPathComponent("mlc").path
            try? FileManager.default.createDirectory(atPath: mlcPath, withIntermediateDirectories: true)

            cemu_bridge_log_checkpoint("launchGame: about to call engine.initialize() [background]")
            EmulationEngine.initializeBlocking(mlcPath: mlcPath)
            cemu_bridge_log_checkpoint("launchGame: engine.initialize() returned [background]")

            TimebaseScale.applyStoredChoiceIfAny()

            cemu_bridge_set_recompiler_enabled(
                UserDefaults.standard.object(forKey: "espressu.cpu.recompiler") as? Bool ?? true)
            cemu_bridge_set_favour_accuracy(
                PerGameSettingsStore.shared.effectiveFavourAccuracy(for: game.settingsKey))
            cemu_bridge_set_favour_performance(
                PerGameSettingsStore.shared.effectiveFavourPerformance(for: game.settingsKey))
            PerGameSettingsStore.shared.applyFullSpeedRenders(for: game.settingsKey)
            cemu_bridge_set_async_shader_compile(
                PerGameSettingsStore.shared.effectivePreCompileShaders(for: game.settingsKey))
            cemu_bridge_set_low_power_mode(PerGameSettingsStore.shared.effectiveOneCoreMode(for: game.settingsKey))
            MotionSettings.applyToBridge()
            cemu_bridge_set_cpu_core_mode(PerGameSettingsStore.shared.effectiveCoreMode(for: game.settingsKey).bridgeValue)
            cemu_bridge_set_cpu_auto_demoted(AutoCoreHistory.isDemotedAtLaunch(gameID: game.settingsKey))
            cemu_bridge_set_vsync_enabled(
                UserDefaults.standard.object(forKey: "espressu.render.vsync") as? Bool ?? true)
            cemu_bridge_set_stretch_to_fill(
                UserDefaults.standard.object(forKey: FrameStretch.storageKey) as? Bool
                    ?? FrameStretch.defaultValue)

            cemu_bridge_set_graphics_api(
                Int32(clamping: UserDefaults.standard.object(forKey: "espressu.render.graphicsAPI") as? Int ?? 2))
            let filters = PerGameSettingsStore.shared.filtersToPush(for: game.settingsKey)
            cemu_bridge_set_upscale_filter(filters.upscale)
            cemu_bridge_set_downscale_filter(filters.downscale)

            cemu_bridge_set_render_upside_down(
                UserDefaults.standard.object(forKey: "espressu.render.upsideDown") as? Bool ?? false)
            cemu_bridge_set_framebuffer_fetch(
                UserDefaults.standard.object(forKey: "espressu.render.framebufferFetch") as? Bool ?? true)
            cemu_bridge_set_display_gamma(Float(
                UserDefaults.standard.object(forKey: DisplayGammaSetting.storageKey) as? Double
                    ?? DisplayGammaSetting.defaultValue))
            cemu_bridge_set_override_app_gamma(
                UserDefaults.standard.object(forKey: "espressu.render.overrideAppGamma") as? Bool ?? false)
            cemu_bridge_set_override_gamma_value(Float(
                UserDefaults.standard.object(forKey: OverrideGammaSetting.storageKey) as? Double
                    ?? OverrideGammaSetting.defaultValue))
            cemu_bridge_set_overlay_position(
                Int32(PerGameSettingsStore.shared.effectiveOverlayPosition(for: game.settingsKey).rawValue))
            cemu_bridge_set_overlay_text_color(
                UInt32(clamping: UserDefaults.standard.object(forKey: OverlaySettings.textColorKey) as? Int
                    ?? OverlaySettings.defaultTextColor))
            cemu_bridge_set_overlay_text_scale(
                Int32(clamping: UserDefaults.standard.object(forKey: OverlaySettings.textScaleKey) as? Int
                    ?? OverlaySettings.defaultTextScale))
            cemu_bridge_set_overlay_fps(
                UserDefaults.standard.object(forKey: OverlaySettings.fpsKey) as? Bool
                    ?? OverlaySettings.defaultFps)
            cemu_bridge_set_overlay_drawcalls(
                UserDefaults.standard.object(forKey: OverlaySettings.drawcallsKey) as? Bool
                    ?? OverlaySettings.defaultDrawcalls)
            cemu_bridge_set_overlay_cpu_usage(
                UserDefaults.standard.object(forKey: OverlaySettings.cpuUsageKey) as? Bool
                    ?? OverlaySettings.defaultCpuUsage)
            cemu_bridge_set_overlay_cpu_per_core_usage(
                UserDefaults.standard.object(forKey: OverlaySettings.cpuPerCoreUsageKey) as? Bool
                    ?? OverlaySettings.defaultCpuPerCoreUsage)
            cemu_bridge_set_overlay_ram_usage(
                UserDefaults.standard.object(forKey: OverlaySettings.ramUsageKey) as? Bool
                    ?? OverlaySettings.defaultRamUsage)
            cemu_bridge_set_overlay_vram_usage(
                UserDefaults.standard.object(forKey: OverlaySettings.vramUsageKey) as? Bool
                    ?? OverlaySettings.defaultVramUsage)
            cemu_bridge_set_overlay_debug(
                UserDefaults.standard.object(forKey: OverlaySettings.debugKey) as? Bool
                    ?? OverlaySettings.defaultDebug)

            cemu_bridge_set_notification_position(
                Int32(clamping: UserDefaults.standard.object(forKey: NotificationSettings.positionKey) as? Int
                    ?? NotificationSettings.defaultPosition.rawValue))
            cemu_bridge_set_notification_text_color(
                UInt32(clamping: UserDefaults.standard.object(forKey: NotificationSettings.textColorKey) as? Int
                    ?? NotificationSettings.defaultTextColor))
            cemu_bridge_set_notification_text_scale(
                Int32(clamping: UserDefaults.standard.object(forKey: NotificationSettings.textScaleKey) as? Int
                    ?? NotificationSettings.defaultTextScale))
            cemu_bridge_set_notification_controller_profiles(
                UserDefaults.standard.object(forKey: NotificationSettings.controllerProfilesKey) as? Bool
                    ?? NotificationSettings.defaultControllerProfiles)
            cemu_bridge_set_notification_controller_battery(
                UserDefaults.standard.object(forKey: NotificationSettings.controllerBatteryKey) as? Bool
                    ?? NotificationSettings.defaultControllerBattery)
            cemu_bridge_set_notification_shader_compiling(
                UserDefaults.standard.object(forKey: NotificationSettings.shaderCompilingKey) as? Bool
                    ?? NotificationSettings.defaultShaderCompiling)
            cemu_bridge_set_notification_friends(
                UserDefaults.standard.object(forKey: NotificationSettings.friendsKey) as? Bool
                    ?? NotificationSettings.defaultFriends)

            cemu_bridge_set_emulate_skylander_portal(
                UserDefaults.standard.object(forKey: EmulatedDevicesSettings.skylanderPortalKey) as? Bool
                    ?? EmulatedDevicesSettings.defaultEnabled)
            cemu_bridge_set_emulate_infinity_base(
                UserDefaults.standard.object(forKey: EmulatedDevicesSettings.infinityBaseKey) as? Bool
                    ?? EmulatedDevicesSettings.defaultEnabled)
            cemu_bridge_set_emulate_dimensions_toypad(
                UserDefaults.standard.object(forKey: EmulatedDevicesSettings.dimensionsToypadKey) as? Bool
                    ?? EmulatedDevicesSettings.defaultEnabled)

            cemu_bridge_set_tv_audio_enabled(
                UserDefaults.standard.object(forKey: AudioSettings.tvEnabledKey) as? Bool ?? AudioSettings.defaultTvEnabled)
            cemu_bridge_set_tv_volume(
                Int32(clamping: UserDefaults.standard.object(forKey: AudioSettings.tvVolumeKey) as? Int ?? AudioSettings.defaultTvVolume))
            cemu_bridge_set_tv_channels(
                Int32(clamping: UserDefaults.standard.object(forKey: AudioSettings.tvChannelsKey) as? Int ?? AudioSettings.defaultTvChannels))
            cemu_bridge_set_pad_audio_enabled(
                UserDefaults.standard.object(forKey: AudioSettings.padEnabledKey) as? Bool ?? AudioSettings.defaultPadEnabled)
            cemu_bridge_set_pad_volume(
                Int32(clamping: UserDefaults.standard.object(forKey: AudioSettings.padVolumeKey) as? Int ?? AudioSettings.defaultPadVolume))
            cemu_bridge_set_pad_channels(
                Int32(clamping: UserDefaults.standard.object(forKey: AudioSettings.padChannelsKey) as? Int ?? AudioSettings.defaultPadChannels))
            cemu_bridge_set_microphone_enabled(
                UserDefaults.standard.object(forKey: AudioSettings.microphoneEnabledKey) as? Bool ?? AudioSettings.defaultMicrophoneEnabled)
            cemu_bridge_set_input_volume(
                Int32(clamping: UserDefaults.standard.object(forKey: AudioSettings.inputVolumeKey) as? Int ?? AudioSettings.defaultInputVolume))

            cemu_bridge_log_checkpoint("launchGame: about to call engine.boot() [background]")
            let status = EmulationEngine.bootBlocking(path: romPath)
            cemu_bridge_log_checkpoint("launchGame: engine.boot() returned [background]")

            await MainActor.run {
                guard let self else { return }
                guard self.launchToken == token, self.emulationState == .loading else {
                    if status == CEMU_BRIDGE_OK, self.currentGame == nil {
                        self.stopEmulation()
                    }
                    return
                }
                engine.refreshStatus()
                self.lastStatusMessage = engine.statusText
                var notice = String(cString: cemu_bridge_take_launch_notice())
                if notice.isEmpty && status == CEMU_BRIDGE_OK && ProcessInfo.processInfo.isLowPowerModeEnabled {
                    notice = "Low Power Mode is on, so games may run slowly. Turn it off in Control Centre for full speed."
                }
                if notice.isEmpty && status == CEMU_BRIDGE_OK, let onlineNotice = OnlineReadiness.launchNotice() {
                    notice = onlineNotice
                }
                if !notice.isEmpty {
                    self.showLaunchNotice(notice)
                }
                self.emulationState = (status == CEMU_BRIDGE_OK) ? .running : .error
                if self.emulationState == .running {
                    self.startFrameRateMonitor()
                    if cemu_bridge_cpu_auto_picked_multicore() {
                        AutoCoreHistory.sessionStarted(gameID: gameID)
                    }
                }
            }
        }

        return true
    }
    #endif

    func showLaunchNotice(_ notice: String) {
        launchNotice = notice
        #if os(iOS)
        UIAccessibility.post(notification: .announcement, argument: notice)
        #endif
        let words = notice.split(whereSeparator: \.isWhitespace).count
        let seconds = min(20.0, max(6.0, 2.5 + Double(words) / 2.5))
        Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
            if self?.launchNotice == notice {
                self?.launchNotice = nil
            }
        }
    }

    func stopEmulation() {
        launchToken = UUID()
        if let gameID = currentGame?.settingsKey {
            AutoCoreHistory.sessionEnded(gameID: gameID, stalled: videoStalled && videoStallKind == 1)
        }
        stopFrameRateMonitor()
        #if os(iOS)
        cemu_bridge_resume()
        #endif
        emulationEngine?.stop()
        #if os(iOS)
        DisplayRouter.shared.titleStopped()
        #endif
        surfaceRegistered = false
        releaseExternalHold()
        emulationState = .idle
        currentGame = nil
        ActiveGameSettings.end()
    }

    /// Same as stopEmulation, but the slow part (joining the CPU and GPU threads, flushing caches)
    /// runs off the main thread so the UI stays responsive. A new launch waits until it is done.
    func quitEmulationAsync() {
        guard !isQuitting else { return }
        isQuitting = true
        launchToken = UUID()
        if let gameID = currentGame?.settingsKey {
            AutoCoreHistory.sessionEnded(gameID: gameID, stalled: videoStalled && videoStallKind == 1)
        }
        stopFrameRateMonitor()
        #if os(iOS)
        cemu_bridge_resume()
        #endif
        DispatchQueue.global(qos: .userInitiated).async {
            cemu_bridge_shutdown_title()
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.emulationEngine?.stop()
                #if os(iOS)
                DisplayRouter.shared.titleStopped()
                #endif
                self.surfaceRegistered = false
                self.releaseExternalHold()
                self.emulationState = .idle
                self.currentGame = nil
                ActiveGameSettings.end()
                self.isQuitting = false
            }
        }
    }

    private func releaseExternalHold() {
        if let hold = externalHold { ExternalLibrary.shared.release(hold) }
        externalHold = nil
    }

    func externalGameIsReady(_ game: GameMetadata) async -> Bool {
        guard let locationID = game.externalLocationID else { return true }
        let path = game.romPath
        let unavailable = game.isUnavailable
        var ready = false
        if !unavailable {
            ready = await Task.detached { ExternalLibrary.shared.probe(locationID, path: path) }.value
        }
        if !ready {
            launchBlockedMessage = "\"\(game.cardName.name)\" is stored on a drive or folder EspressU can't reach right now. Connect the drive, then try again."
        }
        return ready
    }

    private func stopBecauseDriveWasLost() {
        let game = currentGame
        stopEmulation()
        guard let game else { return }
        currentGame = game
        lastStatusMessage = "The drive or folder holding \"\(game.cardName.name)\" was disconnected. Connect it again, then start the game again. Your progress up to your last save is kept."
        driveWasLost = true
        titleEndedByEngine = true
        emulationState = .error
    }

    func getEmulationEngine() -> EmulationEngine? {
        return emulationEngine
    }

    func getFrameTexture() -> MTLTexture? {
        return nil
    }

    func getFrameRate() -> Int {
        return frameRate
    }

    private func startFrameRateMonitor() {
        frameRateTimer?.invalidate()
        frameRateTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                if self.emulationState == .running && !cemu_bridge_is_title_running() {
                    self.stopTitleEndedByEngine()
                    return
                }
                if self.emulationState == .running, let game = self.currentGame, game.isExternal, !self.driveCheckInFlight {
                    self.driveCheckInFlight = true
                    let path = game.romPath
                    let gameID = game.id
                    Task.detached { [weak self] in
                        let present = ExternalLibrary.isReachable(path: path)
                        await MainActor.run { [weak self] in
                            guard let self else { return }
                            self.driveCheckInFlight = false
                            if !present, self.emulationState == .running, self.currentGame?.id == gameID {
                                self.stopBecauseDriveWasLost()
                            }
                        }
                    }
                }
                let fps = Int(cemu_bridge_get_fps().rounded())
                if fps != self.frameRate {
                    self.frameRate = fps
                }
                let stalled = cemu_bridge_video_stalled()
                if stalled != self.videoStalled {
                    self.videoStalled = stalled
                }
                let kind = Int(cemu_bridge_video_stall_kind())
                if kind != self.videoStallKind {
                    self.videoStallKind = kind
                }
                let snapshot = EmulatorProgress.read()
                if snapshot != self.progress {
                    self.progress = snapshot
                }
            }
        }
    }

    private func stopFrameRateMonitor() {
        frameRateTimer?.invalidate()
        frameRateTimer = nil
        frameRate = 0
        videoStalled = false
        videoStallKind = 0
        progress = EmulatorProgress()
    }

    private func stopTitleEndedByEngine() {
        let reason = String(cString: cemu_bridge_status_text())
        let game = currentGame
        let driveGone = game.map { $0.isExternal && !ExternalLibrary.isReachable(path: $0.romPath) } ?? false
        if driveGone { stopBecauseDriveWasLost(); return }
        stopEmulation()
        guard let game else { return }
        currentGame = game
        lastStatusMessage = reason
        titleEndedByEngine = true
        emulationState = .error
    }
}

struct EmulatorProgress: Equatable {
    var gx2InitReached: Bool = false
    var gx2FrameCount: UInt64 = 0
    var gx2FramesPerSecond: Double = 0
    var osScreenScanouts: UInt64 = 0
    var guestFlipRequests: UInt32 = 0

    static func read() -> EmulatorProgress {
        var raw = CemuBridgeProgress()
        cemu_bridge_get_progress(&raw)
        return EmulatorProgress(
            gx2InitReached: raw.gx2_init_reached,
            gx2FrameCount: raw.gx2_frame_count,
            gx2FramesPerSecond: raw.gx2_frames_per_second,
            osScreenScanouts: raw.os_screen_scanouts,
            guestFlipRequests: raw.guest_flip_requests)
    }

    func hudText(wholeFramesPerSecond: Int) -> String {
        if wholeFramesPerSecond > 0 {
            return "\(wholeFramesPerSecond) FPS"
        }
        if gx2FrameCount > 0 {
            if gx2FramesPerSecond > 0 {
                return String(format: "%.1f FPS", gx2FramesPerSecond)
            }
            return String(format: "%llu frames", gx2FrameCount)
        }
        if gx2InitReached {
            return "Started, no picture yet"
        }
        if osScreenScanouts > 0 || guestFlipRequests > 0 {
            return "Booting..."
        }
        return "-- FPS"
    }
}

enum EmulationState {
    case idle
    case loading
    case running
    case paused
    case error
}

final class TitleSwitchSettings {
    static func apply(titleId: UInt64) {
        let id = GameMetadata.settingsKey(forTitleId: cemu_bridge_derive_base_title_id(titleId))
        cemu_bridge_set_cpu_auto_demoted(AutoCoreHistory.isDemoted(gameID: id))
        cemu_bridge_set_cpu_core_mode(PerGameSettingsStore.shared.effectiveCoreMode(for: id).bridgeValue)
        cemu_bridge_set_favour_accuracy(PerGameSettingsStore.shared.effectiveFavourAccuracy(for: id))
        cemu_bridge_set_favour_performance(PerGameSettingsStore.shared.effectiveFavourPerformance(for: id))
        PerGameSettingsStore.shared.applyFullSpeedRenders(for: id)
        cemu_bridge_set_low_power_mode(PerGameSettingsStore.shared.effectiveOneCoreMode(for: id))
        cemu_bridge_set_async_shader_compile(PerGameSettingsStore.shared.effectivePreCompileShaders(for: id))
        cemu_bridge_set_overlay_position(Int32(PerGameSettingsStore.shared.effectiveOverlayPosition(for: id).rawValue))
        let filters = PerGameSettingsStore.shared.filtersToPush(for: id)
        cemu_bridge_set_upscale_filter(filters.upscale)
        cemu_bridge_set_downscale_filter(filters.downscale)
        let resolutionChanged = ActiveGameSettings.begin(gameID: id)
        #if os(iOS)
        if resolutionChanged {
            Task { @MainActor in DisplayRouter.shared.reapplyRenderScale(reason: "the game's own Resolution") }
        }
        #endif
        #if os(iOS)
        GameControlHints.applyBeforeLaunch(titleId: titleId)
        #endif
    }
}
