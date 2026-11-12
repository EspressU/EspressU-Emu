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
    var actions: [Action] = [Action(title: loc("OK"), cancel: true)]
}

extension EspressUSettingsView {
    /// (the figure manager and the licences).
    func subpageGroups(_ page: LegacySettingsPage) -> [SettingsGroup]? {
        switch page {
        case .emulatedClock: return emulatedClockGroups
        case .figures: return figuresGroups
        case .overlay: return overlayGroups
        case .linkedFolders: return linkedFoldersGroups
        case .graphicPacks: return graphicPacksGroups
        case .files: return filesGroups
        case .backup: return backupGroups
        case .account: return accountGroups
        case .diagnostics: return diagnosticsGroups
        case .about: return aboutGroups
        case .controllerMapping: return controllerMappingGroups
        case .licences: return licencesGroups
        case .figureManager: return nil
        }
    }

    func subpageOpened(_ page: LegacySettingsPage) {
        switch page {
        case .account: cemu_bridge_accounts_refresh()
        default: break
        }
        values.refresh()
    }

    private var graphicPacksGroups: [SettingsGroup] {
        let packs = UserGraphicPacks.list()
        let busy = UserGraphicPacks.isBusy
        var items: [SettingsItem] = []
        if packs.isEmpty {
            items.append(SettingsItem(id: "gp.none", title: "No mods imported", detail: "Mods are folders with a rules.txt. Import your own below.", control: .info("", nil)))
        }
        for pack in packs {
            items.append(SettingsItem(id: "gp.\(pack.path)", title: pack.name,
                                      detail: pack.universal ? "All games" : (pack.titleIds.isEmpty ? nil : "\(pack.titleIds.count) game(s)"),
                                      control: .toggle(values.custom(get: { pack.enabled }, set: { on in
                                          UserGraphicPacks.setEnabled(pack, on)
                                          values.refresh()
                                      })),
                                      enabled: !busy))
            items.append(SettingsItem(id: "gp.del.\(pack.path)", title: "Delete \(pack.name)",
                                      control: .action("Delete", destructive: true, confirm: true, run: {
                                          UserGraphicPacks.delete(pack)
                                          values.refresh()
                                      }),
                                      enabled: !busy))
        }
        let add = [
            SettingsItem(id: "gp.import", title: "Import a mod folder",
                         detail: busy ? "Close the running game first." : "Pick a folder that contains rules.txt. EspressU includes no mods.",
                         control: .action("Choose", destructive: false, confirm: false, run: {
                             DocumentImport.present(contentTypes: [.folder]) { result in
                                 switch result {
                                 case .success(let urls):
                                     guard let url = urls.first else { return }
                                     do { try UserGraphicPacks.importPack(from: url); notes["gp.import"] = "Imported." }
                                     catch { pageAlert = SettingsAlert(title: "Couldn't import", message: error.localizedDescription) }
                                 case .failure(let error):
                                     pageAlert = SettingsAlert(title: "Couldn't import", message: error.localizedDescription)
                                 }
                                 values.refresh()
                             }
                         }),
                         enabled: !busy),
        ]
        return [SettingsGroup(id: "gp.list", title: "Your mods", items: items), SettingsGroup(id: "gp.add", title: "Add", items: add)]
    }

