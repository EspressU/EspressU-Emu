// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

struct CoverArtPickerView: View {
    let game: GameMetadata
    @ObservedObject var gameManager: GameManager
    @Environment(\.dismiss) private var dismiss

    @State private var showingLegacyPhotoPicker = false
    @State private var showingFileImporter = false
    @State private var errorMessage: String?
    @State private var showingRemoveConfirmation = false

    @State private var tdbIdText = ""
    @State private var isFetchingTdb = false
    @State private var tdbFetchAttempted = false
    @State private var tdbPreviewImage: UIImage?
    @State private var tdbFetchedData: Data?
    @State private var tdbFetchedExt: String?

    private var hasOverride: Bool {
        gameManager.hasManualCoverOverride(forGameID: game.id)
    }

    var body: some View {
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                Form {
                    currentCoverSection
                    photosSection
                    filesSection
                    gameTdbSection
                }
            }
            .navigationTitle("Change Cover Art")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
        .navigationViewStyle(.stack)
        .foregroundColor(EspressUTheme.brownDarkest)
        .fileImporter(isPresented: $showingFileImporter, allowedContentTypes: [.image]) { result in
            handleFileImportResult(result)
        }
        #if os(iOS)
        .sheet(isPresented: $showingLegacyPhotoPicker) {
            LegacyPhotoPicker { picked in
                showingLegacyPhotoPicker = false
                guard let picked, let jpegData = picked.jpegData(compressionQuality: 0.95) else { return }
                handlePickedImageData(jpegData)
            }
        }
        #endif
        .alert("Error", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(errorMessage ?? "")
        }
        .confirmationDialog("Remove custom cover?", isPresented: $showingRemoveConfirmation, titleVisibility: .visible) {
            Button("Remove Custom Cover", role: .destructive) {
                gameManager.removeManualCover(forGameID: game.id)
                dismiss()
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Your custom cover will be deleted. The game goes back to its automatic cover, or the placeholder.")
        }
    }

    @ViewBuilder
    private var currentCoverSection: some View {
        Section {
            HStack(spacing: 12) {
                coverThumbnail
                VStack(alignment: .leading, spacing: 4) {
                    Text(game.displayTitle ?? game.title)
                        .font(.system(size: 14, weight: .semibold, design: .rounded))
                    Text(hasOverride ? "Using your custom cover." : "Using the automatic cover.")
                        .font(.system(size: 12, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                }
                Spacer()
            }
            if hasOverride {
                Button(role: .destructive) {
                    showingRemoveConfirmation = true
                } label: {
                    Label("Remove Custom Cover", systemImage: "photo.badge.minus")
                }
            }
        }
    }

    @ViewBuilder
    private var coverThumbnail: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .fill(EspressUTheme.cremaTopGradient)
            if let coverPath = game.coverPath {
                CoverImage(path: coverPath, padding: 4)
            } else {
                Image(systemName: "gamecontroller.fill")
                    .foregroundColor(EspressUTheme.onCremaTop)
            }
        }
        .frame(width: 48, height: 64)
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    @ViewBuilder
    private var photosSection: some View {
        Section {
            if #available(iOS 16.0, *) {
                ModernPhotoPickerButton(
                    onPicked: { data in handlePickedImageData(data) },
                    onError: { message in errorMessage = message }
                )
            } else {
                Button {
                    showingLegacyPhotoPicker = true
                } label: {
                    Label("Choose from Photos", systemImage: "photo.on.rectangle")
                }
                .buttonStyle(EspressUSecondaryButtonStyle())
            }
        } header: {
            Text("From Photos")
        }
    }

    @ViewBuilder
    private var filesSection: some View {
        Section {
            Button {
                showingFileImporter = true
            } label: {
                Label("Import a File", systemImage: "folder")
            }
            .buttonStyle(EspressUSecondaryButtonStyle())
        } header: {
            Text("From Files")
        } footer: {
            Text("Pick an image from Files, iCloud Drive, or another app.")
        }
    }

