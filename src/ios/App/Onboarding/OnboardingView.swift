// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

struct OnboardingView: View {
    @ObservedObject var gameManager: GameManager
    var onFinished: () -> Void

    @State private var page = 0
    private let totalPages = 4

    private var isLastPage: Bool { page == totalPages - 1 }

    var body: some View {
        ZStack {
            EspressUTheme.backgroundGradient.ignoresSafeArea()

            VStack(spacing: 0) {
                TabView(selection: $page) {
                    OnboardingWelcomePage()
                        .tag(0)
                    OnboardingKeysPage(onSkip: advance)
                        .tag(1)
                    OnboardingGamesPage(gameManager: gameManager)
                        .tag(2)
                    OnboardingSpeedControlsPage()
                        .tag(3)
                }
                .tabViewStyle(.page(indexDisplayMode: .never))

                footer
            }
        }
    }

    private var footer: some View {
        VStack(spacing: 14) {
            pageDots

            HStack {
                Button("Back", action: goBack)
                    .buttonStyle(EspressUSecondaryButtonStyle())
                    .opacity(page == 0 ? 0 : 1)
                    .disabled(page == 0)
                    .accessibilityHidden(page == 0)

                Spacer()

                Button(action: advance) {
                    Text(isLastPage ? "Start playing" : "Next")
                }
                .buttonStyle(EspressUPrimaryButtonStyle())
                .accessibilityLabel(isLastPage ? "Start playing" : "Next")
                .accessibilityHint(isLastPage
                    ? "Finishes the guide and opens your library."
                    : "Goes to the next page of the guide.")
            }
        }
        .padding(.horizontal, 24)
        .padding(.top, 8)
        .padding(.bottom, 16)
    }

    private var pageDots: some View {
        HStack(spacing: 8) {
            ForEach(0..<totalPages, id: \.self) { index in
                Circle()
                    .fill(index == page ? EspressUTheme.pixelBlue : EspressUTheme.wrapper)
                    .frame(width: index == page ? 9 : 7, height: index == page ? 9 : 7)
            }
        }
        .animation(.easeOut(duration: 0.18), value: page)
        .accessibilityHidden(true)
    }

    private func advance() {
        if isLastPage {
            finish()
        } else {
            withAnimation { page += 1 }
        }
    }

    private func goBack() {
        withAnimation { page = max(0, page - 1) }
    }

    private func finish() {
        OnboardingState.markCompleted()
        onFinished()
    }
}

private enum OnboardingHero {
    case none
    case mark
    case symbol(String)

    @ViewBuilder var view: some View {
        switch self {
        case .none:
            EmptyView()
        case .mark:
            EspressULogo(side: 132)
                .shadow(color: EspressUTheme.shadow.opacity(0.12), radius: 16, x: 0, y: 8)
                .padding(.bottom, 4)
        case .symbol(let name):
            ZStack {
                Circle().fill(EspressUTheme.cream)
                Image(systemName: name)
                    .font(.system(size: 30, weight: .semibold))
                    .foregroundColor(EspressUTheme.accentText)
            }
            .frame(width: 72, height: 72)
            .shadow(color: EspressUTheme.shadow.opacity(0.09), radius: 10, x: 0, y: 4)
            .accessibilityHidden(true)
        }
    }
}

private struct OnboardingPageScaffold<Content: View>: View {
    let title: String
    let subtitle: String
    var hero: OnboardingHero = .none
    @ViewBuilder var content: Content

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                hero.view
                    .frame(maxWidth: .infinity, alignment: .center)

                Text(title)
                    .font(.system(.title).weight(.bold))
                    .foregroundColor(EspressUTheme.onBackground)
                    .fixedSize(horizontal: false, vertical: true)
                    .accessibilityAddTraits(.isHeader)

                Text(subtitle)
                    .font(.system(.body))
                    .foregroundColor(EspressUTheme.onBackgroundMuted)
                    .fixedSize(horizontal: false, vertical: true)

                content

                Spacer(minLength: 24)
            }
            .padding(.horizontal, 24)
            .padding(.top, 40)
            .frame(maxWidth: 640, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }
}

private struct OnboardingFactRow: View {
    let systemImage: String
    let text: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: systemImage)
                .font(.system(size: 17, weight: .semibold))
                .foregroundColor(EspressUTheme.accentText)
                .frame(width: 22)
                .accessibilityHidden(true)
            Text(text)
                .font(.system(.body))
                .foregroundColor(EspressUTheme.brownDarkest)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

private struct OnboardingWelcomePage: View {
    var body: some View {
        OnboardingPageScaffold(
            title: "Welcome to EspressU",
            subtitle: "Play your own Wii U games. This takes a minute and you can skip any step.",
            hero: .mark
        ) {
            EmptyView()
        }
    }
}

private struct OnboardingKeysPage: View {
    var onSkip: () -> Void