    private var emulatedClockGroups: [SettingsGroup] {
        let current = TimebaseScale.current
        var items = [
            SettingsItem(id: "clock.speed", title: loc("Speed"), detail: current.summary,
                         control: .choice(TimebaseScale.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) },
                                          values.custom(get: { String(TimebaseScale.current.rawValue) }, set: { raw in
                                              guard let value = Int(raw), let scale = TimebaseScale(rawValue: value) else { return }
                                              TimebaseScale.apply(scale)
                                          }))),
        ]
        if TimebaseScale.hasExplicitChoice {
            items.append(SettingsItem(id: "clock.auto", title: loc("Let EspressU choose again"),
                                      detail: loc("Slows the clock by itself when a game doesn't start drawing."),
                                      control: .action(loc("Reset"), destructive: false, confirm: false, run: {
                                          TimebaseScale.clearChoice()
                                          values.refresh()
                                      })))
        } else {
            items.append(SettingsItem(id: "clock.automatic", title: loc("Automatic"),
                                      detail: loc("EspressU slows the clock itself if a game doesn't start drawing. Picking a speed turns that off."),
                                      control: .info(loc("On"), nil)))
        }
        return [SettingsGroup(id: "clock", title: "", items: items)]
    }

    private var figuresGroups: [SettingsGroup] {
        [
            SettingsGroup(id: "figures", title: loc("Portals"), items: [
                SettingsItem(id: "fig.skylanders", title: loc("Skylanders Portal"), detail: loc("Applies the next time you start a game."),
                             control: .toggle(values.bool(EmulatedDevicesSettings.skylanderPortalKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_skylander_portal($0)
                             })),
                SettingsItem(id: "fig.infinity", title: loc("Disney Infinity Base"), detail: loc("Applies the next time you start a game."),
                             control: .toggle(values.bool(EmulatedDevicesSettings.infinityBaseKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_infinity_base($0)
                             })),
                SettingsItem(id: "fig.dimensions", title: loc("LEGO Dimensions Toypad"), detail: loc("Applies the next time you start a game."),
                             control: .toggle(values.bool(EmulatedDevicesSettings.dimensionsToypadKey, EmulatedDevicesSettings.defaultEnabled) {
                                 cemu_bridge_set_emulate_dimensions_toypad($0)
                             })),
            ]),
            SettingsGroup(id: "figures.manage", title: loc("Figures"), items: [
                SettingsItem(id: "fig.manage", title: loc("Manage figures"),
                             detail: loc("Load a figure dump you own, create a new one or clear a slot. No figures are included."),
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
            options.append(SettingsOption(id: String(current), title: loc("Custom")))
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
            SettingsItem(id: "ov.position", title: loc("Performance overlay"),
                         detail: loc("Shows FPS, CPU and memory on screen. Pick a corner to turn it on."),
                         control: .choice(positions, values.intChoice(OverlaySettings.positionKey, OverlaySettings.defaultPosition.rawValue) {
                             cemu_bridge_set_overlay_position(Int32($0))
                         })),
        ]
        if overlayPosition != .disabled {
            let colour = defaults.object(forKey: OverlaySettings.textColorKey) as? Int ?? OverlaySettings.defaultTextColor
            overlay += [
                SettingsItem(id: "ov.colour", title: loc("Text colour"),
                             control: .choice(colourOptions(current: colour),
                                              values.intChoice(OverlaySettings.textColorKey, OverlaySettings.defaultTextColor) {
                                                  cemu_bridge_set_overlay_text_color(UInt32(truncatingIfNeeded: $0))
                                              })),
                SettingsItem(id: "ov.scale", title: loc("Text size"),
                             control: .slider(values.intSlider(OverlaySettings.textScaleKey, OverlaySettings.defaultTextScale) {
                                 cemu_bridge_set_overlay_text_scale(Int32($0))
                             }, 50...200, step: 25, label: { "\(Int($0))%" })),
                overlayToggle("ov.fps", loc("Show FPS"), OverlaySettings.fpsKey, OverlaySettings.defaultFps) { cemu_bridge_set_overlay_fps($0) },
                overlayToggle("ov.draw", loc("Show draw calls"), OverlaySettings.drawcallsKey, OverlaySettings.defaultDrawcalls) { cemu_bridge_set_overlay_drawcalls($0) },
                overlayToggle("ov.cpu", loc("Show CPU usage"), OverlaySettings.cpuUsageKey, OverlaySettings.defaultCpuUsage) { cemu_bridge_set_overlay_cpu_usage($0) },
                overlayToggle("ov.cpuCore", loc("Show CPU usage per core"), OverlaySettings.cpuPerCoreUsageKey, OverlaySettings.defaultCpuPerCoreUsage) { cemu_bridge_set_overlay_cpu_per_core_usage($0) },
                overlayToggle("ov.ram", loc("Show RAM usage"), OverlaySettings.ramUsageKey, OverlaySettings.defaultRamUsage) { cemu_bridge_set_overlay_ram_usage($0) },
                overlayToggle("ov.vram", loc("Show VRAM usage"), OverlaySettings.vramUsageKey, OverlaySettings.defaultVramUsage) { cemu_bridge_set_overlay_vram_usage($0) },
                overlayToggle("ov.debug", loc("Show debug info"), OverlaySettings.debugKey, OverlaySettings.defaultDebug) { cemu_bridge_set_overlay_debug($0) },
            ]
        }

        var notices = [
            SettingsItem(id: "nt.position", title: "Notifications",
                         detail: loc("Short messages during a game. Pick a corner, or Off."),
                         control: .choice(positions, values.intChoice(NotificationSettings.positionKey, NotificationSettings.defaultPosition.rawValue) {
                             cemu_bridge_set_notification_position(Int32($0))
                         })),
        ]
        if noticePosition != .disabled {
            let colour = defaults.object(forKey: NotificationSettings.textColorKey) as? Int ?? NotificationSettings.defaultTextColor
            notices += [
                SettingsItem(id: "nt.colour", title: loc("Text colour"),
                             control: .choice(colourOptions(current: colour),
                                              values.intChoice(NotificationSettings.textColorKey, NotificationSettings.defaultTextColor) {
                                                  cemu_bridge_set_notification_text_color(UInt32(truncatingIfNeeded: $0))
                                              })),
                SettingsItem(id: "nt.scale", title: loc("Text size"),
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
            SettingsItem(id: "ov.fade", title: loc("Fade out in-game buttons"),
                         detail: loc("Hides the top bar buttons, HOME included, after a few seconds while a game runs. Tap or swipe down at the top edge to bring them back. Off keeps them always visible."),
                         control: .toggle(values.bool(TopBarAutoHide.enabledKey, TopBarAutoHide.defaultEnabled))),
        ]
        return [
            SettingsGroup(id: "overlay", title: loc("Performance overlay"), items: overlay),
            SettingsGroup(id: "notices", title: "Notifications", items: notices),
            SettingsGroup(id: "buttons", title: loc("In-game buttons"), items: buttons),
        ]
    }

    private func overlayToggle(_ id: String, _ title: String, _ key: String, _ fallback: Bool,
                               apply: @escaping (Bool) -> Void) -> SettingsItem {
        SettingsItem(id: id, title: title, control: .toggle(values.bool(key, fallback, apply)))
    }

    private func creditItem(_ id: String, _ title: String, _ detail: String, _ link: String) -> SettingsItem {
        SettingsItem(id: id, title: title, detail: detail,
                     control: .action(loc("Open"), destructive: false, confirm: false, run: {
                         if let url = URL(string: link) { UIApplication.shared.open(url) }
                     }))
    }

    private var linkedFoldersGroups: [SettingsGroup] {
        let library = ExternalLibrary.shared
        var linked: [SettingsItem] = []
        for location in library.locations {
            let connected = library.availability[location.id] ?? true
            let count = location.games.count
            let status = connected ? (count == 1 ? loc("Connected, 1 game") : locf("Connected, %@ games", count)) : ExternalLibrary.unavailableMessage
            linked.append(SettingsItem(id: "link.\(location.id)", title: location.name, detail: status,
                                       control: .action(loc("Unlink"), destructive: true, confirm: true, run: {
                                           Task { await gameManager.unlinkLocation(location.id); values.refresh() }
                                       })))
            if !location.hidden.isEmpty {
                let hidden = location.hidden.count
                linked.append(SettingsItem(id: "link.hidden.\(location.id)",
                                           title: locn(hidden, "Show %@ removed game again", "Show %@ removed games again"),
                                           detail: location.name,
                                           control: .action(loc("Show"), destructive: false, confirm: false, run: {
                                               Task { await gameManager.restoreHiddenGames(in: location.id); values.refresh() }
                                           })))
            }
        }
        let add = [
            SettingsItem(id: "link.folder", title: loc("Link a folder"), detail: loc("A drive, SD card or iCloud folder."),
                         control: .action(loc("Link"), destructive: false, confirm: false, run: { linkLocation([.folder]) })),
            SettingsItem(id: "link.file", title: loc("Link a game file"), detail: loc("Play one file right where it is."),
                         control: .action(loc("Link"), destructive: false, confirm: false, run: { linkLocation([.item]) })),
            SettingsItem(id: "link.about", title: loc("Nothing is copied"),
                         detail: loc("Linked games stay where they are. EspressU never copies, moves or deletes them. Unlinking keeps your saves and options."),
                         control: .info("", nil)),
        ]
        var groups: [SettingsGroup] = []
        if !linked.isEmpty { groups.append(SettingsGroup(id: "linked", title: loc("Linked"), items: linked)) }
        groups.append(SettingsGroup(id: "link.add", title: loc("Add"), items: add))
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
                        pageAlert = SettingsAlert(title: loc("Couldn't link that"), message: error.localizedDescription)
                    }
                    values.refresh()
                }
            case .failure(let error):
                pageAlert = SettingsAlert(title: loc("Couldn't link that"), message: error.localizedDescription)
            }
        }
    }

    private var filesGroups: [SettingsGroup] {
        let path = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path
            ?? loc("Could not resolve a Documents folder for this install.")
        return [SettingsGroup(id: "files", title: "", items: [
            SettingsItem(id: "files.where", title: loc("Files › On My iPad › EspressU"),
                         detail: loc("Games, saves and shader caches live here."),
                         control: .info("", nil)),
            SettingsItem(id: "files.path", title: loc("Full path"), detail: path,
                         control: .action(loc("Copy"), destructive: false, confirm: false, run: {
                             UIPasteboard.general.string = path
                             notes["files.path"] = "Copied."
                         })),
        ])]
    }

    private var backupGroups: [SettingsGroup] {
        [SettingsGroup(id: "backup", title: "", items: [
            SettingsItem(id: "backup.export", title: loc("Export all settings"),
                         detail: loc("Every setting and every game's own options, in one file. Never your library, keys, accounts or theme."),
                         control: .action(loc("Export"), destructive: false, confirm: false, run: exportSettings)),
            SettingsItem(id: "backup.import", title: loc("Import settings"),
                         detail: loc("Replaces all settings with the file's. EspressU asks first."),
                         control: .action(loc("Choose"), destructive: false, confirm: false, run: chooseSettingsFile)),
        ])]
    }

    private func exportSettings() {
        do {
            let url = try SettingsBackup.exportFile()
            DocumentImport.presentExport([url]) { result in
                switch result {
                case .success(let urls):
                    if let saved = urls.first { notes["backup.export"] = locf("Saved %@.", saved.lastPathComponent) }
                case .failure(let error):
                    notes["backup.export"] = error.localizedDescription
                }
            }
        } catch {
            notes["backup.export"] = locf("Couldn't write the settings file. %@", error.localizedDescription)
        }
    }

    private func chooseSettingsFile() {
        DocumentImport.present(contentTypes: [.json]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                do {
                    let preview = try SettingsBackup.preview(of: url)
                    let games = preview.gameCount == 1 ? loc("1 game's own options") : locf("%@ games' own options", preview.gameCount)
                    var text = locf("This file has %@ settings and %@. Importing replaces all of your settings and every game's own options. Your library and profiles are not touched.", preview.settingCount, games)
                    if preview.skippedCount > 0 { text += locf(" %@ entries can't be used and are skipped.", preview.skippedCount) }
                    pageAlert = SettingsAlert(title: loc("Replace your settings?"), message: text, actions: [
                        .init(title: loc("Import and replace"), destructive: true, run: {
                            SettingsBackup.apply(preview)
                            notes["backup.import"] = loc("Settings imported. Some changes apply the next time you start a game.")
                            values.refresh()
                        }),
                        .init(title: loc("Cancel"), cancel: true),
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

        let accountDetail: String? = locked ? loc("Profiles can't be changed while a game is running.") : nil
        let accountItems = [
            SettingsItem(id: "acc.active", title: loc("Active profile"), detail: accountDetail,
                         control: .choice(accounts.map { SettingsOption(id: String($0.persistentId), title: $0.displayNameWithId) },
                                          values.custom(get: { String(cemu_bridge_active_account_persistent_id()) }, set: { raw in
                                              if let id = UInt32(raw) { cemu_bridge_set_active_account_persistent_id(id) }
                                          })),
                         enabled: !locked && !accounts.isEmpty),
            SettingsItem(id: "acc.create", title: loc("Create profile"), detail: nil,
                         control: .action(loc("Create"), destructive: false, confirm: false, run: { accountSheets.showingCreateAccount = true }),
                         enabled: !locked && cemu_bridge_accounts_has_free_slot()),
            SettingsItem(id: "acc.delete", title: locf("Delete %@", active?.displayName ?? loc("profile")), detail: nil,
                         control: .action(loc("Delete"), destructive: true, confirm: true, run: {
                             if let active, !cemu_bridge_account_delete(active.persistentId) {
                                 pageAlert = SettingsAlert(title: loc("Couldn't delete"), message: loc("Couldn't delete that profile."))
                             }
                             cemu_bridge_accounts_refresh()
                             values.refresh()
                         }),
                         enabled: !locked && accounts.count > 1 && active != nil),
        ]

        return [
            SettingsGroup(id: "account", title: loc("Profile"), items: accountItems),
        ]
    }

    @MainActor private var deviceReport: String {
        String(cString: cemu_bridge_device_report())
            + "\n" + PlatformCapabilities.summary
            + "\nthermal: " + ThermalMonitor.shared.description
            + " · heat " + HeatStatus.band.word
    }

    private var diagnosticsGroups: [SettingsGroup] {
        let report = deviceReport
        return [
            SettingsGroup(id: "diag.device", title: loc("This device"), items: [
                SettingsItem(id: "diag.copy", title: loc("Copy device report"),
                             detail: loc("Send this with any bug report. It lists your chip, memory and app build."),
                             control: .action(loc("Copy"), destructive: false, confirm: false, run: {
                                 UIPasteboard.general.string = report
                                 notes["diag.copy"] = "Copied."
                             })),
                SettingsItem(id: "diag.share", title: "Share report",
                             detail: "Choose where the report goes. You see what it contains first.",
                             control: .action("Share", destructive: false, confirm: false, run: {
                                 pageAlert = SettingsAlert(title: "Share the report?", message: SessionReport.contentsDescription, actions: [
                                     .init(title: "Share", run: { SessionReport.share() }),
                                     .init(title: "Cancel", cancel: true),
                                 ])
                             })),
                SettingsItem(id: "diag.report", title: "", control: .banner(AnyView(
                    Text(loc(report))
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundColor(EspressUTheme.brownMid)
                        .fixedSize(horizontal: false, vertical: true)))),
            ]),
            SettingsGroup(id: "diag.tools", title: loc("Diagnostics"), items: [
                SettingsItem(id: "diag.logs", title: "Logs",
                             detail: "Every 5 seconds notes in the EspressU report what the emulator did, lines start with [Tuning]. Helps with bug reports. The report is EspressU-Report.txt in EspressU's Logs folder in the Files app.",
                             control: .toggle(values.bool(PerformanceTuning.logsKey, PerformanceTuning.logsDefault) { _ in
                                 PerformanceTuning.applyToBridge()
                             })),
                SettingsItem(id: "diag.intro", title: loc("Play the launch intro"), detail: nil,
                             control: .toggle(values.bool("espressu.showLaunchIntro", true))),
                SettingsItem(id: "diag.log", title: loc("Show launch log"),
                             detail: loc("Timestamped boot steps instead of the intro, for games that stay black."),
                             control: .toggle(values.bool(LaunchLogSettings.showKey, false))),
                SettingsItem(id: "diag.pad", title: loc("Show controls diagnostic"),
                             detail: loc("Shows over the game whether button presses reach it, and why not."),
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
                SettingsItem(id: "licences.own", title: loc("EspressU"), detail: loc("EspressU's own source is licensed under the Mozilla Public License 2.0. Each component below keeps its own copyright and licence. Tap one to read its licence."),
                             control: .info(loc("MPL-2.0"), nil)),
            ]),
            SettingsGroup(id: "licences.list", title: loc("Included software"), items: rows),
        ]
    }

    private var aboutGroups: [SettingsGroup] {
        let info = Bundle.main.infoDictionary ?? [:]
        let version = "\(info["CFBundleShortVersionString"] as? String ?? "0.0") (\(info["CFBundleVersion"] as? String ?? "0"))"
        return [
            SettingsGroup(id: "about", title: "", items: [
                SettingsItem(id: "about.version", title: loc("Version"), detail: loc("EspressU is built on Cemu."), control: .info(version, nil)),
                SettingsItem(id: "about.by", title: loc("Created by"), control: .info(loc("Amar"), nil)),
                // The owner sets this to the public repository of the Store build (MPL-2.0 source availability).
                SettingsItem(id: "about.github", title: loc("Source code on GitHub"), detail: nil,
                             control: .action(loc("Open"), destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://github.com/EspressU/EspressU-Emu") { UIApplication.shared.open(url) }
                             })),
                SettingsItem(id: "about.discord", title: loc("Join the Discord"), detail: nil,
                             control: .action(loc("Open"), destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://discord.gg/espressu") { UIApplication.shared.open(url) }
                             })),
                SettingsItem(id: "about.licences", title: loc("Open-source licences"), control: .link("", .licences)),
            ]),
            SettingsGroup(id: "about.notes", title: loc("Notes"), items: [
                SettingsItem(id: "about.games", title: loc("Your games"),
                             detail: loc("EspressU includes no games and no console files. It runs games you own, in a decrypted format, and does not decrypt anything. You are responsible for where your files come from."),
                             control: .info("", nil)),
                SettingsItem(id: "about.source", title: loc("Source code"),
                             detail: loc("Files that come from Cemu and MuffinEMU are available under the Mozilla Public License 2.0. The source code of this build is available at the address above."),
                             control: .info("", nil)),
                SettingsItem(id: "about.credit", title: loc("Touch controls"),
                             detail: loc("The Crema touch controls are by EspressU and are used under the Crema Licence."),
                             control: .info("", nil)),
                SettingsItem(id: "about.trademark", title: loc("Trademarks"),
                             detail: loc("Wii U is a trademark of Nintendo. EspressU is not affiliated with Nintendo."),
                             control: .info("", nil)),
            ]),
            SettingsGroup(id: "about.reset", title: loc("Reset"), items: [
                SettingsItem(id: "about.resetAll", title: loc("Reset settings and per-game options"),
                             detail: loc("Like Reset all settings, and every game's own options go back too. The welcome guide opens again."),
                             control: .action(loc("Reset"), destructive: true, confirm: true, run: {
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
