// SPDX-License-Identifier: MPL-2.0

import SwiftUI

extension EspressUSettingsView {

    var controllerMappingGroups: [SettingsGroup] {
        let session = mapping
        let connected = session.controllerName
        let listening = session.listeningPad

        var top: [SettingsItem] = [
            SettingsItem(id: "map.status", title: connected ?? "No controller connected",
                         detail: connected == nil
                             ? "Pair one in the iOS Bluetooth settings to change buttons."
                             : "Pick a GamePad button, then press the one you want on your controller.",
                         control: .info(connected == nil ? "None" : "Connected", connected == nil ? nil : EspressUTheme.accentText)),
        ]
        if let pad = listening, let slot = PadMapping.slot(pad) {
            top.append(SettingsItem(id: "map.listening", title: "Press a button on your controller",
                                    detail: "It will become GamePad \(slot.name). \(Int(session.secondsLeft.rounded(.up))) s left.",
                                    control: .action("Cancel", destructive: false, confirm: false, run: { session.cancel() })))
        }

        var groups = [SettingsGroup(id: "map.pad", title: "Your controller", items: top)]
        for group in ControllerMappingSession.groupLayout {
            var items: [SettingsItem] = []
            for pad in group.pads {
                guard let slot = PadMapping.slot(Int(pad.rawValue)) else { continue }
                let changed = PadMapping.isRemapped(slot.id)
                let builtIn = PadMapping.deviceName(PadMapping.defaultDevice(for: slot.id))
                let waiting = listening == slot.id
                var resetAction: (() -> Void)? = nil
                if changed && listening == nil { resetAction = { session.reset(pad: slot.id) } }
                items.append(SettingsItem(
                    id: "map.pad.\(slot.id)", title: slot.name,
                    detail: changed ? "Built-in: \(builtIn)" : nil,
                    control: .padButton(label: slot.label,
                                        value: waiting ? "Press a button" : PadMapping.deviceName(PadMapping.device(for: slot.id)),
                                        changed: changed || waiting,
                                        run: { session.listen(for: slot.id) },
                                        reset: resetAction),
                    enabled: connected != nil && (listening == nil || waiting)))
            }
            groups.append(SettingsGroup(id: "map.\(group.title)", title: group.title, items: items))
        }

        groups.append(SettingsGroup(id: "map.more", title: "Reset", items: [
            SettingsItem(id: "map.resetAll", title: "Reset every button",
                         detail: "All GamePad buttons go back to the built-in layout.",
                         control: .action("Reset", destructive: true, confirm: true, run: { session.resetAll() }),
                         enabled: !PadMapping.overrides().isEmpty && listening == nil),
            SettingsItem(id: "tc.resetBindings", title: "Reset controller bindings",
                         detail: "Rebuilds the default button bindings. Use it if some buttons do nothing while the sticks still work.",
                         control: .action("Reset", destructive: true, confirm: true, run: {
                             let ok = cemu_bridge_reset_controller_bindings()
                             let count = Int(cemu_bridge_input_button_mapping_count())
                             notes["tc.resetBindings"] = ok
                                 ? "Reset. The GamePad now has \(count) button bindings."
                                 : (count < 0 ? "No GamePad is wired yet - start a game, then try again."
                                              : "Reset, but the GamePad still has no button bindings.")
                         }),
                         enabled: listening == nil),
        ]))
        return groups
    }

    var customServerGroups: [SettingsGroup] {
        let servers = CustomServerStore.list()
        let active = CustomServerStore.activeData()
        let locked = cemu_bridge_accounts_locked()
        var saved: [SettingsItem] = []
        if servers.isEmpty {
            saved.append(SettingsItem(id: "srv.none", title: "No saved servers yet", control: .info("", nil)))
        }
        for server in servers {
            let isActive = active != nil && active == server.data
            saved.append(SettingsItem(id: "srv.use.\(server.id)", title: server.name,
                                      detail: isActive ? "The active Custom server. Pick Custom under Network Service to use it." : "Make this the active Custom server.",
                                      control: isActive ? .info("Active", EspressUTheme.accentText)
                                                        : .action("Use", destructive: false, confirm: false, run: {
                                                            do {
                                                                try CustomServerStore.activate(server)
                                                                pageAlert = SettingsAlert(title: "Custom servers",
                                                                                          message: "\(server.name) is now the active Custom server. Pick Custom under Network Service to use it.")
                                                            } catch {
                                                                pageAlert = SettingsAlert(title: "Custom servers",
                                                                                          message: "Couldn't make that server active: \(error.localizedDescription)")
                                                            }
                                                            values.refresh()
                                                        }),
                                      enabled: !locked || isActive))
            saved.append(SettingsItem(id: "srv.del.\(server.id)", title: "Delete \(server.name)",
                                      detail: isActive ? "The active network_services.xml goes too." : nil,
                                      control: .action("Delete", destructive: true, confirm: true, run: {
                                          CustomServerStore.delete(server)
                                          values.refresh()
                                      }),
                                      enabled: !locked))
        }
        let add: [SettingsItem] = [
            SettingsItem(id: "srv.import", title: "Import network_services.xml",
                         detail: locked ? "Servers can't be changed while a game is running. Close the game first." : "Opens Files to pick the file.",
                         control: .action("Import", destructive: false, confirm: false, run: { importingServer = true }),
                         enabled: !locked),
            SettingsItem(id: "srv.enter", title: "Enter server addresses",
                         detail: "Type the addresses the server's host gave you.",
                         control: .action("Add", destructive: false, confirm: false, run: { showingServerForm = true }),
                         enabled: !locked),
        ]
        return [
            SettingsGroup(id: "srv.saved", title: "Custom servers", items: saved),
            SettingsGroup(id: "srv.add", title: "Add", items: add),
            SettingsGroup(id: "srv.community", title: "Community servers", items: [
                SettingsItem(id: "srv.mh3u", title: "MH3U Revival",
                             detail: "A self-hosted server for Monster Hunter 3 Ultimate. Someone runs the host and gives you a network_services.xml to import. EspressU doesn't run or check it.",
                             control: .action("Open", destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://github.com/Matt-Wood-23/mh3u-revival") { UIApplication.shared.open(url) }
                             })),
            ]),
        ]
    }

    func importCustomServer(_ result: Result<URL, Error>) {
        func say(_ text: String) { pageAlert = SettingsAlert(title: "Custom servers", message: text) }
        switch result {
        case .failure(let error):
            say("Couldn't import: \(error.localizedDescription)")
        case .success(let url):
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else { say("Couldn't read that file."); return }
            if let problem = CustomServerStore.validate(data) { say(problem); return }
            do {
                let saved = try CustomServerStore.save(data)
                try CustomServerStore.activate(saved)
                say("Added \(saved.name) and made it the active Custom server. Pick Custom under Network Service to use it.")
            } catch {
                say("Couldn't save that server: \(error.localizedDescription)")
            }
            values.refresh()
        }
    }
}
