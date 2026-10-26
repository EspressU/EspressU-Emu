// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

struct EspressUGamePacksView: View {
    let game: GameMetadata

    @ObservedObject private var store = GraphicPackStore.shared
    @StateObject private var input = ControllerMenuInput()
    @Environment(\.dismiss) private var dismiss

    @State private var focus: String?
    @State private var armed: String?
    @State private var dropdown: SettingsDropdownState?
    @State private var selectedPath: String?
    @State private var details: GraphicPackDetails?
    @State private var importNote: String?
    @State private var tick = 0
    @State private var support: SupportWarning?

    private struct SupportWarning {
        let pack: GraphicPack
        let title: String
        let message: String
    }

    private var api: RendererAPI {
        RendererAPI(rawValue: UserDefaults.standard.integer(forKey: RendererAPI.storageKey)) ?? RendererAPI.defaultValue
    }

    private var selectedPack: GraphicPack? {
        guard let selectedPath else { return nil }
        return store.packs.first { $0.path == selectedPath }
    }

    var body: some View {
        let groups = currentGroups
        ZStack {
            EspressUBackground().ignoresSafeArea()
            VStack(alignment: .leading, spacing: 0) {
                header
                ScrollViewReader { proxy in
                    ScrollView(showsIndicators: false) {
                        EspressURowList(groups: groups, focus: $focus, armed: $armed,
                                        showsFocus: input.showsFocus, stacked: false, dropdown: $dropdown)
                            .frame(maxWidth: 760)
                            .frame(maxWidth: .infinity)
                            .padding(.horizontal, 16)
                            .padding(.bottom, 32)
                    }
                    .onValueChange(of: focus) { id in
                        guard let id else { return }
                        withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(id, anchor: .center) }
                    }
                }
            }
        }
        .overlay { SettingsDropdownOverlay(state: $dropdown, groups: groups, showsFocus: input.showsFocus) }
        .animation(.easeOut(duration: 0.15), value: dropdown)
        .preferredColorScheme(EspressUAppearance.colorScheme)
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear {
            store.onOpen()
            input.onCommand = handle
            input.isExclusive = true
            input.start()
            focus = EspressURowLogic.focusOrder(currentGroups).first
        }
        .onDisappear { input.stop() }
    }

    private var header: some View {
        HStack(spacing: 12) {
            if selectedPack != nil {
                Button { goBack() } label: {
                    Image(systemName: "chevron.left")
                        .font(.system(size: 17, weight: .bold))
                        .foregroundColor(EspressUTheme.accentText)
                        .frame(width: 40, height: 40)
                        .background(EspressUTheme.wrapper, in: Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Back")
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(selectedPack?.displayName ?? "Graphic packs")
                    .font(.system(size: 24, weight: .heavy, design: .rounded))
                    .lineLimit(2)
                Text(selectedPack == nil ? game.cardName.name : "Changes apply the next time you start the game.")
                    .font(.system(size: 14, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
            }
            Spacer(minLength: 8)
            EspressUCloseButton(showsHint: input.showsFocus, hintSymbol: input.backSymbol, action: goBack)
        }
        .padding(.horizontal, 20)
        .padding(.top, 18)
        .padding(.bottom, 12)
    }

    private func goBack() {
        if support != nil { support = nil; return }
        if selectedPath != nil {
            let returning = selectedPath.map { "pack.\($0)" }
            selectedPath = nil
            details = nil
            focus = returning
            return
        }
        input.stop()
        dismiss()
    }

    private func refresh() { tick += 1 }

    private var currentGroups: [SettingsGroup] {
        _ = tick
        if let support { return supportGroups(support) }
        if let pack = selectedPack { return packGroups(pack) }
        return listGroups
    }

    private func supportGroups(_ warning: SupportWarning) -> [SettingsGroup] {
        [SettingsGroup(id: "gp.support", title: warning.title, items: [
            SettingsItem(id: "gp.support.why", title: "Heads up", detail: warning.message, control: .info("", nil)),
            SettingsItem(id: "gp.support.go", title: "Turn on anyway",
                         control: .action("Turn on", destructive: false, confirm: false, run: {
                             store.setEnabled(warning.pack, true)
                             support = nil
                             refresh()
                         })),
            SettingsItem(id: "gp.support.cancel", title: "Cancel",
                         control: .action("Cancel", destructive: false, confirm: false, run: { support = nil })),
        ])]
    }

    private var listGroups: [SettingsGroup] {
        var groups: [SettingsGroup] = []
        if store.gameRunning {
            groups.append(SettingsGroup(id: "gp.running", title: "", items: [
                SettingsItem(id: "gp.runningNote", title: "A game is running",
                             detail: "You can browse packs, but change them only after you close it.", control: .info("", nil)),
            ]))
        }
        if game.titleId == nil {
            groups.append(SettingsGroup(id: "gp.noid", title: "", items: [
                SettingsItem(id: "gp.noidNote", title: "Title ID unknown",
                             detail: "EspressU couldn't read this game's title ID, so it can't pick out its packs.", control: .info("", nil)),
            ]))
        } else {
            let own = store.packs.filter { !$0.universal && $0.appliesTo(titleId: game.titleId) }
            let universal = store.packs.filter(\.universal)
            if own.isEmpty && universal.isEmpty {
                groups.append(SettingsGroup(id: "gp.none", title: "", items: [
                    SettingsItem(id: "gp.noneNote",
                                 title: store.packs.isEmpty ? "No packs yet" : "No packs for this game",
                                 detail: store.packs.isEmpty
                                     ? "Download the community packs below, then come back."
                                     : "The community packs don't include one for \(game.title). You can still import your own.",
                                 control: .info("", nil)),
                ]))
            } else {
                groups.append(contentsOf: categoryGroups(own, prefix: ""))
                groups.append(contentsOf: categoryGroups(universal, prefix: "All games · "))
            }
        }
        groups.append(sourceGroup)
        groups.append(SettingsGroup(id: "gp.own", title: "Your own packs", items: [
            SettingsItem(id: "gp.import", title: "Import a pack folder or zip",
                         detail: importNote ?? "A folder with a rules.txt, a folder of packs, or a zip of either.",
                         control: .action("Choose", destructive: false, confirm: false, run: importPack),
                         enabled: !store.gameRunning),
        ]))
        return groups
    }

    private func categoryGroups(_ packs: [GraphicPack], prefix: String) -> [SettingsGroup] {
        let categories = Array(Set(packs.map(\.category))).sorted { a, b in
            let ra = GraphicPack.categoryRank(a), rb = GraphicPack.categoryRank(b)
            return ra != rb ? ra < rb : a.localizedCaseInsensitiveCompare(b) == .orderedAscending
        }
        return categories.map { category in
            let items = packs.filter { $0.category == category }
                .sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
                .map { pack -> SettingsItem in
                    var notes: [String] = []
                    switch pack.support(on: api) {
                    case .full: break
                    case .partial: notes.append(api == .metal ? "Shaders not supported on Metal" : "Some shaders skipped")
                    case .inactive: notes.append("Won't activate here")
                    }
                    if pack.canCostSpeed { notes.append("Can cost speed") }
                    if pack.raisesFrameRate { notes.append("Needs speed to spare") }
                    if pack.isImported { notes.append("Imported") }
                    let detail = ([pack.brief] + [notes.joined(separator: " · ")]).filter { !$0.isEmpty }.joined(separator: "\n")
                    return SettingsItem(id: "pack.\(pack.path)", title: pack.displayName, detail: detail.isEmpty ? nil : detail,
                                        control: .action(pack.enabled ? "On" : "Off", destructive: false, confirm: false, run: {
                                            open(pack)
                                        }))
                }
            return SettingsGroup(id: "gp.cat.\(prefix)\(category)", title: prefix + category, items: items)
        }
    }

    private var sourceGroup: SettingsGroup {
        var items: [SettingsItem] = [
            SettingsItem(id: "gp.installed", title: "Community packs",
                         detail: store.installed == nil ? "Made by the Cemu graphic packs project. Verified before anything is replaced." : nil,
                         control: .info(installedText, nil)),
        ]
        switch store.phase {
        case .checking:
            items.append(SettingsItem(id: "gp.checking", title: "Checking for updates…", control: .info("", nil)))
        case .downloading(let fraction), .installing(let fraction):
            let installing: Bool = { if case .installing = store.phase { return true } else { return false } }()
            items.append(SettingsItem(id: "gp.progress", title: installing ? "Installing…" : "Downloading…",
                                      detail: "\(Int((fraction * 100).rounded()))%",
                                      control: .action("Cancel", destructive: true, confirm: false, run: { store.cancel() })))
        case .waitingForGameToClose:
            items.append(SettingsItem(id: "gp.waiting", title: "Downloaded",
                                      detail: "It installs when this page is opened with no game running.", control: .info("", nil)))
        case .idle:
            if store.updateAvailable, let latest = store.latest {
                items.append(SettingsItem(id: "gp.download",
                                          title: store.installed == nil ? "Download community packs" : "Update community packs",
                                          detail: store.installed == nil
                                              ? "\(latest.name), \(GraphicPackStorage.describe(latest.size))."
                                              : "\(latest.name). Your choices are kept.",
                                          control: .action(store.installed == nil ? "Download" : "Update", destructive: false, confirm: false,
                                                           run: { store.startDownload() }),
                                          enabled: !store.gameRunning))
            }
            items.append(SettingsItem(id: "gp.check", title: store.latest == nil ? "Check for community packs" : "Check for updates",
                                      detail: store.notice?.text,
                                      control: .action("Check", destructive: false, confirm: false, run: { store.checkForUpdate(force: true) })))
        }
        return SettingsGroup(id: "gp.source", title: "Download", items: items)
    }

    private var installedText: String {
        guard let installed = store.installed else { return "Not downloaded" }
        let name = installed.name.replacingOccurrences(of: "Cemu Graphic Packs: ", with: "")
        return installed.packCount > 0 ? "\(name) · \(installed.packCount) packs" : name
    }

    private func open(_ pack: GraphicPack) {
        selectedPath = pack.path
        details = nil
        store.details(for: pack) { details = $0 }
        focus = "gp.use"
    }

    private func packGroups(_ pack: GraphicPack) -> [SettingsGroup] {
        let locked = store.gameRunning
        var main = [
            SettingsItem(id: "gp.use", title: "Use this pack", detail: locked ? "A game is running. Close it to change this pack." : nil,
                         control: .toggle(Binding(get: { store.packs.first { $0.path == pack.path }?.enabled ?? false },
                                                  set: { setPack(pack, $0) })),
                         enabled: !locked),
        ]
        switch pack.support(on: api) {
        case .full: break
        case .partial(let why), .inactive(let why):
            main.append(SettingsItem(id: "gp.supportNote", title: "Not fully supported", detail: why, control: .info("", nil)))
        }
        if let text = details?.description, !text.isEmpty {
            main.append(SettingsItem(id: "gp.about", title: "About this pack", detail: text, control: .info("", nil)))
        }
        var groups = [SettingsGroup(id: "gp.main", title: pack.game == GraphicPack.allGames ? "Every game" : pack.game, items: main)]

        if let details {
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
                                            }, Binding(get: { selected }, set: { name in
                                                store.setPreset(pack, category: category, preset: name) { self.details = $0 }
                                            })),
                                            enabled: !locked))
                if let suggestion, suggestion.name != selected {
                    options.append(SettingsItem(id: "gp.suggest.\(category)", title: "Use the suggested setting",
                                                detail: suggestion.name,
                                                control: .action("Use", destructive: false, confirm: false, run: {
                                                    store.setPreset(pack, category: category, preset: suggestion.name) { self.details = $0 }
                                                }),
                                                enabled: !locked))
                }
            }
            if !options.isEmpty { groups.append(SettingsGroup(id: "gp.options", title: "Options", items: options)) }
        }

        var more = [
            SettingsItem(id: "gp.reset", title: "Reset to the pack's defaults",
                         control: .action("Reset", destructive: false, confirm: false, run: {
                             store.reset(pack) { self.details = $0 }
                         }),
                         enabled: !locked),
        ]
        if pack.isImported {
            more.append(SettingsItem(id: "gp.remove", title: "Remove imported pack",
                                     detail: "Deletes its folder from graphicPacks/imported.",
                                     control: .action("Remove", destructive: true, confirm: true, run: {
                                         store.removeImported(pack)
                                         selectedPath = nil
                                         details = nil
                                     }),
                                     enabled: !locked))
        }
        groups.append(SettingsGroup(id: "gp.more", title: "", items: more))
        return groups
    }

    private func setPack(_ pack: GraphicPack, _ wantOn: Bool) {
        guard wantOn else { store.setEnabled(pack, false); refresh(); return }
        switch pack.support(on: api) {
        case .full:
            store.setEnabled(pack, true)
            refresh()
        case .partial(let why), .inactive(let why):
            let inactive: Bool = { if case .inactive = pack.support(on: api) { return true } else { return false } }()
            support = SupportWarning(pack: pack,
                                     title: inactive ? "This pack won't activate" : (api == .metal ? "Not fully supported on Metal" : "Not fully supported here"),
                                     message: why)
            focus = "gp.support.go"
        }
    }

    private func importPack() {
        DocumentImport.present(contentTypes: [.folder, .zip]) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                store.importPicked(url) { outcome in
                    switch outcome {
                    case .success(let done):
                        importNote = "Imported \(done.packCount) pack\(done.packCount == 1 ? "" : "s") from \(done.folderName)."
                    case .failure(let error):
                        importNote = error.localizedDescription
                    }
                    refresh()
                }
            case .failure(let error):
                importNote = error.localizedDescription
            }
        }
    }

    private func handle(_ command: ControllerMenuCommand) {
        let groups = currentGroups
        if dropdown != nil {
            SettingsDropdown.handle(command, state: $dropdown, groups: groups)
            return
        }
        let order = EspressURowLogic.focusOrder(groups)
        switch command {
        case .up, .down:
            focus = EspressURowLogic.step(focus, in: order, by: command == .down ? 1 : -1)
        case .left, .right:
            if let item = EspressURowLogic.item(focus, in: groups) {
                _ = EspressURowLogic.adjust(item, by: command == .right ? 1 : -1)
            }
        case .confirm:
            if let item = EspressURowLogic.item(focus, in: groups) {
                EspressURowLogic.activate(item, armed: $armed, dropdown: $dropdown)
            } else {
                focus = order.first
            }
        case .back, .menu:
            goBack()
        default:
            break
        }
    }
}
