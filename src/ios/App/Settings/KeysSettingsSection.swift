// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

struct KeysSettingsSection: View {
    @State private var showingKeysImporter = false
    @State private var showingKeysRemovalConfirmation = false
    @State private var keyCount = WiiUKeys.installedKeyCount()
    @State private var keysErrorMessage: String?

    var body: some View {
        Section {
            Button(action: { showingKeysImporter = true }) {
                Label(WiiUKeys.keysFileExists() ? "Replace keys.txt" : "Import keys.txt",
                      systemImage: "key")
            }
            .foregroundColor(EspressUTheme.brownDarkest)

            if WiiUKeys.keysFileExists() {
                SettingsRow(label: "Keys loaded", value: "\(keyCount)")
                Button(role: .destructive, action: { showingKeysRemovalConfirmation = true }) {
                    DestructiveSettingsLabel(title: "Remove keys.txt", systemImage: "trash")
                }
            }
        } header: {
            SettingsSectionHeader("Wii U Keys", icon: "key", accent: .content)
        } footer: {
            InfoButton.footer(
                "Only needed for encrypted games. Homebrew and already-decrypted dumps don't need keys.",
                title: "Wii U Keys",
                text: "Encrypted games (.wux, .wud, .iso, .wua) need AES keys dumped from your own Wii U, in a text file called keys.txt with one key per line. EspressU doesn't include keys.\n\nYou can also open EspressU in the Files app and drop keys.txt into the \"keys\" folder.\n\nNew keys are used the next time you start a game. If you've already started a game since opening EspressU, quit and reopen EspressU first.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .fileImporter(
            isPresented: $showingKeysImporter,
            allowedContentTypes: [.item],
            allowsMultipleSelection: false
        ) { result in
            handleKeysImport(result)
        }
        .alert("Couldn't import keys", isPresented: .constant(keysErrorMessage != nil), presenting: keysErrorMessage) { _ in
            Button("OK") { keysErrorMessage = nil }
        } message: { message in
            Text(message)
        }
        .confirmationDialog("Remove keys.txt?", isPresented: $showingKeysRemovalConfirmation, titleVisibility: .visible) {
            Button("Remove", role: .destructive) {
                do {
                    try WiiUKeys.removeKeys()
                } catch {
                    keysErrorMessage = "Couldn't remove keys.txt: \(error.localizedDescription)"
                }
                keyCount = WiiUKeys.installedKeyCount()
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Encrypted games won't run until you import a keys.txt again. Homebrew is unaffected.")
        }
    }

    private func handleKeysImport(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }
            do {
                keyCount = try WiiUKeys.importKeys(from: url)
            } catch {
                keysErrorMessage = error.localizedDescription
            }
        case .failure(let error):
            keysErrorMessage = error.localizedDescription
        }
    }
}
