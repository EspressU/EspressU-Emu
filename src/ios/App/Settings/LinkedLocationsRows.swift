// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

struct LinkedLocationsRows: View {
    @ObservedObject var gameManager: GameManager
    @ObservedObject private var library = ExternalLibrary.shared
    @State private var pendingUnlink: LinkedLocation?
    @State private var errorMessage: String?

    var body: some View {
        Group {
            ForEach(library.locations) { location in
                locationRow(location)
            }
            Button {
                beginLink(contentTypes: [.folder])
            } label: {
                Label("Link a folder\u{2026}", systemImage: "externaldrive.badge.plus")
                    .font(.subheadline.weight(.semibold))
            }
            Button {
                beginLink(contentTypes: [.item])
            } label: {
                Label("Link a game file\u{2026}", systemImage: "link")
                    .font(.subheadline.weight(.semibold))
            }
            Text("Linked games stay where they are. EspressU never copies, moves or deletes them. A game on a drive that isn't connected stays in the library until you plug the drive back in.")
                .font(.caption)
                .foregroundColor(EspressUTheme.brownMid)
        }
        .confirmationDialog(
            "Unlink this location?",
            isPresented: Binding(get: { pendingUnlink != nil }, set: { if !$0 { pendingUnlink = nil } }),
            titleVisibility: .visible,
            presenting: pendingUnlink
        ) { location in
            Button("Unlink", role: .destructive) {
                Task { await gameManager.unlinkLocation(location.id) }
            }
            Button("Cancel", role: .cancel) {}
        } message: { location in
            Text("\"\(location.name)\" stops appearing in your library. Nothing on the drive or in the folder is deleted. Your saves and options are kept.")
        }
        .alert("Couldn't link that", isPresented: Binding(get: { errorMessage != nil }, set: { if !$0 { errorMessage = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorMessage ?? "")
        }
    }

    private func locationRow(_ location: LinkedLocation) -> some View {
        let connected = library.availability[location.id] ?? true
        return VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: location.kind == .folder ? "folder" : "doc")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(EspressUTheme.brownMid)
                    .frame(width: 20)
                VStack(alignment: .leading, spacing: 2) {
                    Text(location.name)
                        .font(.subheadline.weight(.semibold))
                        .fixedSize(horizontal: false, vertical: true)
                    Text(statusLine(for: location, connected: connected))
                        .font(.caption)
                        .foregroundColor(connected ? EspressUTheme.brownMid : EspressUTheme.alertText)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 8)
                Button("Unlink") { pendingUnlink = location }
                    .font(.subheadline.weight(.semibold))
                    .buttonStyle(.borderless)
                    .frame(minHeight: 44)
                    .accessibilityLabel("Unlink \(location.name)")
            }
            if !location.hidden.isEmpty {
                Button("Show \(location.hidden.count) removed game\(location.hidden.count == 1 ? "" : "s") again") {
                    Task { await gameManager.restoreHiddenGames(in: location.id) }
                }
                .font(.footnote.weight(.semibold))
                .buttonStyle(.borderless)
            }
        }
    }

    private func statusLine(for location: LinkedLocation, connected: Bool) -> String {
        if !connected { return ExternalLibrary.unavailableMessage }
        let count = location.games.count
        return count == 1 ? "Connected, 1 game" : "Connected, \(count) games"
    }

    private func beginLink(contentTypes: [UTType]) {
        DocumentImport.present(contentTypes: contentTypes) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                Task {
                    do {
                        try await gameManager.linkLocation(url)
                    } catch {
                        errorMessage = error.localizedDescription
                    }
                }
            case .failure(let error):
                errorMessage = error.localizedDescription
            }
        }
    }
}
