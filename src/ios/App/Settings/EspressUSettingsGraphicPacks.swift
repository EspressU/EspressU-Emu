// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

extension EspressUSettingsView {
    private var packAPI: RendererAPI {
        RendererAPI(rawValue: UserDefaults.standard.integer(forKey: RendererAPI.storageKey)) ?? RendererAPI.defaultValue
    }

    var graphicPacksGroups: [SettingsGroup] {
        let store = GraphicPackStore.shared
        var source = [
            SettingsItem(id: "gp.installed", title: "Community packs",
                         detail: store.installed == nil ? "Made by the Cemu graphic packs project. Verified before anything is replaced." : nil,
                         control: .info(installedPacksText, nil)),
        ]
        switch store.phase {
        case .checking:
            source.append(SettingsItem(id: "gp.checking", title: "Checking for updates…", control: .info("", nil)))
        case .downloading(let fraction), .installing(let fraction):
            let installing: Bool = { if case .installing = store.phase { return true } else { return false } }()
            source.append(SettingsItem(id: "gp.progress", title: installing ? "Installing…" : "Downloading…",
                                       detail: "\(Int((fraction * 100).rounded()))%",
                                       control: .action("Cancel", destructive: true, confirm: false, run: { store.cancel() })))
        case .waitingForGameToClose:
            source.append(SettingsItem(id: "gp.waiting", title: "Downloaded",
                                       detail: "It installs when this page is opened with no game running.", control: .info("", nil)))
        case .idle:
            if store.updateAvailable, let latest = store.latest {
                source.append(SettingsItem(id: "gp.download",
                                           title: store.installed == nil ? "Download community packs" : "Update community packs",
                                           detail: store.installed == nil
                                               ? "\(latest.name), \(GraphicPackStorage.describe(latest.size))."
                                               : "\(latest.name). Your choices are kept.",
                                           control: .action(store.installed == nil ? "Download" : "Update", destructive: false, confirm: false,
                                                            run: { store.startDownload() }),
                                           enabled: !store.gameRunning))
            }
            source.append(SettingsItem(id: "gp.check", title: store.latest == nil ? "Check for community packs" : "Check for updates",
                                       detail: store.notice?.text,
                                       control: .action("Check", destructive: false, confirm: false, run: { store.checkForUpdate(force: true) })))
        }

        var games: [SettingsItem] = []
        let grouped = Dictionary(grouping: store.packs, by: \.game)
        let names = grouped.keys.sorted { a, b in
            if a == GraphicPack.allGames { return true }
            if b == GraphicPack.allGames { return false }
            return a.localizedCaseInsensitiveCompare(b) == .orderedAscending
        }
        for name in names {
            let packs = grouped[name] ?? []
            let on = packs.filter(\.enabled).count
            games.append(SettingsItem(id: "gp.game.\(name)", title: name == GraphicPack.allGames ? "Every game" : name,
                                      detail: packs.count == 1 ? "1 pack" : "\(packs.count) packs",
                                      control: .open(on > 0 ? "\(on) on" : "", {
                                          selectedPackGame = name
                                          openLegacy(.graphicPackGame)
                                      })))
        }
        if games.isEmpty {
            games.append(SettingsItem(id: "gp.none", title: store.isScanning ? "Looking for packs…" : "No graphic packs yet",
                                      detail: "Download the community packs above, or import your own.", control: .info("", nil)))
        }

        var groups = [SettingsGroup(id: "gp.source", title: "Download", items: source)]
        if store.gameRunning {
            groups.append(SettingsGroup(id: "gp.running", title: "", items: [
                SettingsItem(id: "gp.runningNote", title: "A game is running",
                             detail: "You can browse packs, but change them only after you close it.", control: .info("", nil)),
            ]))
        }
        groups.append(SettingsGroup(id: "gp.games", title: "Games", items: games))
        groups.append(SettingsGroup(id: "gp.own", title: "Your own packs", items: [
            SettingsItem(id: "gp.import", title: "Import a pack folder or zip",
                         detail: notes["gp.importResult"] ?? "A folder with a rules.txt, a folder of packs, or a zip of either.",
                         control: .action("Choose", destructive: false, confirm: false, run: importGraphicPack),
                         enabled: !store.gameRunning),
            SettingsItem(id: "gp.credits", title: "Cemu graphic packs on GitHub",
                         detail: "Made by the Cemu graphic packs project and its contributors (CC0).",
                         control: .action("Open", destructive: false, confirm: false, run: {
                             UIApplication.shared.open(GraphicPackStore.repoPage)
                         })),
        ]))
        return groups
    }

