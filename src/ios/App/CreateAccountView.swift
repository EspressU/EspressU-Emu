import SwiftUI

/// Creates a plain local profile. Games use it to keep their saves apart; nothing here leaves the device.
struct CreateAccountView: View {
    @Environment(\.dismiss) private var dismiss

    @State private var miiName = ""
    @State private var errorMessage: String?
    @FocusState private var nameFocused: Bool

    var body: some View {
        EspressUNavigation {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                Form {
                    Section {
                        HStack {
                            Text("Name")
                                .font(.subheadline.weight(.semibold))
                            TextField("Name", text: $miiName)
                                .multilineTextAlignment(.trailing)
                                .focused($nameFocused)
                                .submitLabel(.done)
                            Text("\(miiName.count)/10")
                                .font(.caption2.weight(.semibold))
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
                        Text("A profile keeps the saves of a game apart. It is stored on this device only.")
                    }
                }
            }
            .navigationTitle("New Profile")
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
        .onAppear {
            if !ControllerPresence.isConnected() { nameFocused = true }
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
        guard !cemu_bridge_accounts_locked() else {
            errorMessage = "Close the running game before creating a profile."
            return
        }
        guard cemu_bridge_accounts_has_free_slot() else {
            errorMessage = "You've reached the maximum number of profiles."
            return
        }
        guard !miiName.isEmpty else {
            errorMessage = "Enter a name."
            return
        }

        let persistentId = max(cemu_bridge_accounts_next_persistent_id(), cemu_bridge_accounts_min_persistent_id())
        let created = miiName.withCString { namePtr in
            "".withCString { emailPtr in
                cemu_bridge_account_create(persistentId, namePtr, 2000, 1, 1, 0, emailPtr, 0)
            }
        }

        guard created else {
            errorMessage = "Couldn't create that profile."
            return
        }

        dismiss()
    }
}
