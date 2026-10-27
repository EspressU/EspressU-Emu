// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

enum CoverSource { case ask, photos, files }

struct CoverRequest {
    let game: GameMetadata
    var source: CoverSource = .ask
}

/// Lets the user pick a cover image from the photo library or from the Files app. Nothing is downloaded.
/// The system photo picker runs outside the app, so no photo permission is needed.
struct CoverFileImporter: ViewModifier {
    @Binding var request: CoverRequest?
    @ObservedObject var gameManager: GameManager
    @State private var errorMessage: String?
    @State private var chosenGame: GameMetadata?
    @State private var askingSource = false
    @State private var showingPhotos = false
    @State private var showingFiles = false
    @State private var photoItem: PhotosPickerItem?

    func body(content: Content) -> some View {
        content
            .onValueChange(of: request == nil) { gone in
                guard !gone, let request else { return }
                chosenGame = request.game
                self.request = nil
                switch request.source {
                case .ask: askingSource = true
                case .photos: showingPhotos = true
                case .files: showingFiles = true
                }
            }
            .confirmationDialog("Choose a cover", isPresented: $askingSource, titleVisibility: .visible) {
                Button("Photo Library") { showingPhotos = true }
                Button("Files") { showingFiles = true }
                if let target = chosenGame, gameManager.hasManualCoverOverride(forGameID: target.id) {
                    Button("Reset cover", role: .destructive) { gameManager.removeManualCover(forGameID: target.id) }
                }
                Button("Cancel", role: .cancel) {}
            }
            .photosPicker(isPresented: $showingPhotos, selection: $photoItem, matching: .images)
            .onValueChange(of: photoItem) { item in
                guard let item, let target = chosenGame else { return }
                photoItem = nil
                Task {
                    if let data = try? await item.loadTransferable(type: Data.self) {
                        apply(data: data, to: target)
                    } else {
                        errorMessage = "That photo couldn't be loaded."
                    }
                }
            }
            .fileImporter(isPresented: $showingFiles, allowedContentTypes: [.image]) { result in
                guard let target = chosenGame else { return }
                switch result {
                case .success(let url):
                    let scoped = url.startAccessingSecurityScopedResource()
                    defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                    guard let data = try? Data(contentsOf: url) else {
                        errorMessage = "Couldn't read that file."
                        return
                    }
                    apply(data: data, to: target)
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
            .alert("Couldn't use that image", isPresented: Binding(get: { errorMessage != nil },
                                                                   set: { if !$0 { errorMessage = nil } })) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(errorMessage ?? "")
            }
    }

    private func apply(data: Data, to target: GameMetadata) {
        guard let prepared = CoverImageLoader.prepareCustomCover(from: data) else {
            errorMessage = "That doesn't look like a valid image."
            return
        }
        do {
            try gameManager.setManualCover(imageData: prepared.data, ext: prepared.ext, forGameID: target.id)
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

extension View {
    func coverFileImporter(request: Binding<CoverRequest?>, gameManager: GameManager) -> some View {
        modifier(CoverFileImporter(request: request, gameManager: gameManager))
    }
}
