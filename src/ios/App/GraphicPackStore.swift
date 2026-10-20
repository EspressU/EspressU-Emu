// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import Combine

enum GraphicPackPaths {
    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSTemporaryDirectory())
    }
    static var mlc: URL { documents.appendingPathComponent("mlc", isDirectory: true) }
    static var root: URL { mlc.appendingPathComponent("graphicPacks", isDirectory: true) }
    static var downloaded: URL { root.appendingPathComponent("downloadedGraphicPacks", isDirectory: true) }
    static var imported: URL { root.appendingPathComponent("imported", isDirectory: true) }
    static var scratch: URL { mlc.appendingPathComponent(".graphicpack-scratch", isDirectory: true) }
}

struct GraphicPackInstall: Codable, Equatable {
    var tag: String
    var name: String
    var installedAt: Date
    var packCount: Int
}

struct GraphicPackRelease: Codable, Equatable {
    var tag: String
    var name: String
    var assetName: String
    var assetURL: URL
    var size: Int64
    var publishedAt: String?
    var pageURL: URL?
}

final class GraphicPackStore: NSObject, ObservableObject {
    static let shared = GraphicPackStore()

    static let repoPage = URL(string: "https://github.com/cemu-project/cemu_graphic_packs")!
    private static let latestReleaseAPI = URL(string: "https://api.github.com/repos/cemu-project/cemu_graphic_packs/releases/latest")!
    private static let sessionIdentifier = "com.am4rdev.EspressU.graphicpacks"

    enum Phase: Equatable {
        case idle
        case checking
        case downloading(Double)
        case installing(Double)
        case waitingForGameToClose
    }

    struct Notice: Equatable {
        var text: String
        var isError: Bool
    }

    @Published private(set) var packs: [GraphicPack] = []
    @Published private(set) var isScanning = false
    @Published private(set) var phase: Phase = .idle
    @Published private(set) var installed: GraphicPackInstall?
    @Published private(set) var latest: GraphicPackRelease?
    @Published private(set) var lastChecked: Date?
    @Published private(set) var notice: Notice?
    @Published private(set) var gameRunning = false

    var updateAvailable: Bool {
        guard let latest else { return false }
        guard let installed else { return true }
        return installed.tag != latest.tag
    }

    var isBusy: Bool {
        switch phase {
        case .idle, .waitingForGameToClose: return false
        default: return true
        }
    }

    private let bridgeQueue = DispatchQueue(label: "com.am4rdev.EspressU.graphicpacks.bridge", qos: .userInitiated)
    private let workQueue = DispatchQueue(label: "com.am4rdev.EspressU.graphicpacks.install", qos: .utility)
    private let defaults = UserDefaults.standard
    private var cancelRequested = false
    private var apiTask: URLSessionDataTask?

    private enum Key {
        static let latest = "espressu.graphicPacks.latestRelease"
        static let lastCheck = "espressu.graphicPacks.lastCheck"
        static let pending = "espressu.graphicPacks.pendingRelease"
    }

    private static let checkInterval: TimeInterval = 24 * 60 * 60

    private lazy var session: URLSession = {
        let config = URLSessionConfiguration.background(withIdentifier: GraphicPackStore.sessionIdentifier)
        config.sessionSendsLaunchEvents = true
        config.isDiscretionary = false
        config.waitsForConnectivity = false
        config.timeoutIntervalForResource = 60 * 30
        return URLSession(configuration: config, delegate: self, delegateQueue: nil)
    }()

    private override init() {
        super.init()
        if let data = defaults.data(forKey: Key.latest) {
            latest = try? JSONDecoder().decode(GraphicPackRelease.self, from: data)
        }
        lastChecked = defaults.object(forKey: Key.lastCheck) as? Date
        installed = Self.readInstalled()
    }

    func onOpen() {
        gameRunning = espressu_gp_title_running()
        installed = Self.readInstalled()
        attachToRunningDownload()
        finishPendingInstallIfPossible()
        reload()
        checkForUpdate(force: false)
    }

