// SPDX-License-Identifier: MPL-2.0

import SwiftUI

extension EspressUSettingsView {

    var touchControlsGroups: [SettingsGroup] {
        let percent: (Double) -> String = { "\(Int(($0 * 100).rounded()))%" }
        let defaults = UserDefaults.standard
        let advanced = SettingsMode.isAdvanced

        let size: [SettingsItem] = [
            SettingsItem(id: "tc.size", title: "Button size", detail: "Bigger buttons are easier to hit.",
                         control: .slider(values.double(ControllerLayoutSettings.scaleKey, ControllerLayoutSettings.defaultScale),
                                          ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale, step: 0.05, label: percent)),
            SettingsItem(id: "tc.aScale", title: "A button size",
                         detail: "Makes A bigger and easier to hit. The buttons around it shrink a little.",
                         control: .slider(values.double(TouchLabSettings.aScaleKey, TouchLabSettings.defaultAScale),
                                          TouchLabSettings.aScaleRange, step: 0.01, label: percent)),
            SettingsItem(id: "tc.opacity", title: "Opacity", detail: "How see-through the buttons are.",
                         control: .slider(values.double(ControllerLayoutSettings.opacityKey, ControllerLayoutSettings.defaultOpacity),
                                          0.2...1.0, step: 0.05, label: percent)),
        ]

        var placement: [SettingsItem] = [
            SettingsItem(id: "tc.spacing", title: "Stick spacing",
                         detail: "Moves both sticks closer together or further apart.",
                         control: .slider(values.double(ControllerLayoutSettings.stickSpacingKey, ControllerLayoutSettings.defaultStickSpacing),
                                          ControllerLayoutSettings.minStickSpacing...ControllerLayoutSettings.maxStickSpacing,
                                          step: ControllerLayoutSettings.stickSpacingStep,
                                          label: { ControllerLayoutSettings.stickSpacingLabel($0) })),
        ]
        if ControllerLayoutSettings.supportsShoulderOffset {
            placement.append(SettingsItem(id: "tc.shoulders", title: "L, R, ZL and ZR height",
                                          detail: "Moves the four shoulder buttons up or down together.",
                                          control: .slider(values.double(ControllerLayoutSettings.shoulderOffsetKey, ControllerLayoutSettings.defaultShoulderOffset),
                                                           ControllerLayoutSettings.shoulderOffsetRange(touchLab: true),
                                                           step: ControllerLayoutSettings.shoulderOffsetStep,
                                                           label: { ControllerLayoutSettings.shoulderOffsetLabel(max(0, $0)) })))
        }

        let feel: [SettingsItem] = [
            SettingsItem(id: "tc.haptics", title: "Haptic feedback", detail: "A light tap on press.",
                         control: .toggle(values.bool(ControllerLayoutSettings.hapticsKey, ControllerLayoutSettings.defaultHaptics))),
            SettingsItem(id: "tc.autoHide", title: "Hide when a controller is connected",
                         detail: "The controls come back when it disconnects. The GamePad's screen stays touchable.",
                         control: .toggle(values.bool(ControllerLayoutSettings.autoHideWithControllerKey,
                                                      ControllerLayoutSettings.defaultAutoHideWithController))),
        ]

        var groups = [
            SettingsGroup(id: "tc.size", title: "Size and look", items: size),
            SettingsGroup(id: "tc.placement", title: "Placement", items: placement),
            SettingsGroup(id: "tc.feel", title: "Behaviour", items: feel),
        ]

        if advanced {
            let gate = ControllerGeometry.StickGate(rawValue: defaults.string(forKey: ControllerLayoutSettings.stickGateKey)
                                                    ?? ControllerLayoutSettings.defaultStickGateRaw) ?? ControllerLayoutSettings.defaultStickGate
            groups.append(SettingsGroup(id: "tc.sticks", title: "Sticks", items: [
                SettingsItem(id: "tc.gate", title: "Stick gate", detail: gate.summary,
                             control: .choice(ControllerGeometry.StickGate.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                                              values.string(ControllerLayoutSettings.stickGateKey, ControllerLayoutSettings.defaultStickGateRaw))),
                SettingsItem(id: "tc.deadzone", title: "Stick deadzone",
                             detail: "How far you can move before the game notices.",
                             control: .slider(values.double(ControllerLayoutSettings.deadzoneKey, ControllerLayoutSettings.defaultDeadzone),
                                              ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone, step: 0.01,
                                              label: { $0 <= 0.0005 ? "off" : "\(Int(($0 * 100).rounded()))%" })),
                SettingsItem(id: "tc.curve", title: "Fine control",
                             detail: "Makes small movements gentler.",
                             control: .slider(values.double(ControllerLayoutSettings.stickCurveKey, ControllerLayoutSettings.defaultStickCurve),
                                              ControllerLayoutSettings.minStickCurve...ControllerLayoutSettings.maxStickCurve, step: 0.1,
                                              label: { $0 <= ControllerLayoutSettings.minStickCurve + 0.005 ? "linear" : String(format: "%.1fx", $0) })),
            ]))
        }

        groups.append(SettingsGroup(id: "tc.more", title: "Reset", items: [
            SettingsItem(id: "tc.resetLayout", title: "Reset controls to default",
                         detail: "Button size, A button size, opacity, stick spacing and shoulder height.",
                         control: .action("Reset", destructive: true, confirm: true, run: {
                             ControllerLayoutSettings.reset()
                             UserDefaults.standard.removeObject(forKey: TouchLabSettings.aScaleKey)
                             values.refresh()
                             notes["tc.resetLayout"] = "Done."
                         })),
            SettingsItem(id: "tc.resetBindings", title: "Reset controller bindings",
                         detail: "Rebuilds the default button bindings. Use it if some buttons do nothing while the sticks still work.",
                         control: .action("Reset", destructive: true, confirm: true, run: {
                             let ok = cemu_bridge_reset_controller_bindings()
                             let count = Int(cemu_bridge_input_button_mapping_count())
                             notes["tc.resetBindings"] = ok
                                 ? "Reset. The GamePad now has \(count) button bindings."
                                 : (count < 0 ? "No GamePad is wired yet - start a game, then try again."
                                              : "Reset, but the GamePad still has no button bindings.")
                         })),
        ]))
        return groups
    }

    var controllerMappingGroups: [SettingsGroup] {
        [
            SettingsGroup(id: "map.pad", title: "Your controller", items: [
                SettingsItem(id: "map.editor", title: "", control: .banner(AnyView(ControllerMappingEditor()))),
            ]),
        ]
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
