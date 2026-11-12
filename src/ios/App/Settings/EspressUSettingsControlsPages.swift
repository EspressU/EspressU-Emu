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
}
