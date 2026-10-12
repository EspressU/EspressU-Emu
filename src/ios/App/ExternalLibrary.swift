// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import SwiftUI

struct ExternalGameRecord: Codable, Equatable {
    var id: String
    var itemRel: String
    var bootRel: String
    var titleId: UInt64?
}

struct LinkedLocation: Codable, Identifiable, Equatable {
    enum Kind: String, Codable { case folder, file }

    let id: String
    var kind: Kind
    var name: String
    var bookmark: Data
    var lastPath: String
    var hidden: [String] = []
    var games: [ExternalGameRecord] = []
}

final class ExternalLibrary: ObservableObject, @unchecked Sendable {
    static let shared = ExternalLibrary()

    @Published private(set) var locations: [LinkedLocation] = []
    @Published private(set) var availability: [String: Bool] = [:]

    private let lock = NSLock()
    private var stored: [LinkedLocation] = []
    private var resolved: [String: URL] = [:]
    private let fileURL: URL

    static let unavailableMessage = "Connect the drive to play"

    private init() {
        let base = (try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true))
            ?? FileManager.default.temporaryDirectory
        let folder = base.appendingPathComponent("EspressU", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        fileURL = folder.appendingPathComponent("LinkedLocations.json")
        if let data = try? Data(contentsOf: fileURL),
           let decoded = try? JSONDecoder().decode([LinkedLocation].self, from: data) {
            stored = decoded
            locations = decoded
        }
    }

    func snapshot() -> [LinkedLocation] {
        lock.lock(); defer { lock.unlock() }
        return stored
    }

    func location(_ id: String) -> LinkedLocation? {
        lock.lock(); defer { lock.unlock() }
        return stored.first { $0.id == id }
    }

    private func mutate(_ body: (inout [LinkedLocation]) -> Void) {
        lock.lock()
        let before = stored
        body(&stored)
        let after = stored
        lock.unlock()
        guard after != before else { return }
        if let data = try? JSONEncoder().encode(after) {
            try? data.write(to: fileURL, options: .atomic)
        }
        DispatchQueue.main.async { self.locations = after }
    }

    func setAvailability(_ value: [String: Bool]) {
        DispatchQueue.main.async { if self.availability != value { self.availability = value } }
    }

    struct Hold {
        let locationID: String
        let url: URL
        fileprivate let scoped: Bool
    }

    func acquire(_ id: String, reresolve: Bool = false) -> Hold? {
        guard let url = resolve(id, force: reresolve) else { return nil }
        let scoped = url.startAccessingSecurityScopedResource()
        return Hold(locationID: id, url: url, scoped: scoped)
    }

    func release(_ hold: Hold) {
        if hold.scoped { hold.url.stopAccessingSecurityScopedResource() }
    }

    private func resolve(_ id: String, force: Bool) -> URL? {
        lock.lock()
        if !force, let cached = resolved[id] { lock.unlock(); return cached }
        let bookmark = stored.first { $0.id == id }?.bookmark
        lock.unlock()
        guard let bookmark else { return nil }
        var stale = false
        guard let url = try? URL(resolvingBookmarkData: bookmark, options: [.withoutUI], relativeTo: nil, bookmarkDataIsStale: &stale) else {
            lock.lock(); resolved[id] = nil; lock.unlock()
            return nil
        }
        lock.lock(); resolved[id] = url; lock.unlock()
        if stale { refreshBookmark(id, url) }
        return url
    }

