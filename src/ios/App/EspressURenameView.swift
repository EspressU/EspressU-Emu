// SPDX-License-Identifier: MPL-2.0

import SwiftUI

struct EspressURenameView: View {
    let game: GameMetadata

    @Environment(\.dismiss) private var dismiss
    @StateObject private var input = ControllerMenuInput()
    @ObservedObject private var names = LibraryCustomNames.shared
    @State private var text: String
    @State private var focus = "field"
    @FocusState private var typing: Bool

    init(game: GameMetadata) {
        self.game = game
        _text = State(initialValue: LibraryCustomNames.shared.name(for: game.installKey) ?? game.cardName.name)
    }

    private var originalTitle: String { game.displayTitle ?? game.title }
    private var hasCustomName: Bool { names.name(for: game.installKey) != nil }
    private var order: [String] { hasCustomName ? ["field", "save", "reset"] : ["field", "save"] }

    var body: some View {
        ZStack {
            EspressUCoverGround(coverPath: game.coverPath)
            ScrollView {
                VStack(spacing: 22) {
                    EspressUCover(game: game, radius: 24)
                        .frame(width: 132, height: 132)
                        .shadow(color: EspressUTheme.shadow.opacity(0.15), radius: 14, y: 6)
                        .padding(.top, 40)

                    VStack(spacing: 6) {
                        Text("Rename")
                            .font(.title.weight(.bold))
                            .foregroundColor(EspressUTheme.brownDarkest)
                        Text("Only changes the name in your library. Original title: \(originalTitle)")
                            .font(.subheadline)
                            .foregroundColor(EspressUTheme.brownMid)
                            .multilineTextAlignment(.center)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    HStack(spacing: 10) {
                        Image(systemName: "pencil")
                            .foregroundColor(EspressUTheme.brownMid)
                        TextField("Name", text: $text)
                            .font(.headline)
                            .foregroundColor(EspressUTheme.brownDarkest)
                            .autocapitalization(.words)
                            .submitLabel(.done)
                            .focused($typing)
                            .onSubmit(save)
                        if !text.isEmpty {
                            Button { text = "" } label: {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundColor(EspressUTheme.brownMid)
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel("Clear")
                        }
                    }
                    .padding(.horizontal, 18)
                    .frame(height: 58)
                    .espressUSurface(RoundedRectangle(cornerRadius: 20, style: .continuous))
                    .espressUFocusRing(input.showsFocus && focus == "field", radius: 20)
                    .onTapGesture { typing = true }

                    Button(action: save) {
                        HStack(spacing: 8) {
                            if input.showsFocus {
                                Image(systemName: input.menuSymbol)
                                    .accessibilityHidden(true)
                            }
                            Text("Save")
                        }
                        .font(.headline)
                        .foregroundColor(EspressUTheme.onCremaTop)
                        .frame(maxWidth: .infinity, minHeight: 52)
                        .background(EspressUTheme.cremaTopGradient, in: Capsule())
                    }
                    .buttonStyle(.plain)
                    .espressUFocusRing(input.showsFocus && focus == "save")

                    if hasCustomName {
                        Button(action: reset) {
                            Label("Use the original title", systemImage: "arrow.uturn.backward")
                                .font(.body.weight(.semibold))
                                .foregroundColor(EspressUTheme.brownDarkest)
                                .frame(maxWidth: .infinity, minHeight: 48)
                                .espressUSurface(Capsule())
                        }
                        .buttonStyle(.plain)
                        .espressUFocusRing(input.showsFocus && focus == "reset")
                    }
                }
                .padding(.horizontal, 24)
                .padding(.bottom, 30)
                .frame(maxWidth: 520)
                .frame(maxWidth: .infinity)
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
    }

    private func save() {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        names.set(trimmed.isEmpty || trimmed == originalTitle ? nil : trimmed, for: game.installKey)
        close()
    }

    private func reset() {
        names.set(nil, for: game.installKey)
        close()
    }

    private func close() {
        typing = false
        input.stop()
        dismiss()
    }

    private func handle(_ command: ControllerMenuCommand) {
        switch command {
        case .up, .left:
            focus = EspressURowLogic.step(focus, in: order, by: -1) ?? "field"
        case .down, .right:
            focus = EspressURowLogic.step(focus, in: order, by: 1) ?? "field"
        case .confirm:
            switch focus {
            case "save": save()
            case "reset": reset()
            default: typing = true
            }
        case .menu:
            save()
        case .back:
            if typing { typing = false } else { close() }
        default:
            break
        }
    }
}
