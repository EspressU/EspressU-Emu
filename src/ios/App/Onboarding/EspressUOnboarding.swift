// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import GameController
import UIKit

/// First-launch guide: a short welcome and the one setup step that changes how games run (JIT).
/// Adding games and keys happens in the Library and in Settings, not here.
struct EspressUOnboardingView: View {
    var onFinished: () -> Void

    @StateObject private var input = ControllerMenuInput()
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject private var pairing = DevicePairing.shared

    @State private var page = 0
    @State private var focus: String?
    @State private var importingPairing = false
    @State private var pairingNote: String?
    @State private var appeared: Set<Int> = []

    private let pageCount = 3
    private var isLastPage: Bool { page == pageCount - 1 }

    var body: some View {
        ZStack {
            EspressUBackground()
            TabView(selection: $page) {
                welcomePage.tag(0)
                controllerPage.tag(1)
                speedPage.tag(2)
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
        }
        .safeAreaInset(edge: .bottom, spacing: 0) { bottomBar }
        .onAppear {
            input.onCommand = handle
            input.isExclusive = true
            input.start()
            focus = defaultFocus(for: page)
            reveal(page)
        }
        .onDisappear { input.stop() }
        .fileImporter(isPresented: $importingPairing, allowedContentTypes: [.propertyList, .xml, .data, .item]) { result in
            input.resume()
            importPairingFile(result)
        }
        .onValueChange(of: page) {
            focus = defaultFocus(for: $0)
            reveal($0)
        }
        .onValueChange(of: scenePhase) { phase in
            guard phase == .active else { return }
            input.resume()
        }
    }

    // MARK: Pages

    private var welcomePage: some View {
        OnboardingPage(alignment: .center) {
            EspressULogo(side: 116)
                .shadow(color: .black.opacity(0.18), radius: 22, y: 12)
                .scaleEffect(shown(0) ? 1 : 0.8)
                .opacity(shown(0) ? 1 : 0)
                .accessibilityHidden(true)
            VStack(alignment: .center, spacing: 12) {
                Text("Welcome to EspressU")
                    .font(.system(.largeTitle, design: .rounded).weight(.bold))
                    .multilineTextAlignment(.center)
                    .accessibilityAddTraits(.isHeader)
                Text(locf("Play the Wii U games you own on your %@.", UIDevice.current.userInterfaceIdiom == .pad ? "iPad" : "iPhone"))
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
            .offset(y: shown(0) ? 0 : 14)
            .opacity(shown(0) ? 1 : 0)
            .animation(.spring(response: 0.7, dampingFraction: 0.8).delay(0.12), value: appeared)
            Text("EspressU doesn't include any games. Add dumps of your own Wii U games from the Library afterwards.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 8)
                .opacity(shown(0) ? 1 : 0)
                .animation(.easeOut(duration: 0.5).delay(0.25), value: appeared)
        }
    }

    private var controllerPage: some View {
        OnboardingPage {
            heroTile("gamecontroller.fill", page: 1)
            VStack(alignment: .leading, spacing: 12) {
                Text(loc("Play with a controller"))
                    .font(.system(.largeTitle, design: .rounded).weight(.bold))
                    .accessibilityAddTraits(.isHeader)
                Text(loc("Pair it in Settings › Bluetooth. Xbox, PlayStation, Switch Pro and MFi controllers work, in games and in every menu of EspressU."))
                    .font(.body)
                    .foregroundStyle(.secondary)
            }
            .slideIn(shown(1))

            controllerCard
                .slideIn(shown(1), delay: 0.2)

            Text("No controller? The touch controls show up on screen and hide as soon as a controller connects.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .slideIn(shown(1), delay: 0.28)
        }
    }

    private var controllerCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            if input.controllers.isEmpty {
                HStack(spacing: 14) {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 28))
                        .foregroundStyle(.secondary)
                        .accessibilityHidden(true)
                    Text("No controller yet. It shows up here as soon as it's paired.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 0)
                }
            } else {
                ForEach(Array(input.controllers.enumerated()), id: \.offset) { _, controller in
                    HStack(spacing: 14) {
                        Image(systemName: "gamecontroller.fill")
                            .font(.system(size: 26))
                            .foregroundColor(EspressUTheme.accentText)
                            .symbolEffect(.bounce, value: input.controllers.count)
                            .accessibilityHidden(true)
                        Text(loc(controller.vendorName ?? "Controller"))
                            .font(.headline)
                        Spacer(minLength: 0)
                        Text("Ready")
                            .font(.footnote.weight(.semibold))
                            .foregroundColor(Color(light: "#17663F", dark: "#7FE0AE"))
                            .padding(.horizontal, 12)
                            .padding(.vertical, 6)
                            .background(Color(light: "#DDF3E7", dark: "#173A2A"), in: Capsule())
                    }
                }
            }
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .espressUSurface(RoundedRectangle(cornerRadius: 24, style: .continuous))
        .animation(.default, value: input.controllers.count)
    }

    /// Hero tile shared by the pages without the app logo.
    private func heroTile(_ symbol: String, page index: Int) -> some View {
        RoundedRectangle(cornerRadius: 22, style: .continuous)
            .fill(EspressUTheme.pixelBlue.opacity(0.14))
            .frame(width: 84, height: 84)
            .overlay(
                Image(systemName: symbol)
                    .font(.system(size: 40, weight: .semibold))
                    .foregroundColor(EspressUTheme.accentText)
            )
            .scaleEffect(shown(index) ? 1 : 0.8)
            .opacity(shown(index) ? 1 : 0)
            .animation(.spring(response: 0.7, dampingFraction: 0.8).delay(0.05), value: appeared)
            .accessibilityHidden(true)
    }

    private func shown(_ index: Int) -> Bool { appeared.contains(index) }

    private func reveal(_ index: Int) {
        withAnimation(.spring(response: 0.7, dampingFraction: 0.8).delay(0.1)) { _ = appeared.insert(index) }
    }

    private var speedPage: some View {
        OnboardingPage {
            heroTile("bolt.fill", page: 2)
            VStack(alignment: .leading, spacing: 12) {
                Text("Turn on full speed")
                    .font(.system(.largeTitle, design: .rounded).weight(.bold))
                    .accessibilityAddTraits(.isHeader)
                Text(loc(DevicePairing.isSupported
                         ? "Built-in JIT switches on by itself when a game starts. For that, EspressU needs a pairing file for this device once."
                         : "Built-in JIT switches on by itself when a game starts. For that, EspressU needs a pairing file for this device once. This iOS version can't make one itself, so you import one."))
                    .font(.body)
                    .foregroundStyle(.secondary)
            }
            .slideIn(shown(2))

            jitStatusCard
                .slideIn(shown(2), delay: 0.18)

            VStack(alignment: .leading, spacing: 14) {
                if DevicePairing.isSupported {
                    step(1, loc("Pair this device"), loc("Tap Pair. A pairing file from iLoader works too: Settings › JIT."))
                } else {
                    step(1, loc("Import a pairing file"), locf("Built-in pairing needs iOS %@ or newer. Make a pairing file with iLoader or idevice_pair, then tap Import.", DevicePairing.minimumIOSVersion))
                }
                step(2, loc("Connect LocalDevVPN"), loc("EspressU reaches the device through it. Wi-Fi works best."))
                step(3, loc("That's it"), loc("JIT switches on by itself whenever LocalDevVPN is connected."))
            }
            .slideIn(shown(2), delay: 0.26)

            Text("No JIT? Games still start, just much slower.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .slideIn(shown(2), delay: 0.32)
        }
    }

    private var jitStatusCard: some View {
        VStack(alignment: .leading, spacing: 16) {
            JITStatusCard(compact: true)
            if pairing.isActive {
                JITPairingCard()
            }
            Button(action: pairOrStop) {
                Text(loc(pairing.isActive ? "Stop pairing" : pairButtonTitle))
                    .font(.headline)
                    .foregroundColor(pairing.isActive ? .primary : EspressUTheme.onAccent)
                    .frame(maxWidth: .infinity, minHeight: 50)
                    .background(pairing.isActive ? AnyShapeStyle(Color(UIColor.tertiarySystemFill)) : AnyShapeStyle(EspressUTheme.pixelBlue), in: Capsule())
            }
            .buttonStyle(.plain)
            .controllerFocusRing(isFocused("pairJit"), shape: Capsule())
            if case .failed(let reason) = pairing.state { noteText(reason) }
            if let pairingNote { noteText(pairingNote) }
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .espressUSurface(RoundedRectangle(cornerRadius: 24, style: .continuous))
    }

    private func step(_ number: Int, _ title: String, _ detail: String) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Text("\(number)")
                .font(.system(.subheadline, design: .rounded).weight(.bold))
                .monospacedDigit()
                .foregroundColor(EspressUTheme.accentText)
                .frame(width: 28, height: 28)
                .background(Circle().fill(EspressUTheme.pixelBlue.opacity(0.16)))
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.headline).padding(.top, 3)
                Text(detail)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(locf("Step %@: %@. %@", number, title, detail))
    }

    private func noteText(_ text: String) -> some View {
        Text(loc(text))
            .font(.subheadline)
            .foregroundColor(EspressUTheme.alertText)
            .fixedSize(horizontal: false, vertical: true)
    }

    // MARK: Bottom bar

    private var primaryTitle: String {
        switch page {
        case 0: return loc("Get started")
        case pageCount - 1: return loc("Start playing")
        default: return loc("Next")
        }
    }

    private var bottomBar: some View {
        VStack(spacing: 8) {
            HStack(spacing: 7) {
                ForEach(0..<pageCount, id: \.self) { index in
                    Capsule()
                        .fill(index == page ? EspressUTheme.pixelBlue : Color(UIColor.tertiaryLabel))
                        .frame(width: index == page ? 20 : 7, height: 7)
                }
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.8), value: page)
            .padding(.bottom, 4)
            .accessibilityHidden(true)

            Button(action: advance) {
                Text(primaryTitle)
                    .frame(maxWidth: .infinity, minHeight: 28)
            }
            .buttonStyle(EspressUPrimaryButtonStyle())
            .controllerFocusRing(isFocused("next"), shape: Capsule())
            .accessibilityHint(isLastPage ? loc("Finishes the guide and opens your library.") : loc("Goes to the next page."))

            Button(action: goBack) {
                Text("Back")
                    .font(.body)
                    .foregroundColor(EspressUTheme.accentText)
                    .frame(maxWidth: .infinity, minHeight: 44)
            }
            .buttonStyle(.plain)
            .controllerFocusRing(isFocused("back"), shape: Capsule())
            .opacity(page == 0 ? 0 : 1)
            .disabled(page == 0)
            .accessibilityHidden(page == 0)

            if input.showsFocus {
                HStack(spacing: 16) {
                    ControllerButtonHint(symbol: input.confirmSymbol, text: loc("Select"))
                    if page > 0 { ControllerButtonHint(symbol: input.backSymbol, text: loc("Back")) }
                }
            }
        }
        .frame(maxWidth: 520)
        .padding(.horizontal, 24)
        .padding(.top, 12)
        .padding(.bottom, 8)
        .frame(maxWidth: .infinity)
    }

    // MARK: JIT pairing

    private var pairButtonTitle: String {
        if DevicePairing.isSupported { return StikJITSetup.hasPairingFile ? loc("Pair again") : loc("Pair") }
        return StikJITSetup.hasPairingFile ? loc("Import another pairing file") : loc("Import pairing file")
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
                pairingNote = locf("Couldn't import it: %@", error.localizedDescription)
            }
        case .failure(let error):
            pairingNote = locf("Couldn't import it: %@", error.localizedDescription)
        }
    }

    // MARK: Navigation and controller

    private func isFocused(_ id: String) -> Bool {
        input.showsFocus && focus == id
    }

    private var focusGrid: ControllerFocusGrid {
        switch page {
        case pageCount - 1: return ControllerFocusGrid(rows: [["pairJit"], ["next"], ["back"]])
        case 0: return ControllerFocusGrid(rows: [["next"]])
        default: return ControllerFocusGrid(rows: [["next"], ["back"]])
        }
    }

    private func defaultFocus(for page: Int) -> String {
        if page == pageCount - 1 && !StikJITSetup.hasPairingFile { return "pairJit" }
        return "next"
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
            if !isLastPage { advance() }
        case .menu:
            advance()
        case .alternate, .select:
            break
        }
    }

    private func activate(_ id: String) {
        switch id {
        case "next": advance()
        case "back": goBack()
        case "pairJit": pairOrStop()
        default: break
        }
    }

    private func advance() {
        if isLastPage { finish() } else { withAnimation { page += 1 } }
    }

    private func goBack() {
        withAnimation { page = max(0, page - 1) }
    }

    private func finish() {
        input.stop()
        OnboardingState.markCompleted()
        onFinished()
    }
}

/// One page of the guide: left-aligned, readable width, centred vertically when it fits and scrolling when it doesn't.
private struct OnboardingPage<Content: View>: View {
    var alignment: HorizontalAlignment = .leading
    @ViewBuilder var content: Content

    var body: some View {
        GeometryReader { geo in
            ScrollView {
                VStack(alignment: alignment, spacing: 28) {
                    content
                }
                .frame(maxWidth: 520, alignment: alignment == .center ? .center : .leading)
                .padding(.horizontal, 24)
                .padding(.vertical, 32)
                .frame(maxWidth: .infinity, minHeight: geo.size.height, alignment: .center)
            }
            .scrollBounceBehavior(.basedOnSize)
        }
    }
}

private extension View {
    /// Slides up 14pt and fades in once its page is current.
    func slideIn(_ shown: Bool, delay: Double = 0.12) -> some View {
        self
            .offset(y: shown ? 0 : 14)
            .opacity(shown ? 1 : 0)
            .animation(.spring(response: 0.7, dampingFraction: 0.8).delay(delay), value: shown)
    }
}