    private func refreshBookmark(_ id: String, _ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil) else { return }
        mutate { list in
            if let index = list.firstIndex(where: { $0.id == id }) {
                list[index].bookmark = data
                list[index].lastPath = url.path
            }
        }
    }

    static func isReachable(path: String) -> Bool {
        FileManager.default.fileExists(atPath: path)
    }

    func probe(_ id: String, path: String) -> Bool {
        guard let hold = acquire(id, reresolve: true) else { return false }
        defer { release(hold) }
        return Self.isReachable(path: path)
    }

    enum LinkError: LocalizedError {
        case accessDenied
        case notReadable(String)
        case unsupportedFile(String)
        case bookmarkFailed(Error)

        var errorDescription: String? {
            switch self {
            case .accessDenied:
                return "Couldn't access that location."
            case .notReadable(let name):
                return "EspressU can't read \"\(name)\". If it's on a drive, make sure the drive is connected."
            case .unsupportedFile(let name):
                return "\"\(name)\" isn't a Wii U game file EspressU can read. It accepts .wua, .wux, .wud, .iso, .rpx, .elf and .wuhb files."
            case .bookmarkFailed(let error):
                return "Couldn't keep access to that location: \(error.localizedDescription)"
            }
        }
    }

    @discardableResult
    func link(_ picked: URL) throws -> LinkedLocation {
        let scoped = picked.startAccessingSecurityScopedResource()
        defer { if scoped { picked.stopAccessingSecurityScopedResource() } }

        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: picked.path, isDirectory: &isDirectory) else {
            throw LinkError.accessDenied
        }
        let kind: LinkedLocation.Kind = isDirectory.boolValue ? .folder : .file
        switch kind {
        case .folder:
            guard (try? FileManager.default.contentsOfDirectory(atPath: picked.path)) != nil else {
                throw LinkError.notReadable(picked.lastPathComponent)
            }
        case .file:
            guard GameManager.supportedROMExtensions.contains(picked.pathExtension.lowercased()),
                  GameManager.isValidROMFile(at: picked) else {
                throw LinkError.unsupportedFile(picked.lastPathComponent)
            }
        }

        let path = picked.standardizedFileURL.path
        if let existing = snapshot().first(where: { $0.lastPath == path && $0.kind == kind }) {
            return existing
        }

        let data: Data
        do {
            data = try picked.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
        } catch {
            throw LinkError.bookmarkFailed(error)
        }
        let location = LinkedLocation(id: UUID().uuidString, kind: kind, name: picked.lastPathComponent, bookmark: data, lastPath: path)
        mutate { $0.append(location) }
        lock.lock(); resolved[location.id] = picked; lock.unlock()
        return location
    }

    func unlink(_ id: String) {
        lock.lock(); resolved[id] = nil; lock.unlock()
        mutate { $0.removeAll { $0.id == id } }
    }

    func removeFromLibrary(locationID: String, gameID: String) {
        guard let location = location(locationID) else { return }
        if location.kind == .file { unlink(locationID); return }
        guard let record = location.games.first(where: { $0.id == gameID }) else { return }
        mutate { list in
            if let index = list.firstIndex(where: { $0.id == locationID }), !list[index].hidden.contains(record.itemRel) {
                list[index].hidden.append(record.itemRel)
                list[index].games.removeAll { $0.id == gameID }
            }
        }
    }

    func restoreHidden(_ id: String) {
        mutate { list in
            if let index = list.firstIndex(where: { $0.id == id }) { list[index].hidden = [] }
        }
    }

    func updateRecords(_ id: String, _ records: [ExternalGameRecord], path: String) {
        mutate { list in
            if let index = list.firstIndex(where: { $0.id == id }) {
                list[index].games = records
                list[index].lastPath = path
            }
        }
    }

    func discover(_ location: LinkedLocation, root: URL) -> [URL]? {
        switch location.kind {
        case .file:
            return FileManager.default.fileExists(atPath: root.path) ? [root] : nil
        case .folder:
            guard (try? FileManager.default.contentsOfDirectory(atPath: root.path)) != nil else { return nil }
            var found: [URL] = []
            Self.walk(root, depth: 0, into: &found)
            return found
        }
    }

    private static let maxDepth = 10
    private static let skippedFolderNames: Set<String> = ["$recycle.bin", "system volume information", "lost+found"]

    private static func walk(_ directory: URL, depth: Int, into found: inout [URL]) {
        if GameManager.looksLikeWiiUDump(directory) {
            if GameManager.executableInDump(directory) != nil { found.append(directory) }
            return
        }
        if GameManager.titleTmdInDump(directory) != nil {
            found.append(directory)
            return
        }
        let tmdChildren = GameManager.nusTitleFolders(in: directory, maxDepth: 1)
        if tmdChildren.filter({ $0.kind == .base }).count == 1, tmdChildren.count > 1 {
            found.append(directory)
            return
        }
        guard depth < maxDepth,
              let children = try? FileManager.default.contentsOfDirectory(
                  at: directory, includingPropertiesForKeys: [.isDirectoryKey], options: [])
        else { return }
        for child in children.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
            let name = child.lastPathComponent
            if name.hasPrefix(".") || skippedFolderNames.contains(name.lowercased()) { continue }
            let isDirectory = (try? child.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
            if isDirectory {
                walk(child, depth: depth + 1, into: &found)
            } else if GameManager.supportedROMExtensions.contains(child.pathExtension.lowercased()) {
                found.append(child)
            }
        }
    }

    static func relativePath(of url: URL, under root: URL) -> String {
        let path = url.path
        let base = root.path
        if path == base { return "" }
        if path.hasPrefix(base + "/") { return String(path.dropFirst(base.count + 1)) }
        return url.lastPathComponent
    }
}
