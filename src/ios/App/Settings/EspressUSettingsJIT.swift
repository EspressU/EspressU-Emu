// SPDX-License-Identifier: MPL-2.0

import SwiftUI

extension EspressUSettingsView {
    var jitGroups: [SettingsGroup] {
        let coordinator = StikJITCoordinator.shared
        let busy = coordinator.job == .prepare || coordinator.job == .resetDDI
        var groups = [
            SettingsGroup(id: "jit.status", title: "", items: [
                SettingsItem(id: "jit.enabled", title: "", control: .modeSwitch(
                    values.custom(get: { JITSettings.enabled }, set: { JITSettings.setEnabled($0) }))),
            ]),
        ]
        let pairingName = StikJITSetup.pairingFileName
        var pairingItems = [
            SettingsItem(id: "jit.pairingFile", title: loc("Pairing file"), detail: nil,
                         control: .info(pairingName ?? loc("Missing"),
                                        pairingName == nil ? EspressUTheme.cautionText : nil)),
        ]
        switch pairing.state {
        case .waiting, .pin:
            pairingItems.append(SettingsItem(id: "jit.pairingCard", title: "",
                                             control: .banner(AnyView(JITPairingCard()))))
            pairingItems.append(SettingsItem(id: "jit.pairStop", title: loc("Stop pairing"),
                                             control: .action(loc("Stop"), destructive: false, confirm: false,
                                                              run: { pairing.cancel() })))
        case .idle, .paired, .failed:
            guard DevicePairing.isSupported else {
                pairingItems.append(SettingsItem(id: "jit.pairUnsupported", title: loc("Pair this device"),
                                                 detail: locf("Built-in pairing needs iOS %@ or newer. Import a pairing file instead.", DevicePairing.minimumIOSVersion),
                                                 control: .info(loc("Not available"), EspressUTheme.cautionText)))
                break
            }
            var detail = loc("No computer needed: EspressU shows up in Settings › Privacy & Security › Developer Mode.")
            if case .paired(let device) = pairing.state {
                detail = device.map { locf("Paired with %@.", $0) } ?? loc("Paired.")
            }
            if case .failed(let reason) = pairing.state { detail = reason }
            pairingItems.append(SettingsItem(id: "jit.pair", title: loc("Pair this device"), detail: detail,
                                             control: .action(pairingName == nil ? loc("Pair") : loc("Pair again"),
                                                              destructive: false, confirm: false,
                                                              run: { pairing.start() }),
                                             enabled: !busy))
        }
        pairingItems.append(SettingsItem(id: "jit.import", title: loc("Import pairing file"),
                                         detail: loc("From iLoader, idevice_pair or another tool."),
                                         control: .action(loc("Import"), destructive: false, confirm: false,
                                                          run: { importingPairing = true }),
                                         enabled: !busy && !pairing.isActive))
        groups.append(SettingsGroup(id: "jit.pairing", title: loc("Pairing"), items: pairingItems))

        return groups
    }

    func importPairingFile(_ result: Result<URL, Error>) {
        switch result {
        case .success(let url):
            do {
                try StikJITSetup.importPairingFile(from: url)
                StikJITCoordinator.shared.pairingFileChanged()
                StikJITCoordinator.shared.autoEnableIfPossible()
                notes["jit.import"] = loc("Imported. JIT switches on by itself when LocalDevVPN is connected.")
            } catch {
                notes["jit.import"] = locf("Couldn't import it: %@", error.localizedDescription)
            }
        case .failure(let error):
            notes["jit.import"] = locf("Couldn't import it: %@", error.localizedDescription)
        }
        values.refresh()
    }
}