    func refreshList() {
        bridgeQueue.async { [weak self] in
            let raw = String(cString: espressu_gp_list())
            let parsed = GraphicPack.parseList(raw)
            DispatchQueue.main.async { self?.packs = parsed }
        }
    }

    func reload() {
        isScanning = true
        let mlc = GraphicPackPaths.mlc.path
        bridgeQueue.async { [weak self] in
            try? FileManager.default.createDirectory(at: GraphicPackPaths.root, withIntermediateDirectories: true)
            _ = espressu_gp_reload(mlc)
            let raw = String(cString: espressu_gp_list())
            let parsed = GraphicPack.parseList(raw)
            let running = espressu_gp_title_running()
            DispatchQueue.main.async {
                self?.packs = parsed
                self?.gameRunning = running
                self?.isScanning = false
            }
        }
    }

    func setEnabled(_ pack: GraphicPack, _ enabled: Bool) {
        if let i = packs.firstIndex(where: { $0.path == pack.path }) { packs[i].enabled = enabled }
        bridgeQueue.async { [weak self] in
            let ok = pack.path.withCString { espressu_gp_set_enabled($0, enabled) }
            if !ok { DispatchQueue.main.async { self?.refreshList() } }
        }
    }

    func details(for pack: GraphicPack, completion: @escaping (GraphicPackDetails) -> Void) {
        bridgeQueue.async {
            let raw = pack.path.withCString { String(cString: espressu_gp_details($0)) }
            let parsed = GraphicPackDetails.parse(raw)
            DispatchQueue.main.async { completion(parsed) }
        }
    }

    func setPreset(_ pack: GraphicPack, category: String, preset: String, completion: @escaping (GraphicPackDetails) -> Void) {
        bridgeQueue.async { [weak self] in
            _ = pack.path.withCString { p in category.withCString { c in preset.withCString { n in espressu_gp_set_preset(p, c, n) } } }
            let raw = pack.path.withCString { String(cString: espressu_gp_details($0)) }
            let parsed = GraphicPackDetails.parse(raw)
            DispatchQueue.main.async {
                completion(parsed)
                self?.refreshList()
            }
        }
    }

    func reset(_ pack: GraphicPack, completion: @escaping (GraphicPackDetails) -> Void) {
        bridgeQueue.async { [weak self] in
            _ = pack.path.withCString { espressu_gp_reset_pack($0) }
            let raw = pack.path.withCString { String(cString: espressu_gp_details($0)) }
            let parsed = GraphicPackDetails.parse(raw)
            DispatchQueue.main.async {
                completion(parsed)
                self?.refreshList()
            }
        }
    }

