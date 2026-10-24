// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

struct EspressUCoverPicker: View {
    let game: GameMetadata
    @ObservedObject var gameManager: GameManager

    @Environment(\.dismiss) private var dismiss
    @StateObject private var input = ControllerMenuInput()
    @State private var focus = "photos"
    @State private var armed: String?
    @State private var showingPhotos = false
    @State private var showingFiles = false
    @State private var errorMessage: String?
    @State private var tdbId = ""
    @State private var fetching = false
    @State private var fetchAttempted = false
    @State private var preview: UIImage?
    @State private var fetched: (data: Data, ext: String)?
    @FocusState private var typing: Bool

    private var live: GameMetadata { gameManager.games.first { $0.id == game.id } ?? game }
    private var hasOverride: Bool { gameManager.hasManualCoverOverride(forGameID: game.id) }

    private var order: [String] {
        var ids = ["photos", "files"]
        if hasOverride { ids.append("automatic") }
        ids += ["tdbField", "fetch"]
        if preview != nil { ids.append("use") }
        return ids
    }

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width >= 600
            ZStack {
                EspressUBackground()
                    .ignoresSafeArea()
                ScrollViewReader { proxy in
                    ScrollView {
                        VStack(spacing: 24) {
                            header
                            sources(wide: wide)
                            gameTdbCard
                        }
                        .padding(.horizontal, 24)
                        .padding(.top, 34)
                        .padding(.bottom, 30)
                        .frame(maxWidth: 620)
                        .frame(maxWidth: .infinity)
                    }
                    .onValueChange(of: focus) { id in
                        withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(id, anchor: .center) }
                    }
                }
            }
        }
        .overlay(alignment: .topTrailing) {
            EspressUCloseButton(showsHint: input.showsFocus, hintSymbol: input.backSymbol, action: close)
                .padding(16)
        }
        .onAppear {
            input.onCommand = handle
            input.isExclusive = true
            input.start()
        }
        .onDisappear { input.stop() }
        .modifier(CoverPhotosPicker(isPresented: $showingPhotos, onPicked: usePicked, onError: { errorMessage = $0 }))
        .fileImporter(isPresented: $showingFiles, allowedContentTypes: [.image]) { result in
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
                usePicked(data)
            }
        }
        .onValueChange(of: showingPhotos) { open in open ? input.pause() : input.resume() }
        .onValueChange(of: showingFiles) { open in open ? input.pause() : input.resume() }
        .alert("Couldn't change the cover", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(errorMessage ?? "")
        }
    }

    private var header: some View {
        VStack(spacing: 14) {
            EspressUCover(game: live, radius: 28)
                .frame(width: 170, height: 170)
                .shadow(color: EspressUTheme.shadow.opacity(0.3), radius: 16, y: 8)
            VStack(spacing: 4) {
                Text("Cover")
                    .font(.system(size: 28, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                Text(hasOverride ? "\(live.cardName.name) uses your own cover." : "\(live.cardName.name) uses the automatic cover.")
                    .font(.system(size: 15, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .multilineTextAlignment(.center)
            }
        }
    }

    private func sources(wide: Bool) -> some View {
        let columns = Array(repeating: GridItem(.flexible(), spacing: 14), count: wide ? (hasOverride ? 3 : 2) : 2)
        return LazyVGrid(columns: columns, spacing: 14) {
            sourceTile(id: "photos", title: "Photos", detail: "Pick an image", symbol: "photo.on.rectangle") {
                showingPhotos = true
            }
            sourceTile(id: "files", title: "Files", detail: "iCloud or this iPad", symbol: "folder.fill") {
                showingFiles = true
            }
            if hasOverride {
                sourceTile(id: "automatic", title: armed == "automatic" ? "Press again" : "Automatic",
                           detail: "Remove your cover", symbol: "arrow.uturn.backward", destructive: true) {
                    EspressURowLogic.press("automatic", confirm: true, armed: $armed) {
                        gameManager.removeManualCover(forGameID: game.id)
                    }
                }
            }
        }
    }

    private func sourceTile(id: String, title: String, detail: String, symbol: String, destructive: Bool = false,
                            action: @escaping () -> Void) -> some View {
        Button {
            focus = id
            action()
        } label: {
            VStack(spacing: 8) {
                Image(systemName: symbol)
                    .font(.system(size: 26, weight: .semibold))
                    .foregroundColor(destructive ? EspressUTheme.alertText : EspressUTheme.accentText)
                Text(title)
                    .font(.system(size: 17, weight: .bold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                Text(detail)
                    .font(.system(size: 13, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity, minHeight: 118)
            .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .espressUFocusRing(input.showsFocus && focus == id, radius: 22)
        .id(id)
    }

    private var gameTdbCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("GAMETDB")
                .font(.system(size: 13, weight: .heavy, design: .rounded))
                .tracking(0.6)
                .foregroundColor(EspressUTheme.brownMid)
            Text("Find the game on gametdb.com/WiiU/CoverArt and enter its 6-character ID, for example AGBE01.")
                .font(.system(size: 15, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 10) {
                TextField("Game ID", text: $tdbId)
                    .font(.system(size: 18, weight: .semibold, design: .monospaced))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .textInputAutocapitalization(.characters)
                    .keyboardType(.asciiCapable)
                    .autocorrectionDisabled()
                    .submitLabel(.search)
                    .focused($typing)
                    .onSubmit(fetch)
                    .padding(.horizontal, 16)
                    .frame(height: 50)
                    .background(EspressUTheme.wrapper, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                    .espressUFocusRing(input.showsFocus && focus == "tdbField", radius: 16)
                    .id("tdbField")
                Button(action: fetch) {
                    Group {
                        if fetching {
                            ProgressView()
                        } else {
                            Text("Fetch")
                        }
                    }
                    .font(.system(size: 16, weight: .bold, design: .rounded))
                    .foregroundColor(EspressUTheme.accentText)
                    .padding(.horizontal, 20)
                    .frame(height: 50)
                    .background(EspressUTheme.wrapper, in: Capsule())
                }
                .buttonStyle(.plain)
                .disabled(fetching || tdbId.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                .espressUFocusRing(input.showsFocus && focus == "fetch")
                .id("fetch")
            }
            if let preview {
                Image(uiImage: preview)
                    .resizable()
                    .scaledToFit()
                    .frame(maxHeight: 220)
                    .frame(maxWidth: .infinity)
                    .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
                Button(action: useFetched) {
                    HStack(spacing: 8) {
                        if input.showsFocus {
                            Image(systemName: input.confirmSymbol)
                                .accessibilityHidden(true)
                        }
                        Text("Use this cover")
                    }
                    .font(.system(size: 17, weight: .heavy, design: .rounded))
                    .foregroundColor(EspressUTheme.onCremaTop)
                    .frame(maxWidth: .infinity, minHeight: 50)
                    .background(EspressUTheme.cremaTopGradient, in: Capsule())
                }
                .buttonStyle(.plain)
                .espressUFocusRing(input.showsFocus && focus == "use")
                .id("use")
            } else if fetchAttempted && !fetching {
                Text("No cover found for that ID.")
                    .font(.system(size: 14, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
            }
        }
        .padding(18)
        .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
    }

    private func close() {
        typing = false
        input.stop()
        dismiss()
    }

    private func usePicked(_ data: Data) {
        guard let prepared = CoverImageLoader.prepareCustomCover(from: data) else {
            errorMessage = "That doesn't look like a valid image."
            return
        }
        apply(prepared.data, ext: prepared.ext)
    }

    private func fetch() {
        let id = tdbId.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard !id.isEmpty, !fetching else { return }
        typing = false
        fetching = true
        fetchAttempted = false
        preview = nil
        fetched = nil
        Task {
            do {
                let found = try await CoverArtFetcher.fetchArt(forGameTdbId: id)
                await MainActor.run {
                    fetching = false
                    fetchAttempted = true
                    if let found, let image = UIImage(data: found.data) {
                        preview = image
                        fetched = (found.data, found.ext)
                        focus = "use"
                    }
                }
            } catch {
                await MainActor.run {
                    fetching = false
                    errorMessage = error.localizedDescription
                }
            }
        }
    }

    private func useFetched() {
        guard let fetched else { return }
        apply(fetched.data, ext: fetched.ext)
    }

    private func apply(_ data: Data, ext: String) {
        do {
            try gameManager.setManualCover(imageData: data, ext: ext, forGameID: game.id)
            close()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func handle(_ command: ControllerMenuCommand) {
        guard errorMessage == nil else {
            if command == .confirm || command == .back { errorMessage = nil }
            return
        }
        switch command {
        case .up, .left:
            focus = EspressURowLogic.step(focus, in: order, by: -1) ?? "photos"
        case .down, .right:
            focus = EspressURowLogic.step(focus, in: order, by: 1) ?? "photos"
        case .confirm:
            switch focus {
            case "photos": showingPhotos = true
            case "files": showingFiles = true
            case "automatic":
                EspressURowLogic.press("automatic", confirm: true, armed: $armed) {
                    gameManager.removeManualCover(forGameID: game.id)
                }
            case "tdbField": typing = true
            case "fetch": fetch()
            case "use": useFetched()
            default: focus = "photos"
            }
        case .back:
            if typing { typing = false } else { close() }
        case .menu:
            close()
        default:
            break
        }
    }
}

private struct CoverPhotosPicker: ViewModifier {
    @Binding var isPresented: Bool
    let onPicked: (Data) -> Void
    let onError: (String) -> Void

    func body(content: Content) -> some View {
        if #available(iOS 16.0, *) {
            content.modifier(ModernCoverPhotosPicker(isPresented: $isPresented, onPicked: onPicked, onError: onError))
        } else {
            content.sheet(isPresented: $isPresented) {
                LegacyPhotoPicker { picked in
                    isPresented = false
                    guard let picked, let data = picked.jpegData(compressionQuality: 0.95) else { return }
                    onPicked(data)
                }
            }
        }
    }
}

@available(iOS 16.0, *)
private struct ModernCoverPhotosPicker: ViewModifier {
    @Binding var isPresented: Bool
    let onPicked: (Data) -> Void
    let onError: (String) -> Void
    @State private var item: PhotosPickerItem?

    func body(content: Content) -> some View {
        content
            .photosPicker(isPresented: $isPresented, selection: $item, matching: .images)
            .onValueChange(of: item) { newItem in
                guard let newItem else { return }
                item = nil
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