    @State private var hasKeys = WiiUKeys.keysFileExists()
    @State private var keyCount = OnboardingKeysPage.currentKeyCount()
    @State private var errorMessage: String?

    var body: some View {
        OnboardingPageScaffold(
            title: "Your keys",
            subtitle: "Encrypted Wii U games need a keys.txt dumped from your own Wii U. EspressU doesn't include one. Homebrew doesn't need it.",
            hero: .symbol("key.fill")
        ) {
            EspressUCard {
                VStack(alignment: .leading, spacing: 16) {
                    Button(action: importKeys) {
                        Label(hasKeys ? "Replace keys.txt" : "Import keys.txt", systemImage: "key")
                    }
                    .buttonStyle(EspressUPrimaryButtonStyle())
                    .accessibilityHint("Opens the Files picker to choose your keys.txt.")

                    if hasKeys {
                        Text("\(keyCount) key\(keyCount == 1 ? "" : "s") loaded")
                            .font(.system(.subheadline).weight(.semibold))
                            .foregroundColor(EspressUTheme.brownMid)
                    }

                    if let errorMessage {
                        Text(errorMessage)
                            .font(.system(.footnote))
                            .foregroundColor(EspressUTheme.alertText)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Button(action: onSkip) {
                        Text("Skip - I only play homebrew")
                            .font(.system(.footnote))
                            .underline()
                    }
                    .buttonStyle(.plain)
                    .foregroundColor(EspressUTheme.brownMid)
                    .accessibilityHint("Continues without importing keys. Homebrew doesn't need them, and you can add keys later from Settings.")
                }
                .padding(16)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }

    private func importKeys() {
        DocumentImport.present(contentTypes: [.item]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    keyCount = try WiiUKeys.importKeys(from: url)
                    hasKeys = true
                    errorMessage = nil
                } catch {
                    errorMessage = error.localizedDescription
                }
            case .failure(let error):
                errorMessage = error.localizedDescription
            }
        }
    }

    private static func currentKeyCount() -> Int {
        let bridgeCount = cemu_bridge_reload_and_count_keys()
        if bridgeCount >= 0 {
            return Int(bridgeCount)
        }
        return WiiUKeys.installedKeyCount()
    }
}

private struct OnboardingGamesPage: View {
    @ObservedObject var gameManager: GameManager
    @State private var errorMessage: String?

    var body: some View {
        OnboardingPageScaffold(
            title: "Add your games",
            subtitle: "Import games from Files: .wud, .wux, .wua, .iso, a dumped game folder or a homebrew .rpx. You can also drop them into Documents/Roms in the Files app.",
            hero: .symbol("square.and.arrow.down.fill")
        ) {
            EspressUCard {
                VStack(alignment: .leading, spacing: 16) {
                    Button(action: importGame) {
                        Label("Import a game", systemImage: "square.and.arrow.down")
                    }
                    .buttonStyle(EspressUPrimaryButtonStyle())
                    .accessibilityHint("Opens the Files picker to choose a game to import.")

                    if case .copying(let name) = gameManager.importState {
                        HStack(spacing: 8) {
                            ProgressView()
                            Text("Copying \(name)\u{2026}")
                        }
                        .font(.system(.footnote))
                        .foregroundColor(EspressUTheme.brownMid)
                    }

                    if let errorMessage {
                        Text(errorMessage)
                            .font(.system(.footnote))
                            .foregroundColor(EspressUTheme.alertText)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .padding(16)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }

    private func importGame() {
        DocumentImport.present(contentTypes: [.item]) { result in
            Task { @MainActor in
                switch result {
                case .success(let urls):
                    guard let url = urls.first else { return }
                    do {
                        try await gameManager.importROM(from: url)
                        errorMessage = nil
                    } catch {
                        errorMessage = error.localizedDescription
                    }
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
        }
    }
}

private struct OnboardingSpeedControlsPage: View {
    var body: some View {
        OnboardingPageScaffold(
            title: "Speed and controls",
            subtitle: "Three things affect speed and control:",
            hero: .symbol("bolt.fill")
        ) {
            EspressUCard {
                VStack(alignment: .leading, spacing: 18) {
                    OnboardingFactRow(
                        systemImage: "bolt.fill",
                        text: "Full speed needs a JIT enabler (StikJIT, SideStore or LiveContainer) attached at launch. Without one, EspressU still runs but much slower. Settings shows which you have."
                    )
                    OnboardingFactRow(
                        systemImage: "checkmark.seal.fill",
                        text: "If a game glitches, desyncs or crashes, turn on Favour accuracy in Settings > CPU. It's slower but more accurate."
                    )
                    OnboardingFactRow(
                        systemImage: "gamecontroller.fill",
                        text: "The on-screen pad can add analog sticks and comfort controls, and any MFi, Xbox, or PlayStation controller works right alongside it."
                    )
                }
                .padding(16)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }
}
