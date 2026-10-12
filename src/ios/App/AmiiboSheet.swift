import SwiftUI
import UniformTypeIdentifiers

enum AmiiboStore {
    static let maxBytes = 600

    static var directory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("amiibo", isDirectory: true)
    }

    static func list() -> [URL] {
        let urls = (try? FileManager.default.contentsOfDirectory(
            at: directory, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles])) ?? []
        return urls
            .filter { !$0.hasDirectoryPath }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    enum ImportError: LocalizedError {
        case unreadable, tooLarge, writeFailed

        var errorDescription: String? {
            switch self {
            case .unreadable: return "EspressU couldn't read that file."
            case .tooLarge: return "That file is too big to be an amiibo dump (they are about 540 bytes)."
            case .writeFailed: return "EspressU couldn't save a copy in Documents/amiibo."
            }
        }
    }

    static func importFile(_ source: URL) throws -> URL {
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }

        let size = (try? source.resourceValues(forKeys: [.fileSizeKey]))?.fileSize
        if let size, size > maxBytes { throw ImportError.tooLarge }
        guard let data = try? Data(contentsOf: source) else { throw ImportError.unreadable }
        if data.count > maxBytes { throw ImportError.tooLarge }

        let fm = FileManager.default
        do { try fm.createDirectory(at: directory, withIntermediateDirectories: true) }
        catch { throw ImportError.writeFailed }

        let base = source.deletingPathExtension().lastPathComponent
        let ext = source.pathExtension
        var candidate = directory.appendingPathComponent(source.lastPathComponent)
        var number = 2
        while fm.fileExists(atPath: candidate.path) {
            if let existing = try? Data(contentsOf: candidate), existing == data { return candidate }
            let name = ext.isEmpty ? "\(base) \(number)" : "\(base) \(number).\(ext)"
            candidate = directory.appendingPathComponent(name)
            number += 1
        }
        do { try data.write(to: candidate, options: .atomic) }
        catch { throw ImportError.writeFailed }
        return candidate
    }

    static func touch(_ file: URL) -> String? {
        file.path.withCString { cemu_bridge_touch_amiibo($0).map { String(cString: $0) } }
    }
}

struct AmiiboSheet: View {
    let onTapped: (String) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var files = AmiiboStore.list()
    @State private var errorMessage: String?

    var body: some View {
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                List {
                    Section {
                        Button {
                            choose()
                        } label: {
                            Label("Choose amiibo file", systemImage: "folder")
                        }
                        .tint(EspressUTheme.accentText)

                        if let errorMessage {
                            Text(errorMessage)
                                .font(EspressUTheme.Font.caption)
                                .foregroundColor(EspressUTheme.alertText)
                                .fixedSize(horizontal: false, vertical: true)
                                .accessibilityAddTraits(.isStaticText)
                        }
                    } footer: {
                        Text("Pick an amiibo dump (.bin or .nfc). EspressU keeps a copy in Documents/amiibo so you can scan it again with one tap. The game has to be asking for an amiibo when you scan.")
                    }

                    if !files.isEmpty {
                        Section("Saved amiibo") {
                            ForEach(files, id: \.self) { file in
                                Button {
                                    scan(file)
                                } label: {
                                    HStack {
                                        Image(systemName: "wave.3.right")
                                            .accessibilityHidden(true)
                                        Text(file.deletingPathExtension().lastPathComponent)
                                            .lineLimit(1)
                                        Spacer(minLength: 4)
                                    }
                                }
                                .accessibilityHint("Scans this amiibo.")
                            }
                            .onDelete(perform: delete)
                        }
                    }
                }
            }
            .navigationTitle("Scan amiibo")
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
    }

    private func choose() {
        errorMessage = nil
        DocumentImport.present(contentTypes: [.item]) { result in
            switch result {
            case .success(let urls):
                guard let source = urls.first else { return }
                do {
                    let copy = try AmiiboStore.importFile(source)
                    files = AmiiboStore.list()
                    scan(copy)
                } catch {
                    errorMessage = error.localizedDescription
                }
            case .failure(let error):
                errorMessage = error.localizedDescription
            }
        }
    }

    private func scan(_ file: URL) {
        if let reason = AmiiboStore.touch(file) {
            errorMessage = reason
            return
        }
        errorMessage = nil
        onTapped("Scanned \(file.deletingPathExtension().lastPathComponent).")
    }

    private func delete(at offsets: IndexSet) {
        for index in offsets where files.indices.contains(index) {
            try? FileManager.default.removeItem(at: files[index])
        }
        files = AmiiboStore.list()
    }
}
