// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

enum AccountImport {
    struct Candidate {
        let persistentId: UInt32
        let data: Data
        let source: String
    }

    struct Prepared {
        var accounts: [Candidate] = []
        var consoleFiles: [URL] = []
        var problems: [String] = []
    }

    private static let minPersistentId: UInt32 = 0x80000001

    static var actURL: URL? {
        WiiUMenu.mlc01URL?.appendingPathComponent("usr/save/system/act")
    }

    private static func persistentId(folderName: String) -> UInt32? {
        guard folderName.count == 8, let value = UInt32(folderName, radix: 16), value >= minPersistentId else { return nil }
        return value
    }

    private static func persistentId(inside data: Data) -> UInt32? {
        guard let text = String(data: data, encoding: .utf8) else { return nil }
        for line in text.split(whereSeparator: \.isNewline) where line.hasPrefix("PersistentId=") {
            if let value = UInt32(line.dropFirst("PersistentId=".count).trimmingCharacters(in: .whitespaces), radix: 16),
               value >= minPersistentId {
                return value
            }
        }
        return nil
    }

    private static func candidate(file: URL, folderId: UInt32?, label: String) -> Candidate? {
        guard let data = try? Data(contentsOf: file), !data.isEmpty,
              String(data: data.prefix(24), encoding: .utf8) == "AccountInstance_20120705"
        else { return nil }
        guard let id = folderId ?? persistentId(inside: data) else { return nil }
        return Candidate(persistentId: id, data: data, source: label)
    }

    static func prepare(urls: [URL]) -> Prepared {
        var result = Prepared()
        let fm = FileManager.default
        for url in urls {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: url.path, isDirectory: &isDir) else { continue }
            let name = url.lastPathComponent
            if isDir.boolValue {
                let direct = url.appendingPathComponent("account.dat")
                if fm.fileExists(atPath: direct.path) {
                    if let c = candidate(file: direct, folderId: persistentId(folderName: name.lowercased()), label: name) {
                        result.accounts.append(c)
                    } else {
                        result.problems.append("\(name)/account.dat isn't a readable Wii U account file, or its folder name isn't an 8-digit account ID like 80000001.")
                    }
                } else {
                    var found = false
                    for child in ((try? fm.contentsOfDirectory(atPath: url.path)) ?? []).sorted() {
                        guard let id = persistentId(folderName: child.lowercased()) else { continue }
                        let file = url.appendingPathComponent(child).appendingPathComponent("account.dat")
                        guard fm.fileExists(atPath: file.path) else { continue }
                        found = true
                        if let c = candidate(file: file, folderId: id, label: child) {
                            result.accounts.append(c)
                        } else {
                            result.problems.append("\(child)/account.dat isn't a readable Wii U account file.")
                        }
                    }
                    if !found { result.problems.append("\(name) has no account.dat in it.") }
                }
            } else {
                let lower = name.lowercased()
                if lower.contains("otp") || lower.contains("seeprom") {
                    result.consoleFiles.append(url)
                } else if let c = candidate(file: url,
                                            folderId: persistentId(folderName: url.deletingLastPathComponent().lastPathComponent.lowercased()),
                                            label: name) {
                    result.accounts.append(c)
                } else {
                    result.problems.append("\(name) isn't a Wii U account.dat, otp.bin or seeprom.bin. If it is an account.dat, pick its 800000XX folder instead so EspressU can read the account ID.")
                }
            }
        }
        return result
    }

    static func exists(_ persistentId: UInt32) -> Bool {
        guard let act = actURL else { return false }
        return FileManager.default.fileExists(atPath: act.appendingPathComponent(String(format: "%08x", persistentId) + "/account.dat").path)
    }

    struct Installed {
        let persistentId: UInt32
        let backupName: String?
        let onlineProblem: String?
    }

    static func install(_ candidate: Candidate) throws -> Installed {
        guard let act = actURL else { throw WiiUMenu.ImportError.accessDenied }
        let fm = FileManager.default
        let folder = act.appendingPathComponent(String(format: "%08x", candidate.persistentId))
        let file = folder.appendingPathComponent("account.dat")
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        var backup: URL?
        if fm.fileExists(atPath: file.path) {
            let name = "account.dat.backup-\(WiiUMenu.timestamp())"
            let target = folder.appendingPathComponent(name)
            try fm.moveItem(at: file, to: target)
            backup = target
        }
        func rollback() {
            try? fm.removeItem(at: file)
            if let backup { try? fm.moveItem(at: backup, to: file) } else { try? fm.removeItem(at: folder) }
            cemu_bridge_accounts_refresh()
        }
        do {
            try candidate.data.write(to: file, options: .atomic)
        } catch {
            rollback()
            throw WiiUMenu.ImportError.failed("Couldn't save the account: \(error.localizedDescription)")
        }
        cemu_bridge_accounts_refresh()
        guard Account.loadAll().contains(where: { $0.persistentId == candidate.persistentId }) else {
            rollback()
            throw WiiUMenu.ImportError.failed("\(candidate.source) couldn't be read as a Wii U account, so it wasn't installed. Your existing accounts are unchanged.")
        }
        let problem: String?
        switch cemu_bridge_account_online_error(candidate.persistentId) {
        case 0: problem = nil
        case 2, 3: problem = "it has no saved password for its Pretendo or Nintendo Network ID"
        case 4: problem = "it has no principal ID"
        default: problem = "it has no linked Pretendo or Nintendo Network ID"
        }
        return Installed(persistentId: candidate.persistentId, backupName: backup?.lastPathComponent, onlineProblem: problem)
    }
}

