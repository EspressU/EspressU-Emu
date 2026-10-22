// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct SettingsAlert: Identifiable {
    struct Action: Identifiable {
        let id = UUID()
        let title: String
        var destructive = false
        var cancel = false
        var run: () -> Void = {}
    }

    let id = UUID()
    let title: String
    let message: String
    var actions: [Action] = [Action(title: "OK", cancel: true)]
}

extension EspressUSettingsView {
    /// (the figure manager and the licences).
    func subpageGroups(_ page: LegacySettingsPage) -> [SettingsGroup]? {
        switch page {
        case .emulatedClock: return emulatedClockGroups
        case .figures: return figuresGroups
        case .overlay: return overlayGroups
        case .linkedFolders: return linkedFoldersGroups
        case .keys: return keysGroups
        case .files: return filesGroups
        case .backup: return backupGroups
        case .account: return accountGroups
        case .diagnostics: return diagnosticsGroups
        case .about: return aboutGroups
        case .graphicPacks: return graphicPacksGroups
        case .graphicPackGame: return graphicPackGameGroups
        case .graphicPack: return graphicPackGroups
        case .controllerMapping: return controllerMappingGroups
        case .networkService: return customServerGroups
        case .licences: return licencesGroups
        case .figureManager: return nil
        }
    }

    func subpageOpened(_ page: LegacySettingsPage) {
        switch page {
        case .account: cemu_bridge_accounts_refresh()
        case .graphicPacks: GraphicPackStore.shared.onOpen()
        case .graphicPack: loadPackDetails()
        default: break
        }
        values.refresh()
    }