    func checkForUpdate(force: Bool) {
        guard phase == .idle || phase == .waitingForGameToClose else { return }
        if !force, let lastChecked, Date().timeIntervalSince(lastChecked) < Self.checkInterval { return }

        phase = .checking
        if force { notice = nil }
        var request = URLRequest(url: Self.latestReleaseAPI, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 20)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("EspressU", forHTTPHeaderField: "User-Agent")
        let task = URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            let outcome = Self.interpretLatestRelease(data: data, response: response, error: error)
            DispatchQueue.main.async {
                guard let self else { return }
                self.apiTask = nil
                if self.phase == .checking { self.phase = .idle }
                switch outcome {
                case .success(let release):
                    self.latest = release
                    self.lastChecked = Date()
                    self.defaults.set(try? JSONEncoder().encode(release), forKey: Key.latest)
                    self.defaults.set(self.lastChecked, forKey: Key.lastCheck)
                    if force {
                        self.notice = self.updateAvailable ? nil : Notice(text: "You have the latest community packs.", isError: false)
                    }
                case .failure(let message):
                    if force { self.notice = Notice(text: message, isError: true) }
                }
            }
        }
        apiTask = task
        task.resume()
    }

    private enum LatestOutcome {
        case success(GraphicPackRelease)
        case failure(String)
    }

    private static func interpretLatestRelease(data: Data?, response: URLResponse?, error: Error?) -> LatestOutcome {
        if let error { return .failure(describe(error)) }
        guard let http = response as? HTTPURLResponse else { return .failure("GitHub didn't answer.") }
        if http.statusCode == 403 || http.statusCode == 429 {
            return .failure("GitHub is limiting requests right now. Try again in a little while.")
        }
        guard http.statusCode == 200, let data else { return .failure("GitHub answered with an error (\(http.statusCode)).") }
        struct Payload: Decodable {
            struct Asset: Decodable { let name: String; let size: Int64; let browser_download_url: URL }
            let tag_name: String
            let name: String?
            let published_at: String?
            let html_url: URL?
            let assets: [Asset]
        }
        guard let payload = try? JSONDecoder().decode(Payload.self, from: data) else {
            return .failure("GitHub's answer wasn't what was expected.")
        }
        let asset = payload.assets.first { $0.name.hasPrefix("graphicPacks") && $0.name.hasSuffix(".zip") }
            ?? payload.assets.first { $0.name.hasSuffix(".zip") }
        guard let asset, isTrustedDownload(asset.browser_download_url) else {
            return .failure("The latest release doesn't have a download EspressU can use.")
        }
        guard asset.size > 0, asset.size <= 256 * 1024 * 1024 else {
            return .failure("The latest release is a different size than graphic packs should be.")
        }
        return .success(GraphicPackRelease(tag: payload.tag_name, name: payload.name ?? payload.tag_name,
                                           assetName: asset.name, assetURL: asset.browser_download_url,
                                           size: asset.size, publishedAt: payload.published_at, pageURL: payload.html_url))
    }

    private static func isTrustedDownload(_ url: URL) -> Bool {
        guard url.scheme == "https", url.host?.lowercased() == "github.com" else { return false }
        return url.path.lowercased().hasPrefix("/cemu-project/cemu_graphic_packs/releases/download/")
    }

    static func describe(_ error: Error) -> String {
        if let storage = error as? GraphicPackStorage.StorageError { return storage.errorDescription ?? "Not enough free storage." }
        if let zip = error as? MiniZip.ZipError { return zip.errorDescription ?? "The zip couldn't be read." }
        if let url = error as? URLError {
            switch url.code {
            case .notConnectedToInternet, .dataNotAllowed, .internationalRoamingOff:
                return "You're offline. Packs you've already downloaded keep working."
            case .timedOut, .networkConnectionLost, .cannotConnectToHost, .cannotFindHost, .dnsLookupFailed:
                return "Couldn't reach GitHub. Check your connection and try again."
            case .cancelled:
                return "Cancelled."
            default:
                return "The download failed (\(url.localizedDescription))."
            }
        }
        return error.localizedDescription
    }

    func startDownload() {
        guard let release = latest, !isBusy else { return }
        if espressu_gp_title_running() {
            notice = Notice(text: "A game is running. Close it before downloading graphic packs.", isError: true)
            return
        }
        do { try GraphicPackStorage.require(release.size * 4, what: "download the graphic packs") } catch {
            notice = Notice(text: Self.describe(error), isError: true)
            return
        }
        notice = nil
        cancelRequested = false
        try? FileManager.default.removeItem(at: Self.pendingZip)
        try? FileManager.default.createDirectory(at: GraphicPackPaths.scratch, withIntermediateDirectories: true)
        defaults.set(try? JSONEncoder().encode(release), forKey: Key.pending)
        phase = .downloading(0)
        let task = session.downloadTask(with: release.assetURL)
        task.taskDescription = release.tag
        task.resume()
    }

    func cancel() {
        cancelRequested = true
        apiTask?.cancel()
        session.getAllTasks { tasks in tasks.forEach { $0.cancel() } }
        defaults.removeObject(forKey: Key.pending)
        switch phase {
        case .downloading, .installing, .checking:
            phase = .idle
            notice = Notice(text: "Cancelled. Your installed packs weren't changed.", isError: false)
        default:
            break
        }
    }

    private func attachToRunningDownload() {
        guard phase == .idle else { return }
        session.getAllTasks { [weak self] tasks in
            guard let self, tasks.contains(where: { $0.state == .running || $0.state == .suspended }) else { return }
            DispatchQueue.main.async { if self.phase == .idle { self.phase = .downloading(0) } }
        }
    }

    private static var pendingZip: URL { GraphicPackPaths.scratch.appendingPathComponent("download.zip") }

    private func finishPendingInstallIfPossible() {
        guard phase == .idle || phase == .waitingForGameToClose else { return }
        guard FileManager.default.fileExists(atPath: Self.pendingZip.path),
              let data = defaults.data(forKey: Key.pending),
              let release = try? JSONDecoder().decode(GraphicPackRelease.self, from: data) else { return }
        guard !espressu_gp_title_running() else {
            phase = .waitingForGameToClose
            return
        }
        install(zip: Self.pendingZip, release: release)
    }

    private func install(zip: URL, release: GraphicPackRelease) {
        phase = .installing(0)
        workQueue.async { [weak self] in
            guard let self else { return }
            let fm = FileManager.default
            let staging = GraphicPackPaths.scratch.appendingPathComponent("extract", isDirectory: true)
            do {
                let attributes = try fm.attributesOfItem(atPath: zip.path)
                let onDisk = (attributes[.size] as? NSNumber)?.int64Value ?? -1
                guard onDisk == release.size else {
                    throw MiniZip.ZipError.corrupt("the download is \(onDisk) bytes, expected \(release.size)")
                }

                let archive = try MiniZip(url: zip)
                let total = archive.entries.reduce(Int64(0)) { $0 + Int64($1.size) }
                try GraphicPackStorage.require(total, what: "install the graphic packs")
                try archive.verify(progress: { p in self.setInstallProgress(p * 0.4) }, isCancelled: { self.cancelRequested })

                try? fm.removeItem(at: staging)
                try archive.extract(to: staging, progress: { p in self.setInstallProgress(0.4 + p * 0.55) },
                                    isCancelled: { self.cancelRequested })
                let packCount = Self.countPacks(in: staging)
                guard packCount > 0 else { throw MiniZip.ZipError.corrupt("it holds no graphic packs") }

                let info = GraphicPackInstall(tag: release.tag, name: release.name, installedAt: Date(), packCount: packCount)
                try release.name.write(to: staging.appendingPathComponent("version.txt"), atomically: true, encoding: .utf8)
                let encoder = JSONEncoder()
                encoder.dateEncodingStrategy = .iso8601
                try encoder.encode(info).write(to: staging.appendingPathComponent("version.json"), options: .atomic)

                if espressu_gp_title_running() {
                    DispatchQueue.main.async { self.phase = .waitingForGameToClose }
                    try? fm.removeItem(at: staging)
                    return
                }
                try fm.createDirectory(at: GraphicPackPaths.root, withIntermediateDirectories: true)
                try Self.swap(staging, into: GraphicPackPaths.downloaded)
                try? fm.removeItem(at: zip)
                self.defaults.removeObject(forKey: Key.pending)
                DispatchQueue.main.async {
                    self.installed = info
                    self.phase = .idle
                    self.notice = Notice(text: "Installed \(packCount) community packs (\(release.name)). Changes apply the next time you launch a game.", isError: false)
                    self.reload()
                }
            } catch is CancellationError {
                try? fm.removeItem(at: staging)
                DispatchQueue.main.async { self.phase = .idle }
            } catch {
                try? fm.removeItem(at: staging)
                try? fm.removeItem(at: zip)
                self.defaults.removeObject(forKey: Key.pending)
                let message = Self.describe(error)
                DispatchQueue.main.async {
                    self.phase = .idle
                    self.notice = Notice(text: "\(message) Your installed packs weren't changed.", isError: true)
                }
            }
        }
    }

    private func setInstallProgress(_ value: Double) {
        DispatchQueue.main.async { if case .installing = self.phase { self.phase = .installing(min(1, value)) } }
    }

    private static func swap(_ staged: URL, into destination: URL) throws {
        let fm = FileManager.default
        guard fm.fileExists(atPath: destination.path) else {
            try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            try fm.moveItem(at: staged, to: destination)
            return
        }
        do {
            _ = try fm.replaceItemAt(destination, withItemAt: staged, backupItemName: nil, options: [])
        } catch {
            let previous = GraphicPackPaths.scratch.appendingPathComponent("previous", isDirectory: true)
            try? fm.removeItem(at: previous)
            try fm.moveItem(at: destination, to: previous)
            do {
                try fm.moveItem(at: staged, to: destination)
                try? fm.removeItem(at: previous)
            } catch {
                try? fm.moveItem(at: previous, to: destination)
                throw error
            }
        }
    }

    private static func countPacks(in directory: URL) -> Int {
        guard let walker = FileManager.default.enumerator(at: directory, includingPropertiesForKeys: nil) else { return 0 }
        var count = 0
        for case let url as URL in walker where url.lastPathComponent == "rules.txt" { count += 1 }
        return count
    }

    private static func readInstalled() -> GraphicPackInstall? {
        let dir = GraphicPackPaths.downloaded
        if let data = try? Data(contentsOf: dir.appendingPathComponent("version.json")) {
            let decoder = JSONDecoder()
            decoder.dateDecodingStrategy = .iso8601
            if let info = try? decoder.decode(GraphicPackInstall.self, from: data) { return info }
        }
        if let text = try? String(contentsOf: dir.appendingPathComponent("version.txt"), encoding: .utf8),
           let line = text.split(whereSeparator: \.isNewline).first {
            return GraphicPackInstall(tag: String(line), name: String(line), installedAt: .distantPast, packCount: countPacks(in: dir))
        }
        return nil
    }

    struct ImportResult {
        let packCount: Int
        let folderName: String
    }

    func importPicked(_ url: URL, completion: @escaping (Result<ImportResult, Error>) -> Void) {
        let scoped = url.startAccessingSecurityScopedResource()
        workQueue.async { [weak self] in
            let fm = FileManager.default
            let work = GraphicPackPaths.scratch.appendingPathComponent("import-\(UUID().uuidString)", isDirectory: true)
            defer {
                if scoped { url.stopAccessingSecurityScopedResource() }
                try? fm.removeItem(at: work)
            }
            do {
                if espressu_gp_title_running() { throw ImportError.gameRunning }
                let isZip = url.pathExtension.lowercased() == "zip"
                let baseName = Self.cleanFolderName(isZip ? url.deletingPathExtension().lastPathComponent : url.lastPathComponent)
                let staged = work.appendingPathComponent(baseName, isDirectory: true)
                try fm.createDirectory(at: work, withIntermediateDirectories: true)

                if isZip {
                    let archive = try MiniZip(url: url)
                    try GraphicPackStorage.require(archive.entries.reduce(Int64(0)) { $0 + Int64($1.size) } * 2, what: "import this zip")
                    try archive.extract(to: staged)
                } else {
                    try GraphicPackStorage.require(Self.folderSize(url) * 2, what: "import this folder")
                    try fm.copyItem(at: url, to: staged)
                }

                let packCount = Self.countPacks(in: staged)
                guard packCount > 0 else { throw ImportError.noPacks }

                let destination = GraphicPackPaths.imported.appendingPathComponent(baseName, isDirectory: true)
                try fm.createDirectory(at: GraphicPackPaths.imported, withIntermediateDirectories: true)
                try Self.swap(staged, into: destination)
                DispatchQueue.main.async {
                    self?.reload()
                    completion(.success(ImportResult(packCount: packCount, folderName: baseName)))
                }
            } catch {
                DispatchQueue.main.async { completion(.failure(error)) }
            }
        }
    }

    func removeImported(_ pack: GraphicPack) {
        guard pack.isImported, !espressu_gp_title_running() else { return }
        let marker = "graphicPacks/imported/"
        guard let range = pack.path.range(of: marker) else { return }
        let rest = pack.path[range.upperBound...]
        guard let folder = rest.split(separator: "/").first else { return }
        let target = GraphicPackPaths.imported.appendingPathComponent(String(folder), isDirectory: true)
        workQueue.async { [weak self] in
            try? FileManager.default.removeItem(at: target)
            DispatchQueue.main.async { self?.reload() }
        }
    }

    enum ImportError: LocalizedError {
        case noPacks
        case gameRunning

        var errorDescription: String? {
            switch self {
            case .noPacks: return "No graphic pack found. A pack is a folder with a rules.txt file in it; pick that folder, a folder of packs, or a zip of either."
            case .gameRunning: return "A game is running. Close it before importing packs."
            }
        }
    }

    private static func cleanFolderName(_ raw: String) -> String {
        let cleaned = raw.replacingOccurrences(of: "/", with: "-").trimmingCharacters(in: CharacterSet(charactersIn: ". \n\t"))
        return cleaned.isEmpty ? "Imported pack" : String(cleaned.prefix(80))
    }

    private static func folderSize(_ url: URL) -> Int64 {
        guard let walker = FileManager.default.enumerator(at: url, includingPropertiesForKeys: [.fileSizeKey]) else { return 0 }
        var total: Int64 = 0
        for case let file as URL in walker {
            total += Int64((try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
        }
        return total
    }

    func clearNotice() { notice = nil }
}

extension GraphicPackStore: URLSessionDownloadDelegate {
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        let expected = totalBytesExpectedToWrite > 0 ? totalBytesExpectedToWrite : (latest?.size ?? 0)
        let fraction = expected > 0 ? min(1, Double(totalBytesWritten) / Double(expected)) : 0
        DispatchQueue.main.async {
            if case .downloading = self.phase { self.phase = .downloading(fraction) }
            else if self.phase == .idle { self.phase = .downloading(fraction) }
        }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        let fm = FileManager.default
        if let http = downloadTask.response as? HTTPURLResponse, http.statusCode != 200 {
            DispatchQueue.main.async {
                self.phase = .idle
                self.notice = Notice(text: "GitHub answered the download with an error (\(http.statusCode)).", isError: true)
            }
            return
        }
        do {
            try fm.createDirectory(at: GraphicPackPaths.scratch, withIntermediateDirectories: true)
            try? fm.removeItem(at: Self.pendingZip)
            try fm.moveItem(at: location, to: Self.pendingZip)
        } catch {
            DispatchQueue.main.async {
                self.phase = .idle
                self.notice = Notice(text: "The download finished but couldn't be saved: \(error.localizedDescription)", isError: true)
            }
            return
        }
        DispatchQueue.main.async {
            // finishPendingInstallIfPossible only starts from .idle. Left at .downloading(1.0) it returned at once,
            // so the progress bar sat at 100% and the pack was never installed.
            if case .downloading = self.phase { self.phase = .idle }
            self.finishPendingInstallIfPossible()
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        guard let error else { return }
        let cancelled = (error as? URLError)?.code == .cancelled
        DispatchQueue.main.async {
            if case .downloading = self.phase { self.phase = .idle }
            if !cancelled && !self.cancelRequested {
                self.notice = Notice(text: "\(Self.describe(error)) Your installed packs weren't changed.", isError: true)
            }
        }
    }
}
