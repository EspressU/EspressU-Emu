// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import GameController
import UIKit

struct EspressUOnboardingView: View {
    @ObservedObject var gameManager: GameManager
    var onFinished: () -> Void

    @StateObject private var input = ControllerMenuInput()
    @Environment(\.scenePhase) private var scenePhase

    @State private var page = 0
    @State private var focus: String?
    @ObservedObject private var jitCoordinator = StikJITCoordinator.shared
    @ObservedObject private var pairing = DevicePairing.shared
    @State private var gamesNote: String?
    @State private var choosingFolder = false
    @State private var importingPairing = false
    @State private var pairingNote: String?
    @State private var keyCount = WiiUKeys.keysFileExists() ? WiiUKeys.installedKeyCount() : 0

    private let pageCount = 4

    var body: some View {
        GeometryReader { geo in
            let wide = geo.size.width >= 760 && geo.size.width > geo.size.height * 1.05
            ZStack {
                EspressUBackground()
                VStack(spacing: 0) {
                    topBar
                    TabView(selection: $page) {
                        welcomePage.tag(0)
                        gamesPage(wide: wide).tag(1)
                        jitPage(wide: wide).tag(2)
                        controllerPage(wide: wide).tag(3)
                    }
                    .tabViewStyle(.page(indexDisplayMode: .never))
                    bottomBar
                }
            }
        }
        .onAppear {
            input.onCommand = handle
            input.isExclusive = true
            input.start()
            focus = defaultFocus(for: page)
        }
        .onDisappear { input.stop() }
        .fileImporter(isPresented: $choosingFolder, allowedContentTypes: [.folder]) { result in
            input.resume()
            switch result {
            case .success(let url): importGames([url])
            case .failure(let error): gamesNote = error.localizedDescription
            }
        }
        .fileImporter(isPresented: $importingPairing, allowedContentTypes: [.propertyList, .xml, .data, .item]) { result in
            input.resume()
            importPairingFile(result)
        }
        .onChange(of: page) { focus = defaultFocus(for: $0) }
        .onChange(of: scenePhase) { phase in
            guard phase == .active else { return }
            input.resume()
        }
    }