struct AccountImportButton: View {
    let locked: Bool
    let onChange: () -> Void
    var external: Binding<Bool>? = nil

    @State private var showingImporter = false
    @State private var prepared = AccountImport.Prepared()
    @State private var showingReplace = false
    @State private var resultTitle = ""
    @State private var resultMessage: String?
    @State private var activateCandidate: UInt32?

    var body: some View {
        Group {
            if external == nil {
                Button {
                    showingImporter = true
                } label: {
                    Label("Import from Wii U", systemImage: "square.and.arrow.down")
                }
                .disabled(locked)
            } else {
                Color.clear.frame(width: 1, height: 1).accessibilityHidden(true)
            }
        }
        .fileImporter(isPresented: external ?? $showingImporter, allowedContentTypes: [.item, .folder], allowsMultipleSelection: true) { result in
            switch result {
            case .failure(let error):
                resultTitle = "Couldn't import"
                resultMessage = error.localizedDescription
            case .success(let urls):
                let found = AccountImport.prepare(urls: urls)
                if found.accounts.contains(where: { AccountImport.exists($0.persistentId) }) {
                    prepared = found
                    showingReplace = true
                } else {
                    run(found, replacing: true)
                }
            }
        }
        .confirmationDialog("Replace an existing account?", isPresented: $showingReplace, titleVisibility: .visible) {
            Button("Replace and keep a backup") { run(prepared, replacing: true) }
            Button("Skip those accounts") { run(prepared, replacing: false) }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("An account with the same ID is already installed. If you replace it, its account.dat is saved next to the new one first.")
        }
        .alert(resultTitle, isPresented: Binding(get: { resultMessage != nil }, set: { if !$0 { resultMessage = nil } })) {
            Button("OK") { resultMessage = nil; offerActivation() }
        } message: {
            Text(resultMessage ?? "")
        }
        .alert("Use this account?", isPresented: Binding(get: { activateCandidate != nil }, set: { if !$0 { activateCandidate = nil } })) {
            Button("Make active") {
                if let id = activateCandidate { cemu_bridge_set_active_account_persistent_id(id) }
                activateCandidate = nil
                onChange()
            }
            Button("Not now", role: .cancel) { activateCandidate = nil }
        } message: {
            Text("Make the imported account the active one? Then set Network Service to Pretendo Network below.")
        }
    }

    @State private var pendingActivation: UInt32?

    private func offerActivation() {
        if let id = pendingActivation, id != cemu_bridge_active_account_persistent_id() {
            activateCandidate = id
        }
        pendingActivation = nil
    }

    private func run(_ found: AccountImport.Prepared, replacing: Bool) {
        var lines: [String] = found.problems
        var firstValid: UInt32?
        var firstAny: UInt32?
        for candidate in found.accounts {
            if !replacing && AccountImport.exists(candidate.persistentId) {
                lines.append("Skipped account \(String(format: "%08x", candidate.persistentId)): it's already installed.")
                continue
            }
            do {
                let done = try AccountImport.install(candidate)
                let id = String(format: "%08x", done.persistentId)
                if let backup = done.backupName { lines.append("The old account \(id) was saved as \(backup).") }
                if let problem = done.onlineProblem {
                    lines.append("Account \(id) was imported, but it can't play online: \(problem). Link a PNID on the console first, then dump account.dat again.")
                } else {
                    lines.append("Account \(id) was imported and can play online.")
                    if firstValid == nil { firstValid = done.persistentId }
                }
                if firstAny == nil { firstAny = done.persistentId }
            } catch {
                lines.append(error.localizedDescription)
            }
        }
        if !found.consoleFiles.isEmpty {
            do {
                lines.append(contentsOf: try WiiUMenu.importConsoleFiles(from: found.consoleFiles))
            } catch {
                lines.append(error.localizedDescription)
            }
        }
        if lines.isEmpty { lines.append("Nothing to import. Pick account.dat (or its 800000XX folder), otp.bin and seeprom.bin.") }
        let missing = OnlineReadiness.missingItems(accountId: firstValid ?? cemu_bridge_active_account_persistent_id())
        if !missing.isEmpty && (!found.accounts.isEmpty || !found.consoleFiles.isEmpty) {
            lines.append("Still missing for online play: " + missing.joined(separator: ", ") + ".")
        }
        pendingActivation = firstValid ?? firstAny
        resultTitle = "Import"
        resultMessage = lines.joined(separator: "\n")
        onChange()
    }
}
