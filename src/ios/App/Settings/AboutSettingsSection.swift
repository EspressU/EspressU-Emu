import SwiftUI

private extension Bundle {
    var appVersionString: String {
        let short = infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0"
        let build = infoDictionary?["CFBundleVersion"] as? String ?? "0"
        return "\(short) (\(build))"
    }
}

struct AboutSettingsSection: View {
    @State private var showingResetConfirmation = false
    @State private var resetMessage: String?

    var body: some View {
        Section {
            SettingsRow(label: "Version", value: Bundle.main.appVersionString, icon: "number")
            SettingsRow(label: "Created by", value: "Amar", icon: "person.fill")
            Link(destination: URL(string: "https://github.com/EspressU/EspressU-Emu")!) {
                Label("View on GitHub", systemImage: "arrow.up.right.square")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            Link(destination: URL(string: "https://discord.gg/espressu")!) {
                Label("Join the Discord", systemImage: "bubble.left.and.bubble.right")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            NavigationLink {
                OpenSourceLicencesView()
            } label: {
                Label("Open-source licences", systemImage: "doc.text")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }

            Text("EspressU is built on Cemu and based on [MuffinEMU](https://github.com/MuffinEMU/Muffin-EMU) by Void.")
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)

            SettingsOnboardingRow(onRequestReopen: {
                NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
            })

            Button(role: .destructive) {
                showingResetConfirmation = true
            } label: {
                DestructiveSettingsLabel(title: "Reset settings to defaults", systemImage: "arrow.counterclockwise")
            }

            if let resetMessage {
                Text(resetMessage)
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        } header: {
            SettingsSectionHeader("About", icon: "info.circle", accent: .system)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .confirmationDialog("Reset settings to defaults?", isPresented: $showingResetConfirmation, titleVisibility: .visible) {
            Button("Reset Settings", role: .destructive) {
                SettingsDefaults.reset(includingPerGameOverrides: false)
                resetMessage = "Settings reset to defaults."
                NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
            }
            Button("Reset Settings and Per-Game Options", role: .destructive) {
                SettingsDefaults.reset(includingPerGameOverrides: true)
                resetMessage = "Settings and per-game options reset to defaults."
                NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Puts every option in Settings back to its default, including controls, graphics, audio, overlay, appearance and Built-in JIT with its pairing file, and shows the welcome guide again. Your library, favourites, keys.txt, accounts and theme are not affected. Per-game options stay unless you pick the second button.")
        }
    }
}
