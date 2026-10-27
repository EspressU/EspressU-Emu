import SwiftUI
import UniformTypeIdentifiers

struct GameContentSection: View {
    let game: GameMetadata
    @State private var hasUpdate = false
    @State private var hasDLC = false
    @State private var confirmRemove: DlcUpdateImport.ContentKind?
    @State private var message: String?
    @State private var packing = false
    @State private var progress = DecryptProgress()
    @State private var pollTimer: Timer?
    @State private var packedFile: URL?
    @State private var showingAddContentPicker = false
    @State private var importMessage: String?
    @State private var importInProgress = false
    @State private var pendingContentKind: DlcUpdateImport.ContentKind?
    @State private var pendingReinstall: (url: URL, kind: DlcUpdateImport.ContentKind)?
    @State private var showingReinstallConfirm = false

    var body: some View {
        Section {
            row("Update", installed: hasUpdate, kind: .update)
            row("DLC", installed: hasDLC, kind: .dlc)

            if importInProgress {
                HStack {
                    ProgressView()
                        .scaleEffect(0.8, anchor: .center)
                    Text("Adding content...")
                        .font(.footnote)
                }
            } else {
                Menu {
                    Button("Update") { selectContentKind(.update) }
                    Button("DLC") { selectContentKind(.dlc) }
                } label: {
                    Label("Add update or DLC...", systemImage: "plus.circle")
                }
                .disabled(cemu_bridge_is_title_running())
            }

            if packing {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Packing into one .wua... \(progress.filesWritten) files, "
                        + ByteCountFormatter.string(fromByteCount: Int64(progress.bytesWritten), countStyle: .file))
                        .font(.footnote)
                    Button("Cancel", role: .destructive) { cemu_bridge_cancel_decrypt() }
                }
            } else {
                Button {
                    startPacking()
                } label: {
                    Label("Pack game, update and DLC into one .wua", systemImage: "shippingbox")
                }
                .disabled(packSources().isEmpty || cemu_bridge_is_title_running())
            }
            if let packedFile {
                Button {
                    DocumentImport.presentExport([packedFile]) { _ in }
                } label: {
                    Label("Save \(packedFile.lastPathComponent)...", systemImage: "square.and.arrow.up")
                }
            }
            if let message {
                Text(message)
                    .font(.caption)
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            if let importMessage {
                Text(importMessage)
                    .font(.caption)
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        } header: {
            Text("Updates & DLC")
        } footer: {
            Text("Packing leaves your installed files where they are.")
        }
        .onAppear { refresh() }
        .onDisappear { pollTimer?.invalidate(); pollTimer = nil }
        .fileImporter(
            isPresented: $showingAddContentPicker,
            allowedContentTypes: [.folder, .item],
            onCompletion: addContent
        )
        .alert(item: Binding(
            get: { confirmRemove.map { RemoveRequest(kind: $0) } },
            set: { confirmRemove = $0?.kind }
        )) { request in
            Alert(
                title: Text("Remove \(request.kind.displayName)?"),
                message: Text("The game itself and your saves are not touched."),
                primaryButton: .destructive(Text("Remove")) { remove(request.kind) },
                secondaryButton: .cancel())
        }
        .confirmationDialog(
            "Reinstall this version?",
            isPresented: $showingReinstallConfirm,
            titleVisibility: .visible
        ) {
            Button("Reinstall", role: .destructive) {
                if let pending = pendingReinstall {
                    install(pending.url, kind: pending.kind, allowReinstall: true)
                }
            }
            Button("Cancel", role: .cancel) { pendingReinstall = nil }
        } message: {
            Text("The same version is already installed. Reinstalling replaces it. Save data is not touched.")
        }
    }

    private struct RemoveRequest: Identifiable {
        let kind: DlcUpdateImport.ContentKind
        var id: String { kind.displayName }
    }

