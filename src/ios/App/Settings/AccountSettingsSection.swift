import SwiftUI
import Combine

final class AccountSheetRouter: ObservableObject {
    static let shared = AccountSheetRouter()
    @Published var showingCreateAccount = false
    @Published private(set) var revision = 0
    func sheetClosed() { revision += 1 }
}

struct AccountSettingsSection: View {
    @State private var accounts: [Account] = []
    @State private var activePersistentId: UInt32 = 0
    @State private var locked = false
    @ObservedObject private var accountSheets = AccountSheetRouter.shared
    @State private var accountToDelete: Account?
    @State private var errorMessage: String?

    private var activeAccount: Account? {
        accounts.first { $0.persistentId == activePersistentId }
    }

    @ViewBuilder private var accountFooter: some View {
        if locked {
            InfoButton.footer("Accounts can't be changed while a game is running. Close the game first.")
        } else if let note = OnlineReadiness.note(accountId: activePersistentId,
                                                  service: NetworkService(cemu_bridge_network_service(activePersistentId))) {
            InfoButton.footer(
                note,
                title: "Online play",
                text: OnlineReadiness.howToFix + "\n\nImport from Wii U accepts account.dat, its 800000XX folder, otp.bin and seeprom.bin. EspressU can't create a linked account itself.")
        } else if let activeAccount, !activeAccount.isValidOnline {
            InfoButton.footer(
                "This account has no cached NNID/PNID login, so it can't play online yet.",
                title: "Account",
                text: "Online play needs an account with a saved NNID or PNID login. Sign in on a real console and copy its account.dat here; EspressU can't create one.")
        }
    }

    var body: some View {
        Section {
            Picker("Active account", selection: Binding(
                get: { activePersistentId },
                set: { newValue in
                    activePersistentId = newValue
                    cemu_bridge_set_active_account_persistent_id(newValue)
                }
            )) {
                ForEach(accounts) { account in
                    Text(account.displayNameWithId).tag(account.persistentId)
                }
            }
            .pickerStyle(.menu)
            .tint(EspressUTheme.accentText)
            .disabled(locked || accounts.isEmpty)

            HStack {
                Button("Create") { accountSheets.showingCreateAccount = true }
                    .disabled(locked || !cemu_bridge_accounts_has_free_slot())
                Spacer()
                Button("Delete", role: .destructive) { accountToDelete = activeAccount }
                    .disabled(locked || accounts.count <= 1 || activeAccount == nil)
            }
            .buttonStyle(.borderless)

            AccountImportButton(locked: locked, onChange: reload)
                .buttonStyle(.borderless)
        } header: {
            SettingsSectionHeader("Account", icon: "person.crop.circle", accent: .content)
        } footer: {
            accountFooter
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear(perform: reload)
        .refreshable { reload() }
        .onReceive(accountSheets.$revision.dropFirst()) { _ in reload() }
        .confirmationDialog(
            "Delete account?",
            isPresented: Binding(
                get: { accountToDelete != nil },
                set: { if !$0 { accountToDelete = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button("Delete", role: .destructive) {
                if let account = accountToDelete, !cemu_bridge_account_delete(account.persistentId) {
                    errorMessage = "Couldn't delete that account."
                }
                accountToDelete = nil
                reload()
            }
            Button("Cancel", role: .cancel) { accountToDelete = nil }
        } message: {
            if let account = accountToDelete {
                Text("Are you sure you want to delete \(account.displayName) (\(account.persistentIdHex))?")
            }
        }
        .alert("Error", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(errorMessage ?? "")
        }
    }

    private func reload() {
        cemu_bridge_accounts_refresh()
        accounts = Account.loadAll()
        activePersistentId = cemu_bridge_active_account_persistent_id()
        locked = cemu_bridge_accounts_locked()
    }
}

struct NetworkServiceSettingsSection: View {
    @State private var activePersistentId: UInt32 = 0
    @State private var activeAccountName: String?
    @State private var selectedService: NetworkService = .offline
    @State private var locked = false
    @State private var customAvailable = false
    @State private var accounts: [Account] = []
    @State private var hasOTP = false
    @State private var hasSeeprom = false

    private var readinessLine: String {
        let missing = OnlineReadiness.missingItems(accountId: activePersistentId)
        if selectedService == .offline { return "Online play is off. Pick a Network Service to use it." }
        if selectedService == .custom { return customAvailable ? "Ready for online play (custom server)" : "Not ready for online play. Missing: a valid network_services.xml." }
        return missing.isEmpty ? "Ready for online play" : "Not ready for online play. Missing: " + missing.joined(separator: ", ") + "."
    }

    var body: some View {
        Section {
            Text(readinessLine)
                .font(.system(size: 14, weight: .semibold, design: .rounded))

            DeviceConnectionStatusRow()

            NavigationLink {
                Form { CustomServersSettingsSection() }
                    .navigationTitle("Custom servers")
            } label: {
                Label("Custom servers", systemImage: "server.rack")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }

            ForEach(NetworkService.allCases) { service in
                Button {
                    cemu_bridge_set_network_service(activePersistentId, service.bridgeValue)
                    selectedService = service
                } label: {
                    HStack {
                        Text(service.string)
                        Spacer()
                        if selectedService == service {
                            Image(systemName: "checkmark")
                        }
                    }
                }
                .disabled(locked || activeAccountName == nil
                          || (service == .custom && !customAvailable)
                          || (service == .nintendo && selectedService != .nintendo))
            }
        } header: {
            SettingsSectionHeader("Network Service\(activeAccountName.map { " (\($0))" } ?? "")",
                                  icon: "network", accent: .content)
        } footer: {
            InfoButton.footer(
                locked ? "The Network Service can't be changed while a game is running. Close the game first." : selectedService.accountHelp,
                title: "Network Service",
                text: "Pretendo is a community-run replacement for Nintendo's Wii U online services. Its server addresses are built in, so there's nothing to configure.\n\nNintendo's own servers have been shut down, so that option can't be selected.\n\nCustom works with a network_services.xml, the same file desktop Cemu reads. Add one under Custom servers below. It needs no linked account or otp.bin and seeprom.bin.\n\n" + OnlineReadiness.howToFix)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear(perform: reload)
        .onReceive(NotificationCenter.default.publisher(for: .customServerChanged)) { _ in reload() }
        .refreshable { reload() }
    }

    private func reload() {
        activePersistentId = cemu_bridge_active_account_persistent_id()
        let loaded = Account.loadAll()
        accounts = loaded
        activeAccountName = loaded.first { $0.persistentId == activePersistentId }?.displayName
        selectedService = NetworkService(cemu_bridge_network_service(activePersistentId))
        locked = cemu_bridge_accounts_locked()
        customAvailable = cemu_bridge_custom_network_service_available()
        let root = WiiUMenu.mlcRootURL
        hasOTP = root.map { FileManager.default.fileExists(atPath: $0.appendingPathComponent("otp.bin").path) } ?? false
        hasSeeprom = root.map { FileManager.default.fileExists(atPath: $0.appendingPathComponent("seeprom.bin").path) } ?? false
    }
}
