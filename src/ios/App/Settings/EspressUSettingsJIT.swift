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
            SettingsItem(id: "jit.pairingFile", title: "Pairing file", detail: nil,
                         control: .info(pairingName ?? "Missing",
                                        pairingName == nil ? EspressUTheme.cautionText : nil)),
        ]
        switch pairing.state {
        case .waiting, .pin:
            pairingItems.append(SettingsItem(id: "jit.pairingCard", title: "",
                                             control: .banner(AnyView(JITPairingCard()))))
            pairingItems.append(SettingsItem(id: "jit.pairStop", title: "Stop pairing",
                                             control: .action("Stop", destructive: false, confirm: false,
                                                              run: { pairing.cancel() })))
        case .idle, .paired, .failed:
            guard DevicePairing.isSupported else {
                pairingItems.append(SettingsItem(id: "jit.pairUnsupported", title: "Pair this device",
                                                 detail: "Built-in pairing needs iOS \(DevicePairing.minimumIOSVersion) or newer. Import a pairing file instead.",
                                                 control: .info("Not available", EspressUTheme.cautionText)))
                break
            }
            var detail = "No computer needed: EspressU shows up in Settings › Privacy & Security › Developer Mode."
            if case .paired(let device) = pairing.state {
                detail = device.map { "Paired with \($0)." } ?? "Paired."
            }
            if case .failed(let reason) = pairing.state { detail = reason }
            pairingItems.append(SettingsItem(id: "jit.pair", title: "Pair this device", detail: detail,
                                             control: .action(pairingName == nil ? "Pair" : "Pair again",
                                                              destructive: false, confirm: false,
                                                              run: { pairing.start() }),
                                             enabled: !busy))
        }
        pairingItems.append(SettingsItem(id: "jit.import", title: "Import pairing file",
                                         detail: "From iLoader, idevice_pair or another tool.",
                                         control: .action("Import", destructive: false, confirm: false,
                                                          run: { importingPairing = true }),
                                         enabled: !busy && !pairing.isActive))
        groups.append(SettingsGroup(id: "jit.pairing", title: "Pairing", items: pairingItems))

        return groups
    }

    func importPairingFile(_ result: Result<URL, Error>) {
        switch result {
        case .success(let url):
            do {
                try StikJITSetup.importPairingFile(from: url)
                StikJITCoordinator.shared.pairingFileChanged()
                StikJITCoordinator.shared.autoEnableIfPossible()
                notes["jit.import"] = "Imported. JIT switches on by itself when LocalDevVPN is connected."
            } catch {
                notes["jit.import"] = "Couldn't import it: \(error.localizedDescription)"
            }
        case .failure(let error):
            notes["jit.import"] = "Couldn't import it: \(error.localizedDescription)"
        }
        values.refresh()
    }
}