    private func row(_ name: String, installed: Bool, kind: DlcUpdateImport.ContentKind) -> some View {
        HStack {
            Text(name)
            Spacer()
            if installed {
                Text("Installed").foregroundColor(EspressUTheme.secondaryText)
                Button("Remove", role: .destructive) { confirmRemove = kind }
                    .buttonStyle(.borderless)
                    .disabled(cemu_bridge_is_title_running())
            } else {
                Text("None").foregroundColor(EspressUTheme.secondaryText)
            }
        }
    }

    private func refresh() {
        let installed = DlcUpdateImport.installedContent(for: game)
        hasUpdate = installed.hasUpdate
        hasDLC = installed.hasDLC
    }

    private func remove(_ kind: DlcUpdateImport.ContentKind) {
        do {
            _ = try DlcUpdateImport.remove(kind: kind, for: game)
            message = "Removed the \(kind.displayName)."
        } catch {
            message = "Couldn't remove it: \(error.localizedDescription)"
        }
        refresh()
    }

    private func packSources() -> [String] {
        var sources: [String] = []
        if let dump = game.dumpDirectoryPath {
            sources.append(dump)
        } else {
            let ext = (game.romPath as NSString).pathExtension.lowercased()
            guard ext == "wud" || ext == "wux" else { return [] }
            sources.append(game.romPath)
        }
        for kind in [DlcUpdateImport.ContentKind.update, .dlc] {
            if let folder = DlcUpdateImport.installedFolder(kind: kind, for: game) {
                sources.append(folder.path)
            }
        }
        return sources
    }

    private func startPacking() {
        let sources = packSources()
        guard !sources.isEmpty else { return }
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        guard let destination = docs?.appendingPathComponent("Decrypted/\(game.id)-complete.wua") else { return }
        message = nil
        packedFile = nil
        guard cemu_bridge_start_wua_build(sources.joined(separator: "\n"), destination.path) else {
            message = "Another export is already running."
            return
        }
        packing = true
        progress = DecryptProgress()
        pollTimer?.invalidate()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { _ in
            Task { @MainActor in
                let snapshot = DecryptProgress.read()
                progress = snapshot
                guard snapshot.completed else { return }
                pollTimer?.invalidate()
                pollTimer = nil
                packing = false
                if snapshot.isSuccess {
                    packedFile = destination
                    message = "Done. The .wua holds the game with its update and DLC."
                } else {
                    message = failureText(snapshot.resultStatus)
                }
            }
        }
    }

    private func failureText(_ status: Int32) -> String {
        switch status {
        case 1: return "One of the files couldn't be read as a Wii U title."
        case 3: return "Couldn't write the .wua. The device may be out of space."
        case 4: return "Cancelled."
        case 5: return "Some files couldn't be read, so no .wua was made."
        case 9: return "The update or DLC belongs to a different game."
        case 10: return "The same title was added twice."
        default: return "Couldn't build the .wua."
        }
    }

    private func selectContentKind(_ kind: DlcUpdateImport.ContentKind) {
        pendingContentKind = kind
        showingAddContentPicker = true
    }

    private func addContent(result: Result<URL, Error>) {
        importMessage = nil
        guard case .success(let url) = result, let kind = pendingContentKind else {
            importMessage = "Couldn't access that file."
            return
        }
        install(url, kind: kind, allowReinstall: false)
    }

    private func install(_ url: URL, kind: DlcUpdateImport.ContentKind, allowReinstall: Bool) {
        importMessage = nil
        importInProgress = true
        Task {
            do {
                _ = try await DlcUpdateImport.import(
                    from: url, kind: kind, library: [], manualMatch: game,
                    strictMatch: true, allowReinstall: allowReinstall)
                importMessage = "Added the \(kind.displayName)."
            } catch DlcUpdateImport.ImportError.alreadyInstalledSameOrNewer(let installed, let imported)
                where installed == imported && !allowReinstall {
                pendingReinstall = (url, kind)
                showingReinstallConfirm = true
            } catch {
                importMessage = error.localizedDescription
            }
            pendingContentKind = nil
            importInProgress = false
            refresh()
        }
    }
}