    private var topBar: some View {
        HStack {
            HStack(spacing: 8) {
                ForEach(0..<pageCount, id: \.self) { index in
                    Capsule()
                        .fill(index <= page ? EspressUTheme.pixelBlue : EspressUTheme.wrapper)
                        .frame(width: index == page ? 26 : 8, height: 8)
                }
            }
            .animation(.easeOut(duration: 0.2), value: page)
            .accessibilityHidden(true)
            Spacer()
            if page < pageCount - 1 {
                Button("Skip setup", action: finish)
                    .font(.system(.body, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .frame(minHeight: 44)
                    .padding(.horizontal, 10)
                    .controllerFocusRing(isFocused("skip"), shape: Capsule())
                    .accessibilityHint("Closes the guide and opens your library.")
            }
        }
        .padding(.horizontal, 28)
        .padding(.top, 12)
    }

    private var bottomBar: some View {
        HStack(spacing: 12) {
            Button(action: goBack) {
                Text("Back")
                    .font(.system(.headline, design: .rounded).weight(.regular))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .frame(minWidth: 96, minHeight: 50)
            }
            .buttonStyle(.plain)
            .controllerFocusRing(isFocused("back"), shape: Capsule())
            .opacity(page == 0 ? 0 : 1)
            .disabled(page == 0)
            .accessibilityHidden(page == 0)

            Spacer(minLength: 0)
            if input.showsFocus {
                HStack(spacing: 16) {
                    ControllerButtonHint(symbol: input.confirmSymbol, text: "Select")
                    if page > 0 { ControllerButtonHint(symbol: input.backSymbol, text: "Back") }
                }
            } else {
                Text("Step \(page + 1) of \(pageCount)")
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
            }
            Spacer(minLength: 0)

            Button(action: advance) {
                Text(primaryTitle)
                    .font(.system(.headline, design: .rounded))
                    .foregroundColor(EspressUTheme.onAccent)
                    .padding(.horizontal, 22)
                    .frame(minWidth: 112, minHeight: 50)
                    .background(EspressUTheme.pixelBlue, in: Capsule())
            }
            .buttonStyle(.plain)
            .controllerFocusRing(isFocused(page == pageCount - 1 ? "finish" : "next"), shape: Capsule())
            .accessibilityHint(page == pageCount - 1 ? "Finishes the guide and opens your library." : "Goes to the next page.")
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .espressUSurface(Capsule())
        .frame(maxWidth: 1100)
        .padding(.horizontal, 24)
        .padding(.bottom, 12)
    }

    private var primaryTitle: String {
        switch page {
        case 0: return "Get started"
        case pageCount - 1: return "Start playing"
        default: return "Next"
        }
    }

    private var welcomePage: some View {
        ScrollView {
            VStack(spacing: 20) {
                EspressULogo(side: 140)
                    .shadow(color: EspressUTheme.shadow.opacity(0.22), radius: 18, y: 10)
                    .accessibilityHidden(true)
                Text("Welcome to EspressU")
                    .font(.system(.largeTitle, design: .rounded).weight(.bold))
                    .foregroundColor(EspressUTheme.onBackground)
                    .multilineTextAlignment(.center)
                    .accessibilityAddTraits(.isHeader)
                Text("Play the Wii U games you own on your \(UIDevice.current.userInterfaceIdiom == .pad ? "iPad" : "iPhone"). Setup takes about two minutes, and every step can be skipped.")
                    .font(.system(.title3, design: .rounded))
                    .foregroundColor(EspressUTheme.onBackgroundMuted)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 560)
                    .fixedSize(horizontal: false, vertical: true)
                flowChips
                    .padding(.top, 8)
            }
            .padding(.horizontal, 24)
            .padding(.vertical, 32)
            .frame(maxWidth: .infinity)
        }
    }

    private var flowChips: some View {
        let chips = [("arrow.down.to.line", "Your own games"),
                     ("bolt.fill", "Full speed with JIT"),
                     ("gamecontroller.fill", "Touch or controller")]
        return HStackOrVStack {
            ForEach(chips.indices, id: \.self) { index in
                Label(chips[index].1, systemImage: chips[index].0)
                    .font(.system(.callout, design: .rounded).weight(.medium))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 11)
                    .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
            }
        }
    }

    private func gamesPage(wide: Bool) -> some View {
        OnboardingColumns(wide: wide) {
            VStack(alignment: .leading, spacing: 16) {
                pageHeading("Add your games",
                            "Use games dumped from your own Wii U. A single .wua file is easiest: it holds the game, its update and DLC in one.")
                bigButton(id: "choose", title: "Game file", detail: ".wua, .wud, .wux or .rpx",
                          systemImage: "doc.fill", primary: true, action: chooseGames)
                bigButton(id: "folder", title: "Game folder", detail: "A dumped game with code, content and meta",
                          systemImage: "folder.fill", primary: false, action: chooseGameFolder)
                bigButton(id: "files", title: "Put games in Files",
                          detail: "Opens Files at EspressU › Roms. New games show up on their own.",
                          systemImage: "folder.badge.plus", primary: false, action: openRomsInFiles)
                keysRow
                if case .copying(let name) = gameManager.importState {
                    HStack(spacing: 10) {
                        ProgressView()
                        Text("Copying \(name)\u{2026}")
                    }
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                }
                if let gamesNote { noteText(gamesNote) }
            }
        } trailing: {
            libraryPreview
        }
    }

    private var keysRow: some View {
        HStack(spacing: 12) {
            Image(systemName: "key.fill")
                .foregroundColor(EspressUTheme.brownMid)
                .accessibilityHidden(true)
            Text(keyCount > 0
                 ? "\(keyCount) key\(keyCount == 1 ? "" : "s") loaded for encrypted dumps (.wud, .wux)."
                 : "Encrypted dumps (.wud, .wux) also need your keys.txt. Not needed for .wua.")
                .font(.system(.subheadline, design: .rounded))
                .foregroundColor(EspressUTheme.brownMid)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
            Button(keyCount > 0 ? "Replace" : "Add keys", action: importKeys)
                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                .foregroundColor(EspressUTheme.accentText)
                .frame(minHeight: 44)
                .padding(.horizontal, 8)
                .controllerFocusRing(isFocused("keys"), shape: Capsule())
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 6)
        .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous)
            .strokeBorder(EspressUTheme.brownMid.opacity(0.45), style: StrokeStyle(lineWidth: 1, dash: [5, 4])))
    }

    private var libraryPreview: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .firstTextBaseline) {
                Text("Your library")
                    .font(.system(.title3, design: .rounded).weight(.semibold))
                    .foregroundColor(EspressUTheme.brownDarkest)
                Spacer()
                Text("\(gameManager.games.count) game\(gameManager.games.count == 1 ? "" : "s")")
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
            }
            if gameManager.games.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "square.grid.2x2")
                        .font(.system(size: 34))
                        .foregroundColor(EspressUTheme.brownMid.opacity(0.6))
                    Text("Games you add show up here, with their own icon and name.")
                        .font(.system(.subheadline, design: .rounded))
                        .foregroundColor(EspressUTheme.brownMid)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, minHeight: 180)
                .background(EspressUTheme.wrapper.opacity(0.5), in: RoundedRectangle(cornerRadius: 20, style: .continuous))
            } else {
                LazyVGrid(columns: [GridItem(.flexible(), spacing: 14), GridItem(.flexible(), spacing: 14)], spacing: 14) {
                    ForEach(Array(gameManager.games.prefix(4))) { game in
                        VStack(alignment: .leading, spacing: 6) {
                            ZStack {
                                EspressUTheme.cremaTopGradient
                                if let path = game.coverPath {
                                    CoverImage(path: path, fill: true)
                                } else {
                                    Image(systemName: "gamecontroller.fill")
                                        .font(.system(size: 26))
                                        .foregroundColor(EspressUTheme.onCremaTop)
                                }
                            }
                            .aspectRatio(1, contentMode: .fit)
                            .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
                            Text(game.cardName.name)
                                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                                .foregroundColor(EspressUTheme.brownDarkest)
                                .lineLimit(1)
                        }
                    }
                }
            }
        }
        .padding(22)
        .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 28, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 28, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
    }

    private func jitPage(wide: Bool) -> some View {
        OnboardingColumns(wide: wide) {
            VStack(alignment: .leading, spacing: 16) {
                pageHeading("Turn on full speed",
                            DevicePairing.isSupported
                                ? "Built-in JIT switches on by itself when a game starts. For that, EspressU needs a pairing file for this device once."
                                : "Built-in JIT switches on by itself when a game starts. For that, EspressU needs a pairing file for this device once. This iOS version can't make one itself, so you import one.")
                jitStatusCard
                Text("No JIT? Games still start, just much slower.")
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.onBackgroundMuted)
            }
        } trailing: {
            VStack(alignment: .leading, spacing: 10) {
                Text("How to set it up")
                    .font(.system(.title3, design: .rounded).weight(.semibold))
                    .foregroundColor(EspressUTheme.onBackground)
                    .accessibilityAddTraits(.isHeader)
                if DevicePairing.isSupported {
                    stepCard(1, "Pair this device", "Tap Pair. A pairing file from iLoader works too: Settings › JIT.")
                } else {
                    stepCard(1, "Import a pairing file", "Built-in pairing needs iOS \(DevicePairing.minimumIOSVersion) or newer. Make a pairing file with iLoader or idevice_pair, then tap Import.")
                }
                stepCard(2, "Connect LocalDevVPN", "EspressU reaches the device through it. Wi-Fi works best.")
                stepCard(3, "That's it", "JIT switches on by itself whenever LocalDevVPN is connected.")
            }
        }
    }

    private var jitStatusCard: some View {
        return VStack(alignment: .leading, spacing: 16) {
            JITStatusCard(compact: true)
            if pairing.isActive {
                JITPairingCard()
            }
            Button(action: pairOrStop) {
                Text(pairing.isActive ? "Stop pairing" : pairButtonTitle)
                    .font(.system(.headline, design: .rounded))
                    .foregroundColor(pairing.isActive ? EspressUTheme.brownDarkest : EspressUTheme.onAccent)
                    .frame(maxWidth: .infinity, minHeight: 52)
                    .background(pairing.isActive ? AnyShapeStyle(EspressUTheme.wrapper) : AnyShapeStyle(EspressUTheme.pixelBlue), in: Capsule())
            }
            .buttonStyle(.plain)
            .controllerFocusRing(isFocused("pairJit"), shape: Capsule())
            if case .failed(let reason) = pairing.state { noteText(reason) }
            if let pairingNote { noteText(pairingNote) }
        }
        .padding(22)
        .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 28, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 28, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
    }

    private var pairButtonTitle: String {
        if DevicePairing.isSupported { return StikJITSetup.hasPairingFile ? "Pair again" : "Pair" }
        return StikJITSetup.hasPairingFile ? "Import another pairing file" : "Import pairing file"
    }

    private func pairOrStop() {
        if pairing.isActive {
            pairing.cancel()
        } else if DevicePairing.isSupported {
            pairing.start()
        } else {
            input.pause()
            importingPairing = true
        }
    }

    private func importPairingFile(_ result: Result<URL, Error>) {
        switch result {
        case .success(let url):
            do {
                try StikJITSetup.importPairingFile(from: url)
                StikJITCoordinator.shared.pairingFileChanged()
                StikJITCoordinator.shared.autoEnableIfPossible()
                pairingNote = nil
            } catch {
                pairingNote = "Couldn't import it: \(error.localizedDescription)"
            }
        case .failure(let error):
            pairingNote = "Couldn't import it: \(error.localizedDescription)"
        }
    }

    private func controllerPage(wide: Bool) -> some View {
        OnboardingColumns(wide: wide) {
            VStack(alignment: .leading, spacing: 16) {
                Text("Optional")
                    .font(.system(.footnote, design: .rounded).weight(.semibold))
                    .foregroundColor(EspressUTheme.brownMid)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(EspressUTheme.wrapper, in: Capsule())
                pageHeading("Play with a controller",
                            "Pair it in Settings › Bluetooth. Xbox, PlayStation, Switch Pro and MFi controllers work, in games and in every menu of EspressU.")
                Text("No controller? The touch controls show up on screen and hide as soon as a controller connects.")
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.onBackgroundMuted)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } trailing: {
            VStack(alignment: .leading, spacing: 12) {
                Text("Connected")
                    .font(.system(.title3, design: .rounded).weight(.semibold))
                    .foregroundColor(EspressUTheme.brownDarkest)
                if input.controllers.isEmpty {
                    HStack(spacing: 14) {
                        Image(systemName: "gamecontroller")
                            .font(.system(size: 28))
                            .accessibilityHidden(true)
                        Text("No controller yet. It shows up here as soon as it's paired.")
                            .font(.system(.callout, design: .rounded))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .foregroundColor(EspressUTheme.brownMid)
                    .padding(16)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous)
                        .strokeBorder(EspressUTheme.brownMid.opacity(0.45), style: StrokeStyle(lineWidth: 1, dash: [5, 4])))
                } else {
                    ForEach(Array(input.controllers.enumerated()), id: \.offset) { _, controller in
                        HStack(spacing: 14) {
                            Image(systemName: "gamecontroller.fill")
                                .font(.system(size: 26))
                                .foregroundColor(EspressUTheme.accentText)
                                .accessibilityHidden(true)
                            Text(controller.vendorName ?? "Controller")
                                .font(.system(.headline, design: .rounded))
                                .foregroundColor(EspressUTheme.brownDarkest)
                            Spacer(minLength: 0)
                            Text("Ready")
                                .font(.system(.footnote, design: .rounded).weight(.semibold))
                                .foregroundColor(Color(light: "#17663F", dark: "#7FE0AE"))
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(Color(light: "#DDF3E7", dark: "#173A2A"), in: Capsule())
                        }
                        .padding(16)
                        .background(EspressUTheme.wrapper.opacity(0.7), in: RoundedRectangle(cornerRadius: 20, style: .continuous))
                    }
                    HStack(spacing: 12) {
                        Text("Press any button to test it:")
                            .font(.system(.callout, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                        Image(systemName: input.lastPressedSymbol ?? "circle.dashed")
                            .font(.system(size: 30, weight: .semibold))
                            .foregroundColor(input.lastPressedSymbol == nil ? EspressUTheme.brownMid.opacity(0.5) : EspressUTheme.accentText)
                            .id(input.lastPressedSymbol)
                            .transition(.scale.combined(with: .opacity))
                    }
                    .animation(.spring(response: 0.25, dampingFraction: 0.6), value: input.lastPressedSymbol)
                    .accessibilityHidden(true)
                }
            }
            .padding(22)
            .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 28, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 28, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
        }
    }

    private func pageHeading(_ title: String, _ subtitle: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.system(.largeTitle, design: .rounded).weight(.bold))
                .foregroundColor(EspressUTheme.onBackground)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityAddTraits(.isHeader)
            Text(subtitle)
                .font(.system(.body, design: .rounded))
                .foregroundColor(EspressUTheme.onBackgroundMuted)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func bigButton(id: String, title: String, detail: String, systemImage: String,
                           primary: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 16) {
                Image(systemName: systemImage)
                    .font(.system(size: 26, weight: .semibold))
                    .frame(width: 34)
                    .foregroundColor(primary ? EspressUTheme.onAccent : EspressUTheme.accentText)
                VStack(alignment: .leading, spacing: 3) {
                    Text(title)
                        .font(.system(.headline, design: .rounded))
                    Text(detail)
                        .font(.system(.subheadline, design: .rounded))
                        .opacity(primary ? 0.88 : 1)
                        .foregroundColor(primary ? EspressUTheme.onAccent : EspressUTheme.brownMid)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .foregroundColor(primary ? EspressUTheme.onAccent : EspressUTheme.brownDarkest)
                Spacer(minLength: 0)
            }
            .multilineTextAlignment(.leading)
            .padding(18)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(primary ? EspressUTheme.pixelBlue : EspressUTheme.cream,
                        in: RoundedRectangle(cornerRadius: 22, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous)
                .stroke(primary ? Color.clear : EspressUTheme.wrapper, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .controllerFocusRing(isFocused(id), shape: RoundedRectangle(cornerRadius: 26, style: .continuous))
    }

    private func stepCard(_ number: Int, _ title: String, _ detail: String) -> some View {
        HStack(alignment: .top, spacing: 14) {
            Text("\(number)")
                .font(.system(.callout, design: .rounded).weight(.bold))
                .foregroundColor(EspressUTheme.accentText)
                .frame(width: 32, height: 32)
                .background(EspressUTheme.wrapper, in: Circle())
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .font(.system(.headline, design: .rounded))
                    .foregroundColor(EspressUTheme.brownDarkest)
                Text(detail)
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(16)
        .background(EspressUTheme.cream, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous).stroke(EspressUTheme.wrapper, lineWidth: 1))
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Step \(number): \(title). \(detail)")
    }

    private func noteText(_ text: String) -> some View {
        Text(text)
            .font(.system(.subheadline, design: .rounded))
            .foregroundColor(EspressUTheme.alertText)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func isFocused(_ id: String) -> Bool {
        input.showsFocus && focus == id
    }

    private var focusGrid: ControllerFocusGrid {
        switch page {
        case 0: return ControllerFocusGrid(rows: [["skip"], ["next"]])
        case 1: return ControllerFocusGrid(rows: [["skip"], ["choose"], ["folder"], ["files"], ["keys"], ["back", "next"]])
        case 2: return ControllerFocusGrid(rows: [["skip"], ["pairJit"], ["back", "next"]])
        default: return ControllerFocusGrid(rows: [["back", "finish"]])
        }
    }

    private func defaultFocus(for page: Int) -> String {
        switch page {
        case 1: return gameManager.games.isEmpty ? "choose" : "next"
        case 2: return StikJITSetup.hasPairingFile ? "next" : "pairJit"
        case pageCount - 1: return "finish"
        default: return "next"
        }
    }

    private func handle(_ command: ControllerMenuCommand) {
        switch command {
        case .up, .down, .left, .right:
            focus = focusGrid.move(focus, command)
        case .confirm:
            activate(focus ?? defaultFocus(for: page))
        case .back, .previousPage:
            if page > 0 { goBack() }
        case .nextPage:
            if page < pageCount - 1 { advance() }
        case .menu:
            advance()
        case .alternate, .select:
            break
        }
    }

    private func activate(_ id: String) {
        switch id {
        case "skip", "finish": finish()
        case "next": advance()
        case "back": goBack()
        case "choose": chooseGames()
        case "folder": chooseGameFolder()
        case "files": openRomsInFiles()
        case "keys": importKeys()
        case "pairJit": pairOrStop()
        default: break
        }
    }

    private func advance() {
        if page == pageCount - 1 { finish() } else { withAnimation { page += 1 } }
    }

    private func goBack() {
        withAnimation { page = max(0, page - 1) }
    }

    private func finish() {
        input.stop()
        OnboardingState.markCompleted()
        onFinished()
    }

    private func chooseGames() {
        input.pause()
        DocumentImport.present(contentTypes: [.item]) { result in
            input.resume()
            Task { @MainActor in
                switch result {
                case .success(let urls): importGames(urls)
                case .failure(let error): gamesNote = error.localizedDescription
                }
            }
        }
    }

    private func chooseGameFolder() {
        input.pause()
        choosingFolder = true
    }

    private func importGames(_ urls: [URL]) {
        gamesNote = nil
        Task { @MainActor in
            for url in urls {
                do { try await gameManager.importROM(from: url) } catch { gamesNote = error.localizedDescription }
            }
        }
    }

    private func openRomsInFiles() {
        let folder = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Roms", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let path = folder.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? folder.path
        guard let url = URL(string: "shareddocuments://" + path) else { return }
        input.pause()
        UIApplication.shared.open(url) { opened in
            if !opened { input.resume() }
        }
    }

    private func importKeys() {
        input.pause()
        DocumentImport.present(contentTypes: [.item]) { result in
            input.resume()
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    keyCount = try WiiUKeys.importKeys(from: url)
                    gamesNote = nil
                } catch {
                    gamesNote = error.localizedDescription
                }
            case .failure(let error):
                gamesNote = error.localizedDescription
            }
        }
    }
}

private struct OnboardingColumns<Leading: View, Trailing: View>: View {
    let wide: Bool
    @ViewBuilder var leading: Leading
    @ViewBuilder var trailing: Trailing

    var body: some View {
        ScrollView {
            Group {
                if wide {
                    HStack(alignment: .center, spacing: 40) {
                        leading.frame(maxWidth: .infinity, alignment: .leading)
                        trailing.frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(.horizontal, 48)
                } else {
                    VStack(alignment: .leading, spacing: 24) {
                        leading
                        trailing
                    }
                    .padding(.horizontal, 20)
                    .frame(maxWidth: 640)
                }
            }
            .padding(.vertical, 24)
            .frame(maxWidth: .infinity)
        }
    }
}

private struct HStackOrVStack<Content: View>: View {
    @ViewBuilder var content: Content
    @Environment(\.horizontalSizeClass) private var sizeClass

    var body: some View {
        if sizeClass == .regular {
            HStack(spacing: 12) { content }
        } else {
            VStack(spacing: 10) { content }
        }
    }
}
