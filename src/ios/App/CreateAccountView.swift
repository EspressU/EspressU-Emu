import SwiftUI

struct CreateAccountView: View {
    @Environment(\.dismiss) private var dismiss

    @State private var persistentIdText: String
    @State private var miiName = ""
    @State private var birthDate: Date
    @State private var gender = 0
    @State private var email = ""
    @State private var country = 0
    @State private var countries: [AccountCountry] = []
    @State private var errorMessage: String?
    @FocusState private var nameFocused: Bool

    init() {
        _persistentIdText = State(initialValue: String(cemu_bridge_accounts_next_persistent_id(), radix: 16))
        _birthDate = State(initialValue: Calendar.current.date(from: DateComponents(year: 2000, month: 1, day: 1)) ?? Date())
    }

    var body: some View {
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                Form {
                    Section {
                        Text("A new account plays offline. It can't be linked to a Pretendo ID here, so games will still ask you to link one. To play online, link a Pretendo ID on a real Wii U, then use Import from Wii U in Settings > Network Service.")
                            .font(.system(size: 13, design: .rounded))
                            .foregroundColor(EspressUTheme.brownDarkest)
                    }

                    Section {
                        HStack {
                            Text("Persistent ID")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            TextField("Persistent ID", text: $persistentIdText)
                                .multilineTextAlignment(.trailing)
                                .font(.body.monospaced())
                                .keyboardType(.asciiCapable)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                        }
                        HStack {
                            Text("Mii name")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            TextField("Mii name", text: $miiName)
                                .multilineTextAlignment(.trailing)
                                .focused($nameFocused)
                                .submitLabel(.done)
                            Text("\(miiName.count)/10")
                                .font(.system(size: 11, weight: .semibold, design: .rounded))
                                .foregroundColor(miiName.count >= 10 ? EspressUTheme.alertText : EspressUTheme.brownMid)
                                .monospacedDigit()
                                .accessibilityLabel("\(miiName.count) of 10 characters used")
                        }
                        .onValueChange(of: miiName) { newValue in
                            let trimmedName = String(newValue.prefix(10))
                            if trimmedName != newValue {
                                miiName = trimmedName
                            }
                        }
                    } footer: {
                        Text("Names the folder your saves live in. Leave it alone unless you're importing saves from a real Wii U.")
                    }

                    Section("Mii details") {
                        DatePicker(selection: $birthDate, in: ...Date(), displayedComponents: .date) {
                            Text("Birthday")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                        }
                        Picker(selection: $gender) {
                            Text("Male").tag(0)
                            Text("Female").tag(1)
                        } label: {
                            Text("Gender")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                        }
                        .pickerStyle(.segmented)
                        Picker(selection: $country) {
                            ForEach(countries) { entry in
                                Text(entry.name).tag(entry.code)
                            }
                        } label: {
                            Text("Country")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                        }
                        .pickerStyle(.menu)
                        .tint(EspressUTheme.accentText)
                    }

                    Section {
                        TextField("Email (optional)", text: $email)
                            .keyboardType(.emailAddress)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                    } footer: {
                        Text("Only needed if you later link this account to an NNID/PNID for online play.")
                    }
                }
            }
            .navigationTitle("New Account")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") {
                        dismiss()
                    }
                }

                ToolbarItem(placement: .confirmationAction) {
                    Button("Create") {
                        createAccount()
                    }
                }
            }
        }
        .navigationViewStyle(.stack)
        .onAppear {
            if !ControllerPresence.isConnected() { nameFocused = true }
            countries = AccountCountry.loadAll()
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

    private func createAccount() {
        let idString = persistentIdText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !idString.isEmpty else {
            errorMessage = "Enter a persistent ID."
            return
        }

        guard !cemu_bridge_accounts_locked() else {
            errorMessage = "Close the running game before creating an account."
            return
        }
        guard cemu_bridge_accounts_has_free_slot() else {
            errorMessage = "You've reached the maximum number of accounts."
            return
        }
        guard let persistentId = UInt32(idString, radix: 16) else {
            errorMessage = "The persistent ID must be hexadecimal (0-9, A-F)."
            return
        }
        let minimumPersistentId = cemu_bridge_accounts_min_persistent_id()
        guard persistentId >= minimumPersistentId else {
            errorMessage = "The persistent ID must be \(String(minimumPersistentId, radix: 16)) or higher."
            return
        }

        let existingAccounts = Account.loadAll()
        if let existing = existingAccounts.first(where: { $0.persistentId == persistentId }) {
            errorMessage = "That ID is already used by account \(existing.displayName)."
            return
        }

        guard !miiName.isEmpty else {
            errorMessage = "Enter a Mii name."
            return
        }

        let components = Calendar.current.dateComponents([.year, .month, .day], from: birthDate)
        let created = miiName.withCString { miiNamePtr in
            email.withCString { emailPtr in
                cemu_bridge_account_create(
                    persistentId,
                    miiNamePtr,
                    UInt16(components.year ?? 2000),
                    UInt8(components.month ?? 1),
                    UInt8(components.day ?? 1),
                    Int32(gender),
                    emailPtr,
                    Int32(country)
                )
            }
        }

        guard created else {
            errorMessage = "Couldn't create that account."
            return
        }

        dismiss()
    }
}
