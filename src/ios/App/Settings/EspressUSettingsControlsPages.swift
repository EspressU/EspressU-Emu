// SPDX-License-Identifier: MPL-2.0

import SwiftUI

extension EspressUSettingsView {

    var controllerMappingGroups: [SettingsGroup] {
        let session = mapping
        let connected = session.controllerName
        let listening = session.listeningPad

        var top: [SettingsItem] = [
            SettingsItem(id: "map.status", title: connected ?? loc("No controller connected"),
                         detail: connected == nil
                             ? loc("Pair one in the iOS Bluetooth settings to change buttons.")
                             : loc("Pick a GamePad button, then press the one you want on your controller."),
                         control: .info(connected == nil ? loc("None") : loc("Connected"), connected == nil ? nil : EspressUTheme.accentText)),
        ]
        if let pad = listening, let slot = PadMapping.slot(pad) {
            top.append(SettingsItem(id: "map.listening", title: loc("Press a button on your controller"),
                                    detail: locf("It will become GamePad %@. %@ s left.", slot.name, Int(session.secondsLeft.rounded(.up))),
                                    control: .action(loc("Cancel"), destructive: false, confirm: false, run: { session.cancel() })))
        }

        var groups = [SettingsGroup(id: "map.pad", title: loc("Your controller"), items: top)]
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
                    detail: changed ? locf("Built-in: %@", builtIn) : nil,
                    control: .padButton(label: slot.label,
                                        value: waiting ? loc("Press a button") : PadMapping.deviceName(PadMapping.device(for: slot.id)),
                                        changed: changed || waiting,
                                        run: { session.listen(for: slot.id) },
                                        reset: resetAction),
                    enabled: connected != nil && (listening == nil || waiting)))
            }
            groups.append(SettingsGroup(id: "map.\(group.title)", title: group.title, items: items))
        }

        groups.append(SettingsGroup(id: "map.more", title: loc("Reset"), items: [
            SettingsItem(id: "map.resetAll", title: loc("Reset every button"),
                         detail: loc("All GamePad buttons go back to the built-in layout."),
                         control: .action(loc("Reset"), destructive: true, confirm: true, run: { session.resetAll() }),
                         enabled: !PadMapping.overrides().isEmpty && listening == nil),
            SettingsItem(id: "tc.resetBindings", title: loc("Reset controller bindings"),
                         detail: loc("Rebuilds the default button bindings. Use it if some buttons do nothing while the sticks still work."),
                         control: .action(loc("Reset"), destructive: true, confirm: true, run: {
                             let ok = cemu_bridge_reset_controller_bindings()
                             let count = Int(cemu_bridge_input_button_mapping_count())
                             notes["tc.resetBindings"] = ok
                                 ? locf("Reset. The GamePad now has %@ button bindings.", count)
                                 : (count < 0 ? loc("No GamePad is wired yet - start a game, then try again.")
                                              : loc("Reset, but the GamePad still has no button bindings."))
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
            saved.append(SettingsItem(id: "srv.none", title: loc("No saved servers yet"), control: .info("", nil)))
        }
        for server in servers {
            let isActive = active != nil && active == server.data
            saved.append(SettingsItem(id: "srv.use.\(server.id)", title: server.name,
                                      detail: isActive ? loc("The active Custom server. Pick Custom under Network Service to use it.") : loc("Make this the active Custom server."),
                                      control: isActive ? .info(loc("Active"), EspressUTheme.accentText)
                                                        : .action(loc("Use"), destructive: false, confirm: false, run: {
                                                            do {
                                                                try CustomServerStore.activate(server)
                                                                pageAlert = SettingsAlert(title: loc("Custom servers"),
                                                                                          message: locf("%@ is now the active Custom server. Pick Custom under Network Service to use it.", server.name))
                                                            } catch {
                                                                pageAlert = SettingsAlert(title: loc("Custom servers"),
                                                                                          message: locf("Couldn't make that server active: %@", error.localizedDescription))
                                                            }
                                                            values.refresh()
                                                        }),
                                      enabled: !locked || isActive))
            saved.append(SettingsItem(id: "srv.del.\(server.id)", title: locf("Delete %@", server.name),
                                      detail: isActive ? loc("The active network_services.xml goes too.") : nil,
                                      control: .action(loc("Delete"), destructive: true, confirm: true, run: {
                                          CustomServerStore.delete(server)
                                          values.refresh()
                                      }),
                                      enabled: !locked))
        }
        let add: [SettingsItem] = [
            SettingsItem(id: "srv.import", title: loc("Import network_services.xml"),
                         detail: locked ? loc("Servers can't be changed while a game is running. Close the game first.") : loc("Opens Files to pick the file."),
                         control: .action(loc("Import"), destructive: false, confirm: false, run: { importingServer = true }),
                         enabled: !locked),
            SettingsItem(id: "srv.enter", title: loc("Enter server addresses"),
                         detail: loc("Type the addresses the server's host gave you."),
                         control: .action(loc("Add"), destructive: false, confirm: false, run: { showingServerForm = true }),
                         enabled: !locked),
        ]
        return [
            SettingsGroup(id: "srv.saved", title: loc("Custom servers"), items: saved),
            SettingsGroup(id: "srv.add", title: loc("Add"), items: add),
            SettingsGroup(id: "srv.community", title: loc("Community servers"), items: [
                SettingsItem(id: "srv.mh3u", title: loc("MH3U Revival"),
                             detail: loc("A self-hosted server for Monster Hunter 3 Ultimate. Someone runs the host and gives you a network_services.xml to import. EspressU doesn't run or check it."),
                             control: .action(loc("Open"), destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://github.com/Matt-Wood-23/mh3u-revival") { UIApplication.shared.open(url) }
                             })),
            ]),
        ]
    }

    func importCustomServer(_ result: Result<URL, Error>) {
        func say(_ text: String) { pageAlert = SettingsAlert(title: loc("Custom servers"), message: text) }
        switch result {
        case .failure(let error):
            say(locf("Couldn't import: %@", error.localizedDescription))
        case .success(let url):
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else { say(loc("Couldn't read that file.")); return }
            if let problem = CustomServerStore.validate(data) { say(problem); return }
            do {
                let saved = try CustomServerStore.save(data)
                try CustomServerStore.activate(saved)
                say(locf("Added %@ and made it the active Custom server. Pick Custom under Network Service to use it.", saved.name))
            } catch {
                say(locf("Couldn't save that server: %@", error.localizedDescription))
            }
            values.refresh()
        }
    }
}