    private var emulatedClockGroups: [SettingsGroup] {
        let current = TimebaseScale.current
        var items = [
            SettingsItem(id: "clock.speed", title: "Speed", detail: current.summary,
                         control: .choice(TimebaseScale.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) },
                                          values.custom(get: { String(TimebaseScale.current.rawValue) }, set: { raw in
                                              guard let value = Int(raw), let scale = TimebaseScale(rawValue: value) else { return }
                                              TimebaseScale.apply(scale)
                                          }))),
        ]
        if TimebaseScale.hasExplicitChoice {
            items.append(SettingsItem(id: "clock.auto", title: "Let EspressU choose again",
                                      detail: "Slows the clock by itself when a game doesn't start drawing.",
                                      control: .action("Reset", destructive: false, confirm: false, run: {
                                          TimebaseScale.clearChoice()
                                          values.refresh()
                                      })))
        } else {
            items.append(SettingsItem(id: "clock.automatic", title: "Automatic",
                                      detail: "EspressU slows the clock itself if a game doesn't start drawing. Picking a speed turns that off.",
                                      control: .info("On", nil)))
        }
        return [SettingsGroup(id: "clock", title: "", items: items)]
    }

    private var figuresGroups: [SettingsGroup] {
        [
            SettingsGroup(id: "figures", title: "Portals", items: [
                SettingsItem(id: "fig.skylanders", title: "Skylanders Portal", detail: "Applies the next time you start a game.",
                             control: .toggle(values.bool(EmulatedDevicesSettings.skylanderPortalKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_skylander_portal($0)
                             })),
                SettingsItem(id: "fig.infinity", title: "Disney Infinity Base", detail: "Applies the next time you start a game.",
                             control: .toggle(values.bool(EmulatedDevicesSettings.infinityBaseKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_infinity_base($0)
                             })),
                SettingsItem(id: "fig.dimensions", title: "LEGO Dimensions Toypad", detail: "Applies the next time you start a game.",
                             control: .toggle(values.bool(EmulatedDevicesSettings.dimensionsToypadKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_dimensions_toypad($0)
                             })),
            ]),
            SettingsGroup(id: "figures.manage", title: "Figures", items: [
                SettingsItem(id: "fig.manage", title: "Manage figures",
                             detail: "Load a figure dump you own, create a new one or clear a slot. No figures are included.",
                             control: .link("", .figureManager)),
            ]),
        ]
    }

    private static let textColours: [(String, Int)] = [
        ("White", 0xFFFFFFFF), ("Yellow", 0xFFFFD60A), ("Green", 0xFF34C759), ("Cyan", 0xFF5AC8FA),
        ("Orange", 0xFFFF9F0A), ("Pink", 0xFFFF6482), ("Red", 0xFFFF453A),
    ]

    private func colourOptions(current: Int) -> [SettingsOption] {
        var options = Self.textColours.map { SettingsOption(id: String($0.1), title: $0.0) }
        if !Self.textColours.contains(where: { $0.1 == current }) {
            options.append(SettingsOption(id: String(current), title: "Custom"))
        }
        return options
    }

    private var overlayGroups: [SettingsGroup] {
        let defaults = UserDefaults.standard
        let overlayPosition = ScreenPosition(rawValue: defaults.object(forKey: OverlaySettings.positionKey) as? Int
                                             ?? OverlaySettings.defaultPosition.rawValue) ?? .disabled
        let noticePosition = ScreenPosition(rawValue: defaults.object(forKey: NotificationSettings.positionKey) as? Int
                                            ?? NotificationSettings.defaultPosition.rawValue) ?? .disabled
        let positions = ScreenPosition.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }

        var overlay = [
            SettingsItem(id: "ov.position", title: "Performance overlay",
                         detail: "Shows FPS, CPU and memory on screen. Pick a corner to turn it on.",
                         control: .choice(positions, values.intChoice(OverlaySettings.positionKey, OverlaySettings.defaultPosition.rawValue) {
                             cemu_bridge_set_overlay_position(Int32($0))
                         })),
        ]
        if overlayPosition != .disabled {
            let colour = defaults.object(forKey: OverlaySettings.textColorKey) as? Int ?? OverlaySettings.defaultTextColor
            overlay += [
                SettingsItem(id: "ov.colour", title: "Text colour",
                             control: .choice(colourOptions(current: colour),
                                              values.intChoice(OverlaySettings.textColorKey, OverlaySettings.defaultTextColor) {
                                                  cemu_bridge_set_overlay_text_color(UInt32(truncatingIfNeeded: $0))
                                              })),
                SettingsItem(id: "ov.scale", title: "Text size",
                             control: .slider(values.intSlider(OverlaySettings.textScaleKey, OverlaySettings.defaultTextScale) {
                                 cemu_bridge_set_overlay_text_scale(Int32($0))
                             }, 50...200, step: 25, label: { "\(Int($0))%" })),
                overlayToggle("ov.fps", "Show FPS", OverlaySettings.fpsKey, OverlaySettings.defaultFps) { cemu_bridge_set_overlay_fps($0) },
                overlayToggle("ov.draw", "Show draw calls", OverlaySettings.drawcallsKey, OverlaySettings.defaultDrawcalls) { cemu_bridge_set_overlay_drawcalls($0) },
                overlayToggle("ov.cpu", "Show CPU usage", OverlaySettings.cpuUsageKey, OverlaySettings.defaultCpuUsage) { cemu_bridge_set_overlay_cpu_usage($0) },
                overlayToggle("ov.cpuCore", "Show CPU usage per core", OverlaySettings.cpuPerCoreUsageKey, OverlaySettings.defaultCpuPerCoreUsage) { cemu_bridge_set_overlay_cpu_per_core_usage($0) },
                overlayToggle("ov.ram", "Show RAM usage", OverlaySettings.ramUsageKey, OverlaySettings.defaultRamUsage) { cemu_bridge_set_overlay_ram_usage($0) },
                overlayToggle("ov.vram", "Show VRAM usage", OverlaySettings.vramUsageKey, OverlaySettings.defaultVramUsage) { cemu_bridge_set_overlay_vram_usage($0) },
                overlayToggle("ov.debug", "Show debug info", OverlaySettings.debugKey, OverlaySettings.defaultDebug) { cemu_bridge_set_overlay_debug($0) },
            ]
        }

        var notices = [
            SettingsItem(id: "nt.position", title: "Notifications",
                         detail: "Short messages during a game. Pick a corner, or Off.",
                         control: .choice(positions, values.intChoice(NotificationSettings.positionKey, NotificationSettings.defaultPosition.rawValue) {
                             cemu_bridge_set_notification_position(Int32($0))
                         })),
        ]
        if noticePosition != .disabled {
            let colour = defaults.object(forKey: NotificationSettings.textColorKey) as? Int ?? NotificationSettings.defaultTextColor
            notices += [
                SettingsItem(id: "nt.colour", title: "Text colour",
                             control: .choice(colourOptions(current: colour),
                                              values.intChoice(NotificationSettings.textColorKey, NotificationSettings.defaultTextColor) {
                                                  cemu_bridge_set_notification_text_color(UInt32(truncatingIfNeeded: $0))
                                              })),
                SettingsItem(id: "nt.scale", title: "Text size",
                             control: .slider(values.intSlider(NotificationSettings.textScaleKey, NotificationSettings.defaultTextScale) {
                                 cemu_bridge_set_notification_text_scale(Int32($0))
                             }, 50...200, step: 25, label: { "\(Int($0))%" })),
                overlayToggle("nt.profiles", "Controller profiles", NotificationSettings.controllerProfilesKey, NotificationSettings.defaultControllerProfiles) { cemu_bridge_set_notification_controller_profiles($0) },
                overlayToggle("nt.battery", "Low controller battery", NotificationSettings.controllerBatteryKey, NotificationSettings.defaultControllerBattery) { cemu_bridge_set_notification_controller_battery($0) },
                overlayToggle("nt.shaders", "Shader compiling", NotificationSettings.shaderCompilingKey, NotificationSettings.defaultShaderCompiling) { cemu_bridge_set_notification_shader_compiling($0) },
                overlayToggle("nt.friends", "Friends", NotificationSettings.friendsKey, NotificationSettings.defaultFriends) { cemu_bridge_set_notification_friends($0) },
            ]
        }
        let buttons = [
            SettingsItem(id: "ov.fade", title: "Fade out in-game buttons",
                         detail: "Hides the top bar buttons, HOME included, after a few seconds while a game runs. Tap or swipe down at the top edge to bring them back. Off keeps them always visible.",
                         control: .toggle(values.bool(TopBarAutoHide.enabledKey, TopBarAutoHide.defaultEnabled))),
        ]
        return [
            SettingsGroup(id: "overlay", title: "Performance overlay", items: overlay),
            SettingsGroup(id: "notices", title: "Notifications", items: notices),
            SettingsGroup(id: "buttons", title: "In-game buttons", items: buttons),
        ]
    }

    private func overlayToggle(_ id: String, _ title: String, _ key: String, _ fallback: Bool,
                               apply: @escaping (Bool) -> Void) -> SettingsItem {
        SettingsItem(id: id, title: title, control: .toggle(values.bool(key, fallback, apply)))
    }

    private func creditItem(_ id: String, _ title: String, _ detail: String, _ link: String) -> SettingsItem {
        SettingsItem(id: id, title: title, detail: detail,
                     control: .action("Open", destructive: false, confirm: false, run: {
                         if let url = URL(string: link) { UIApplication.shared.open(url) }
                     }))
    }

    private var linkedFoldersGroups: [SettingsGroup] {
        let library = ExternalLibrary.shared
        var linked: [SettingsItem] = []
        for location in library.locations {
            let connected = library.availability[location.id] ?? true
            let count = location.games.count
            let status = connected ? (count == 1 ? "Connected, 1 game" : "Connected, \(count) games") : ExternalLibrary.unavailableMessage
            linked.append(SettingsItem(id: "link.\(location.id)", title: location.name, detail: status,
                                       control: .action("Unlink", destructive: true, confirm: true, run: {
                                           Task { await gameManager.unlinkLocation(location.id); values.refresh() }
                                       })))
            if !location.hidden.isEmpty {
                let hidden = location.hidden.count
                linked.append(SettingsItem(id: "link.hidden.\(location.id)",
                                           title: "Show \(hidden) removed game\(hidden == 1 ? "" : "s") again",
                                           detail: location.name,
                                           control: .action("Show", destructive: false, confirm: false, run: {
                                               Task { await gameManager.restoreHiddenGames(in: location.id); values.refresh() }
                                           })))
            }
        }
        let add = [
            SettingsItem(id: "link.folder", title: "Link a folder", detail: "A drive, SD card or iCloud folder.",
                         control: .action("Link", destructive: false, confirm: false, run: { linkLocation([.folder]) })),
            SettingsItem(id: "link.file", title: "Link a game file", detail: "Play one file right where it is.",
                         control: .action("Link", destructive: false, confirm: false, run: { linkLocation([.item]) })),
            SettingsItem(id: "link.about", title: "Nothing is copied",
                         detail: "Linked games stay where they are. EspressU never copies, moves or deletes them. Unlinking keeps your saves and options.",
                         control: .info("", nil)),
        ]
        var groups: [SettingsGroup] = []
        if !linked.isEmpty { groups.append(SettingsGroup(id: "linked", title: "Linked", items: linked)) }
        groups.append(SettingsGroup(id: "link.add", title: "Add", items: add))
        return groups
    }

    private func linkLocation(_ types: [UTType]) {
        DocumentImport.present(contentTypes: types) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                Task {
                    do {
                        try await gameManager.linkLocation(url)
                        notes["link.folder"] = nil
                    } catch {
                        pageAlert = SettingsAlert(title: "Couldn't link that", message: error.localizedDescription)
                    }
                    values.refresh()
                }
            case .failure(let error):
                pageAlert = SettingsAlert(title: "Couldn't link that", message: error.localizedDescription)
            }
        }
    }

    private var keysGroups: [SettingsGroup] {
        let exists = WiiUKeys.keysFileExists()
        var items = [
            SettingsItem(id: "keys.import", title: exists ? "Replace keys.txt" : "Import keys.txt",
                         detail: "Only for encrypted games (.wud, .wux). Keys from your own Wii U, one per line. New keys apply to the next game you start.",
                         control: .action("Choose", destructive: false, confirm: false, run: {
                             DocumentImport.present(contentTypes: [.item], asCopy: true) { result in
                                 switch result {
                                 case .success(let urls):
                                     guard let url = urls.first else { return }
                                     do {
                                         let count = try WiiUKeys.importKeys(from: url)
                                         notes["keys.import"] = "Imported \(count) keys."
                                     } catch {
                                         pageAlert = SettingsAlert(title: "Couldn't import keys", message: error.localizedDescription)
                                     }
                                 case .failure(let error):
                                     pageAlert = SettingsAlert(title: "Couldn't import keys", message: error.localizedDescription)
                                 }
                                 values.refresh()
                             }
                         })),
        ]
        if exists {
            items.append(SettingsItem(id: "keys.count", title: "Keys loaded", control: .info("\(WiiUKeys.installedKeyCount())", nil)))
            items.append(SettingsItem(id: "keys.remove", title: "Remove keys.txt",
                                      detail: "Encrypted games won't run until you import keys again.",
                                      control: .action("Remove", destructive: true, confirm: true, run: {
                                          do { try WiiUKeys.removeKeys() } catch {
                                              pageAlert = SettingsAlert(title: "Couldn't remove keys.txt", message: error.localizedDescription)
                                          }
                                          values.refresh()
                                      })))
        }
        items.append(SettingsItem(id: "keys.files", title: "Or use Files",
                                  detail: "Put keys.txt into the keys folder: Files › EspressU › keys.",
                                  control: .info("", nil)))
        return [SettingsGroup(id: "keys", title: "", items: items)]
    }

    private var filesGroups: [SettingsGroup] {
        let path = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path
            ?? "Could not resolve a Documents folder for this install."
        return [SettingsGroup(id: "files", title: "", items: [
            SettingsItem(id: "files.where", title: "Files › On My iPad › EspressU",
                         detail: "Games, saves, shader caches and keys.txt live here. Installed through LiveContainer, the folder is under LiveContainer instead.",
                         control: .info("", nil)),
            SettingsItem(id: "files.path", title: "Full path", detail: path,
                         control: .action("Copy", destructive: false, confirm: false, run: {
                             UIPasteboard.general.string = path
                             notes["files.path"] = "Copied."
                         })),
        ])]
    }

    private var backupGroups: [SettingsGroup] {
        [SettingsGroup(id: "backup", title: "", items: [
            SettingsItem(id: "backup.export", title: "Export all settings",
                         detail: "Every setting and every game's own options, in one file. Never your library, keys, accounts or theme.",
                         control: .action("Export", destructive: false, confirm: false, run: exportSettings)),
            SettingsItem(id: "backup.import", title: "Import settings",
                         detail: "Replaces all settings with the file's. EspressU asks first.",
                         control: .action("Choose", destructive: false, confirm: false, run: chooseSettingsFile)),
        ])]
    }

    private func exportSettings() {
        do {
            let url = try SettingsBackup.exportFile()
            DocumentImport.presentExport([url]) { result in
                switch result {
                case .success(let urls):
                    if let saved = urls.first { notes["backup.export"] = "Saved \(saved.lastPathComponent)." }
                case .failure(let error):
                    notes["backup.export"] = error.localizedDescription
                }
            }
        } catch {
            notes["backup.export"] = "Couldn't write the settings file. \(error.localizedDescription)"
        }
    }

    private func chooseSettingsFile() {
        DocumentImport.present(contentTypes: [.json]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    let preview = try SettingsBackup.preview(of: url)
                    let games = preview.gameCount == 1 ? "1 game's own options" : "\(preview.gameCount) games' own options"
                    var text = "This file has \(preview.settingCount) settings and \(games). Importing replaces all of your settings and every game's own options. Your library, keys and accounts are not touched."
                    if preview.skippedCount > 0 { text += " \(preview.skippedCount) entries can't be used and are skipped." }
                    pageAlert = SettingsAlert(title: "Replace your settings?", message: text, actions: [
                        .init(title: "Import and replace", destructive: true, run: {
                            SettingsBackup.apply(preview)
                            notes["backup.import"] = "Settings imported. Some changes apply the next time you start a game."
                            values.refresh()
                        }),
                        .init(title: "Cancel", cancel: true),
                    ])
                } catch {
                    notes["backup.import"] = error.localizedDescription
                }
            case .failure(let error):
                notes["backup.import"] = error.localizedDescription
            }
        }
    }

    private var accountGroups: [SettingsGroup] {
        let accounts = Account.loadAll()
        let activeId = cemu_bridge_active_account_persistent_id()
        let active = accounts.first { $0.persistentId == activeId }
        let locked = cemu_bridge_accounts_locked()

        var accountDetail: String?
        if locked {
            accountDetail = "Accounts can't be changed while a game is running."
        } else if let note = OnlineReadiness.note(accountId: activeId, service: NetworkService(cemu_bridge_network_service(activeId))) {
            accountDetail = note
        } else if let active, !active.isValidOnline {
            accountDetail = "No saved NNID/PNID login, so it can't play online yet."
        }
        let accountItems = [
            SettingsItem(id: "acc.active", title: "Active account", detail: accountDetail,
                         control: .choice(accounts.map { SettingsOption(id: String($0.persistentId), title: $0.displayNameWithId) },
                                          values.custom(get: { String(cemu_bridge_active_account_persistent_id()) }, set: { raw in
                                              if let id = UInt32(raw) { cemu_bridge_set_active_account_persistent_id(id) }
                                          })),
                         enabled: !locked && !accounts.isEmpty),
            SettingsItem(id: "acc.create", title: "Create account", detail: nil,
                         control: .action("Create", destructive: false, confirm: false, run: { accountSheets.showingCreateAccount = true }),
                         enabled: !locked && cemu_bridge_accounts_has_free_slot()),
            SettingsItem(id: "acc.import", title: "Import from Wii U",
                         detail: "account.dat or its 800000XX folder, plus otp.bin and seeprom.bin for online play.",
                         control: .action("Choose", destructive: false, confirm: false, run: { importingAccount = true }),
                         enabled: !locked),
            SettingsItem(id: "acc.delete", title: "Delete \(active?.displayName ?? "account")", detail: nil,
                         control: .action("Delete", destructive: true, confirm: true, run: {
                             if let active, !cemu_bridge_account_delete(active.persistentId) {
                                 pageAlert = SettingsAlert(title: "Couldn't delete", message: "Couldn't delete that account.")
                             }
                             cemu_bridge_accounts_refresh()
                             values.refresh()
                         }),
                         enabled: !locked && accounts.count > 1 && active != nil),
        ]

        let service = NetworkService(cemu_bridge_network_service(activeId))
        let customAvailable = cemu_bridge_custom_network_service_available()
        let services = NetworkService.allCases.filter { candidate in
            if candidate == .nintendo { return service == .nintendo }
            if candidate == .custom { return customAvailable || service == .custom }
            return true
        }
        let missing = OnlineReadiness.missingItems(accountId: activeId)
        let readiness: String
        switch service {
        case .offline: readiness = "Online play is off."
        case .custom: readiness = customAvailable ? "Ready for online play (custom server)." : "Missing: a valid network_services.xml."
        default: readiness = missing.isEmpty ? "Ready for online play." : "Missing: " + missing.joined(separator: ", ") + "."
        }
        let online = [
            SettingsItem(id: "net.service", title: "Network service", detail: readiness,
                         control: .choice(services.map { SettingsOption(id: String($0.rawValue), title: $0.string) },
                                          values.custom(get: { String(NetworkService(cemu_bridge_network_service(cemu_bridge_active_account_persistent_id())).rawValue) },
                                                        set: { raw in
                                                            guard let value = services.first(where: { String($0.rawValue) == raw }) else { return }
                                                            cemu_bridge_set_network_service(cemu_bridge_active_account_persistent_id(), value.bridgeValue)
                                                        })),
                         enabled: !locked && active != nil),
            SettingsItem(id: "net.connection", title: "", control: .banner(AnyView(DeviceConnectionStatusRow()))),
            SettingsItem(id: "net.custom", title: "Custom servers", detail: "A network_services.xml, like desktop Cemu reads.",
                         control: .link("", .networkService)),
        ]
        return [
            SettingsGroup(id: "account", title: "Account", items: accountItems),
            SettingsGroup(id: "online", title: "Online", items: online),
        ]
    }

    @MainActor private var deviceReport: String {
        String(cString: cemu_bridge_device_report())
            + "\n" + PlatformCapabilities.summary
            + "\nthermal: " + ThermalMonitor.shared.description
            + " · heat " + HeatStatus.band.word
            + (HeatStatus.temperatureCelsius.map { String(format: " (battery %.0f C)", $0) } ?? " (no sensor reading)")
    }

    private var diagnosticsGroups: [SettingsGroup] {
        let report = deviceReport
        return [
            SettingsGroup(id: "diag.device", title: "This device", items: [
                SettingsItem(id: "diag.copy", title: "Copy device report",
                             detail: "Send this with any bug report. It lists your chip, memory and app build.",
                             control: .action("Copy", destructive: false, confirm: false, run: {
                                 UIPasteboard.general.string = report
                                 notes["diag.copy"] = "Copied."
                             })),
                SettingsItem(id: "diag.report", title: "", control: .banner(AnyView(
                    Text(report)
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundColor(EspressUTheme.brownMid)
                        .fixedSize(horizontal: false, vertical: true)))),
            ]),
            SettingsGroup(id: "diag.tools", title: "Diagnostics", items: [
                SettingsItem(id: "diag.intro", title: "Play the launch intro", detail: nil,
                             control: .toggle(values.bool("espressu.showLaunchIntro", true))),
                SettingsItem(id: "diag.log", title: "Show launch log",
                             detail: "Timestamped boot steps instead of the intro, for games that stay black.",
                             control: .toggle(values.bool(LaunchLogSettings.showKey, false))),
                SettingsItem(id: "diag.pad", title: "Show controls diagnostic",
                             detail: "Shows over the game whether button presses reach it, and why not.",
                             control: .toggle(values.bool(PadDiagnostics.enabledKey, PadDiagnostics.defaultEnabled))),
            ]),
        ]
    }

    private var licencesGroups: [SettingsGroup] {
        let rows = OpenSourceLicencesView.components.map { component in
            SettingsItem(id: "licence.\(component.name)", title: component.name,
                         control: .open(component.licence, {
                             if let url = URL(string: component.licenceURL) { UIApplication.shared.open(url) }
                         }))
        }
        return [
            SettingsGroup(id: "licences.note", title: "", items: [
                SettingsItem(id: "licences.own", title: "EspressU", detail: "EspressU's own source is licensed under the Mozilla Public License 2.0. Each component below keeps its own copyright and licence. Tap one to read its licence.",
                             control: .info("MPL-2.0", nil)),
            ]),
            SettingsGroup(id: "licences.list", title: "Included software", items: rows),
        ]
    }

    private var aboutGroups: [SettingsGroup] {
        let info = Bundle.main.infoDictionary ?? [:]
        let version = "\(info["CFBundleShortVersionString"] as? String ?? "0.0") (\(info["CFBundleVersion"] as? String ?? "0"))"
        return [
            SettingsGroup(id: "about", title: "", items: [
                SettingsItem(id: "about.version", title: "Version", detail: "EspressU is built on Cemu.", control: .info(version, nil)),
                SettingsItem(id: "about.by", title: "Created by", control: .info("Amar", nil)),
                SettingsItem(id: "about.github", title: "View on GitHub", detail: nil,
                             control: .action("Open", destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://github.com/EspressU/EspressU-Emu") { UIApplication.shared.open(url) }
                             })),
                SettingsItem(id: "about.discord", title: "Join the Discord", detail: nil,
                             control: .action("Open", destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://discord.gg/espressu") { UIApplication.shared.open(url) }
                             })),
                SettingsItem(id: "about.licences", title: "Open-source licences", control: .link("", .licences)),
            ]),
            SettingsGroup(id: "about.reset", title: "Reset", items: [
                SettingsItem(id: "about.resetAll", title: "Reset settings and per-game options",
                             detail: "Like Reset all settings, and every game's own options go back too. The welcome guide opens again.",
                             control: .action("Reset", destructive: true, confirm: true, run: {
                                 SettingsDefaults.reset(includingPerGameOverrides: true)
                                 UserDefaults.standard.set(SettingsMode.advanced.rawValue, forKey: SettingsMode.storageKey)
                                 values.refresh()
                                 close()
                                 NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
                             })),
            ]),
        ]
    }
}