    @ViewBuilder
    private var gameTdbSection: some View {
        Section {
            TextField("Game ID, e.g. AGBE01", text: $tdbIdText)
                #if os(iOS)
                .textInputAutocapitalization(.characters)
                .keyboardType(.asciiCapable)
                #endif
                .autocorrectionDisabled()
                .font(.body.monospaced())

            Button {
                fetchFromGameTDB()
            } label: {
                if isFetchingTdb {
                    HStack {
                        ProgressView()
                        Text("Fetching\u{2026}")
                    }
                } else {
                    Text("Fetch")
                }
            }
            .buttonStyle(EspressUSecondaryButtonStyle())
            .disabled(isFetchingTdb || tdbIdText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

            if let tdbPreviewImage {
                VStack(alignment: .leading, spacing: 10) {
                    Image(uiImage: tdbPreviewImage)
                        .resizable()
                        .scaledToFit()
                        .frame(maxHeight: 180)
                        .frame(maxWidth: .infinity)
                        .background(EspressUTheme.cremaTopGradient)
                        .cornerRadius(10)
                    Button {
                        commitTdbFetchedCover()
                    } label: {
                        Text("Use This Cover")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(EspressUPrimaryButtonStyle())
                }
            } else if tdbFetchAttempted && !isFetchingTdb {
                Text("No cover art found for that ID.")
                    .font(.system(size: 13, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
            }
        } header: {
            Text("Try a Specific GameTDB ID")
        } footer: {
            InfoButton.footer(
                "Look up your game's ID on gametdb.com.",
                title: "GameTDB Game ID",
                text: "GameTDB has no search feature we can use. Find your game at https://www.gametdb.com/WiiU/CoverArt, copy its 6-character Game ID (for example AGBE01) and enter it above. Fetch shows a preview before anything is saved."
            )
        }
    }

    private func handlePickedImageData(_ data: Data) {
        guard let prepared = CoverImageLoader.prepareCustomCover(from: data) else {
            errorMessage = "That doesn't look like a valid image."
            return
        }
        applyImageData(prepared.data, ext: prepared.ext)
    }

    private func handleFileImportResult(_ result: Result<URL, Error>) {
        switch result {
        case .failure(let error):
            errorMessage = "Couldn't import that file: \(error.localizedDescription)"
        case .success(let url):
            let accessed = url.startAccessingSecurityScopedResource()
            defer { if accessed { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else {
                errorMessage = "Couldn't read that file."
                return
            }
            handlePickedImageData(data)
        }
    }

    private func fetchFromGameTDB() {
        let id = tdbIdText.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard !id.isEmpty else { return }

        isFetchingTdb = true
        tdbFetchAttempted = false
        tdbPreviewImage = nil
        tdbFetchedData = nil
        tdbFetchedExt = nil

        Task {
            do {
                let found = try await CoverArtFetcher.fetchArt(forGameTdbId: id)
                await MainActor.run {
                    isFetchingTdb = false
                    tdbFetchAttempted = true
                    if let found, let image = UIImage(data: found.data) {
                        tdbPreviewImage = image
                        tdbFetchedData = found.data
                        tdbFetchedExt = found.ext
                    }
                }
            } catch {
                await MainActor.run {
                    isFetchingTdb = false
                    tdbFetchAttempted = false
                    errorMessage = error.localizedDescription
                }
            }
        }
    }

    private func commitTdbFetchedCover() {
        guard let data = tdbFetchedData, let ext = tdbFetchedExt else { return }
        applyImageData(data, ext: ext)
    }

    private func applyImageData(_ data: Data, ext: String) {
        do {
            try gameManager.setManualCover(imageData: data, ext: ext, forGameID: game.id)
            dismiss()
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

@available(iOS 16.0, *)
private struct ModernPhotoPickerButton: View {
    var onPicked: (Data) -> Void
    var onError: (String) -> Void
    @State private var item: PhotosPickerItem?

    var body: some View {
        PhotosPicker(selection: $item, matching: .images) {
            Label("Choose from Photos", systemImage: "photo.on.rectangle")
        }
        .buttonStyle(EspressUSecondaryButtonStyle())
        .onValueChange(of: item) { newItem in
            guard let newItem else { return }
            Task {
                guard let data = try? await newItem.loadTransferable(type: Data.self) else {
                    await MainActor.run { onError("Couldn't load that photo.") }
                    return
                }
                await MainActor.run { onPicked(data) }
            }
        }
    }
}

#if os(iOS)
struct LegacyPhotoPicker: UIViewControllerRepresentable {
    var onPick: (UIImage?) -> Void

    func makeUIViewController(context: Context) -> UIImagePickerController {
        let picker = UIImagePickerController()
        picker.sourceType = .photoLibrary
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ uiViewController: UIImagePickerController, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(onPick: onPick) }

    final class Coordinator: NSObject, UIImagePickerControllerDelegate, UINavigationControllerDelegate {
        let onPick: (UIImage?) -> Void
        init(onPick: @escaping (UIImage?) -> Void) { self.onPick = onPick }

        func imagePickerController(
            _ picker: UIImagePickerController,
            didFinishPickingMediaWithInfo info: [UIImagePickerController.InfoKey: Any]
        ) {
            onPick(info[.originalImage] as? UIImage)
        }

        func imagePickerControllerDidCancel(_ picker: UIImagePickerController) {
            onPick(nil)
        }
    }
}
#endif