    private var installedPacksText: String {
        guard let installed = GraphicPackStore.shared.installed else { return "Not downloaded" }
        let name = installed.name.replacingOccurrences(of: "Cemu Graphic Packs: ", with: "")
        return installed.packCount > 0 ? "\(name) · \(installed.packCount) packs" : name
    }

    private func importGraphicPack() {
        DocumentImport.present(contentTypes: [.folder, .zip]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                GraphicPackStore.shared.importPicked(url) { outcome in
                    switch outcome {
                    case .success(let done):
                        notes["gp.importResult"] = "Imported \(done.packCount) pack\(done.packCount == 1 ? "" : "s") from \(done.folderName)."
                    case .failure(let error):
                        notes["gp.importResult"] = error.localizedDescription
                    }
                    values.refresh()
                }
            case .failure(let error):
                notes["gp.importResult"] = error.localizedDescription
            }
        }
    }

    var graphicPackGameGroups: [SettingsGroup] {
        let store = GraphicPackStore.shared
        let packs = store.packs.filter { $0.game == selectedPackGame }
        let categories = Array(Set(packs.map(\.category))).sorted { a, b in
            let ra = GraphicPack.categoryRank(a), rb = GraphicPack.categoryRank(b)
            return ra != rb ? ra < rb : a.localizedCaseInsensitiveCompare(b) == .orderedAscending
        }
        return categories.map { category in
            let items = packs.filter { $0.category == category }
                .sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
                .map { pack -> SettingsItem in
                    var notes: [String] = []
                    switch pack.support(on: packAPI) {
                    case .full: break
                    case .partial: notes.append(packAPI == .metal ? "Shaders not supported on Metal" : "Some shaders skipped")
                    case .inactive: notes.append("Won't activate here")
                    }
                    if pack.canCostSpeed { notes.append("Can cost speed") }
                    if pack.raisesFrameRate { notes.append("Needs speed to spare") }
                    if pack.isImported { notes.append("Imported") }
                    let detail = ([pack.brief] + [notes.joined(separator: " · ")]).filter { !$0.isEmpty }.joined(separator: "\n")
                    return SettingsItem(id: "gp.pack.\(pack.path)", title: pack.displayName, detail: detail.isEmpty ? nil : detail,
                                        control: .open(pack.enabled ? "On" : "Off", {
                                            selectedPackPath = pack.path
                                            packDetails = nil
                                            openLegacy(.graphicPack)
                                        }))
                }
            return SettingsGroup(id: "gp.cat.\(category)", title: category, items: items)
        }
    }

    var graphicPackGroups: [SettingsGroup] {
        let store = GraphicPackStore.shared
        guard let pack = store.packs.first(where: { $0.path == selectedPackPath }) else {
            return [SettingsGroup(id: "gp.missing", title: "", items: [
                SettingsItem(id: "gp.missingNote", title: "Pack not found", detail: "It was removed or the packs were rescanned.",
                             control: .info("", nil)),
            ])]
        }
        let locked = store.gameRunning
        var main = [
            SettingsItem(id: "gp.use", title: "Use this pack", detail: locked ? "A game is running. Close it to change this pack." : nil,
                         control: .toggle(values.custom(get: { GraphicPackStore.shared.packs.first { $0.path == pack.path }?.enabled ?? false },
                                                        set: { wantOn in setPack(pack, wantOn) })),
                         enabled: !locked),
        ]
        switch pack.support(on: packAPI) {
        case .full: break
        case .partial(let why), .inactive(let why):
            main.append(SettingsItem(id: "gp.support", title: "Not fully supported", detail: why, control: .info("", nil)))
        }
        if let text = packDetails?.description, !text.isEmpty {
            main.append(SettingsItem(id: "gp.about", title: "About this pack", detail: text, control: .info("", nil)))
        }
        var groups = [SettingsGroup(id: "gp.main", title: pack.game == GraphicPack.allGames ? "Every game" : pack.game, items: main)]

        if let details = packDetails {
            let profile = GraphicPackDeviceProfile.current
            var options: [SettingsItem] = []
            for category in details.categories {
                let choices = details.visiblePresets(in: category)
                guard !choices.isEmpty else { continue }
                let isResolution = details.isResolutionCategory(category)
                let suggestion = isResolution ? profile.suggestedPreset(in: choices) : nil
                let selected = choices.first(where: \.active)?.name ?? ""
                options.append(SettingsItem(id: "gp.opt.\(category)", title: category.isEmpty ? "Option" : category,
                                            detail: isResolution ? "Higher resolutions cost speed and memory. \(profile.summary)" : nil,
                                            control: .choice(choices.map { preset in
                                                var label = preset.name
                                                if isResolution, let height = preset.height, height > profile.suggestedMaxHeight { label += " · heavy here" }
                                                if isResolution, preset == suggestion { label += " · suggested" }
                                                return SettingsOption(id: preset.name, title: label)
                                            }, values.custom(get: { selected }, set: { name in
                                                store.setPreset(pack, category: category, preset: name) { packDetails = $0 }
                                            })),
                                            enabled: !locked))
                if let suggestion, suggestion.name != selected {
                    options.append(SettingsItem(id: "gp.suggest.\(category)", title: "Use the suggested setting",
                                                detail: suggestion.name,
                                                control: .action("Use", destructive: false, confirm: false, run: {
                                                    store.setPreset(pack, category: category, preset: suggestion.name) { packDetails = $0 }
                                                }),
                                                enabled: !locked))
                }
            }
            if !options.isEmpty { groups.append(SettingsGroup(id: "gp.options", title: "Options", items: options)) }
        }

        var more = [
            SettingsItem(id: "gp.reset", title: "Reset to the pack's defaults",
                         control: .action("Reset", destructive: false, confirm: false, run: {
                             store.reset(pack) { packDetails = $0 }
                         }),
                         enabled: !locked),
        ]
        if pack.isImported {
            more.append(SettingsItem(id: "gp.remove", title: "Remove imported pack",
                                     detail: "Deletes its folder from graphicPacks/imported.",
                                     control: .action("Remove", destructive: true, confirm: true, run: {
                                         store.removeImported(pack)
                                         closeLegacy()
                                     }),
                                     enabled: !locked))
        }
        groups.append(SettingsGroup(id: "gp.more", title: "", items: more))
        return groups
    }

    private func setPack(_ pack: GraphicPack, _ wantOn: Bool) {
        let store = GraphicPackStore.shared
        guard wantOn else { store.setEnabled(pack, false); values.refresh(); return }
        switch pack.support(on: packAPI) {
        case .full:
            store.setEnabled(pack, true)
            values.refresh()
        case .partial(let why), .inactive(let why):
            let inactive: Bool = { if case .inactive = pack.support(on: packAPI) { return true } else { return false } }()
            pageAlert = SettingsAlert(title: inactive ? "This pack won't activate" : (packAPI == .metal ? "Not fully supported on Metal" : "Not fully supported here"),
                                      message: why, actions: [
                                          .init(title: "Turn on anyway", run: { store.setEnabled(pack, true); values.refresh() }),
                                          .init(title: "Cancel", cancel: true),
                                      ])
        }
    }

    func loadPackDetails() {
        guard let pack = GraphicPackStore.shared.packs.first(where: { $0.path == selectedPackPath }) else { return }
        GraphicPackStore.shared.details(for: pack) { packDetails = $0 }
    }
}
