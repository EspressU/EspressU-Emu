// SPDX-License-Identifier: MPL-2.0

import SwiftUI

enum SettingsCategory: Int, CaseIterable, Identifiable {
    case jit, performance, graphics, controls, screenSound, gamesData, system

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .jit:         return loc("JIT")
        case .performance: return loc("Performance")
        case .graphics:    return loc("Graphics")
        case .controls:    return loc("Controls")
        case .screenSound: return loc("Screen & Sound")
        case .gamesData:   return loc("Games & Data")
        case .system:      return loc("System")
        }
    }

    /// Each category keeps its own system colour, like the icons in the Settings app.
    var tint: Color {
        switch self {
        case .jit:         return Color(UIColor.systemOrange)
        case .performance: return Color(UIColor.systemGreen)
        case .graphics:    return Color(UIColor.systemBlue)
        case .controls:    return Color(UIColor.systemPurple)
        case .screenSound: return Color(UIColor.systemPink)
        case .gamesData:   return Color(UIColor.systemTeal)
        case .system:      return Color(UIColor.systemGray)
        }
    }

    var summary: String {
        switch self {
        case .jit:         return loc("Built-in JIT: on or off, status and pairing.")
        case .performance: return loc("How games run: the CPU and heat.")
        case .graphics:    return loc("Picture quality, frame pacing and shaders.")
        case .controls:    return loc("Controllers and motion.")
        case .screenSound: return loc("Screen layout, brightness, volume and the microphone.")
        case .gamesData:   return loc("Your library, keys, files, backups and online.")
        case .system:      return loc("Look, help and about EspressU.")
        }
    }

    var hint: String {
        switch self {
        case .jit:         return loc("Status, pairing")
        case .performance: return loc("CPU cores, speed, heat")
        case .graphics:    return loc("Resolution, shaders")
        case .controls:    return loc("Touch, controllers, motion")
        case .screenSound: return loc("Layout, brightness, volume")
        case .gamesData:   return loc("Library, keys, backups")
        case .system:      return loc("Look, help, about")
        }
    }

    var icon: String {
        switch self {
        case .jit:         return loc("bolt.fill")
        case .performance: return loc("speedometer")
        case .graphics:    return loc("photo.on.rectangle")
        case .controls:    return loc("gamecontroller.fill")
        case .screenSound: return loc("speaker.wave.2.fill")
        case .gamesData:   return loc("folder.fill")
        case .system:      return loc("gearshape.fill")
        }
    }
}

enum LegacySettingsPage: String, Identifiable {
    case emulatedClock, figures, overlay, graphicPacks, linkedFolders
    case keys, files, backup, account, diagnostics, about
    case figureManager, networkService, licences
    case graphicPackGame, graphicPack, controllerMapping

    var id: String { rawValue }

    var parent: LegacySettingsPage? {
        switch self {
        case .figureManager: return .figures
        case .networkService: return .account
        case .licences: return .about
        case .graphicPackGame: return .graphicPacks
        case .graphicPack: return .graphicPackGame
        default: return nil
        }
    }

    var summary: String {
        switch self {
        case .emulatedClock: return loc("How fast the Wii U's clock runs.")
        case .figures:       return loc("Toy figure portals for Skylanders, Disney Infinity and LEGO Dimensions.")
        case .overlay:       return loc("What EspressU shows on top of a game.")
        case .graphicPacks:  return loc("Mods and fixes for your games.")
        case .linkedFolders: return loc("Game folders that stay where they are.")
        case .keys:          return loc("Needed for encrypted dumps (.wud, .wux).")
        case .files:         return loc("Where EspressU keeps games, saves and more.")
        case .backup:        return loc("Save every setting to a file, or load one.")
        case .account:       return loc("Your Wii U account for saves and online play.")
        case .diagnostics:   return loc("For bug reports.")
        case .about:         return loc("Credits and open-source licences.")
        case .figureManager: return loc("Load, create or clear figures.")
        case .networkService: return loc("Which online service games connect to.")
        case .licences:      return loc("The open-source code inside EspressU.")
        case .graphicPackGame: return loc("Packs made for this game. Changes apply the next time you start a game.")
        case .graphicPack:   return loc("Changes apply the next time you start a game.")
        case .controllerMapping: return loc("Choose which controller button does what.")
        }
    }

    var title: String {
        switch self {
        case .emulatedClock: return loc("Emulated clock")
        case .figures:       return loc("Figure portals")
        case .overlay:       return loc("Overlay and notifications")
        case .graphicPacks:  return loc("Graphic packs")
        case .linkedFolders: return loc("Linked folders")
        case .keys:          return loc("Wii U keys")
        case .files:         return loc("Your files")
        case .backup:        return loc("Back up and restore")
        case .account:       return loc("Account and online")
        case .diagnostics:   return loc("Device report and logs")
        case .about:         return loc("About EspressU")
        case .figureManager: return loc("Manage figures")
        case .networkService: return loc("Network service")
        case .licences:      return loc("Open-source licences")
        case .graphicPackGame: return loc("Graphic packs")
        case .graphicPack:   return loc("Pack")
        case .controllerMapping: return loc("Controller buttons")
        }
    }
}

struct SettingsOption: Identifiable {
    let id: String
    let title: String

    /// `verbatim` keeps names that must not be translated, like a language shown in its own words.
    init(id: String, title: String, verbatim: Bool = false) {
        self.id = id
        self.title = verbatim ? title : loc(title)
    }
}

struct SettingsItem: Identifiable {
    enum Control {
        case toggle(Binding<Bool>)
        case choice([SettingsOption], Binding<String>)
        case slider(Binding<Double>, ClosedRange<Double>, step: Double, label: (Double) -> String)
        case action(String, destructive: Bool, confirm: Bool, run: () -> Void)
        case link(String, LegacySettingsPage)
        case open(String, () -> Void)
        case info(String, Color?)
        case custom(AnyView)
        case modeSwitch(Binding<Bool>)
        case banner(AnyView)
        case padButton(label: String, value: String, changed: Bool, run: () -> Void, reset: (() -> Void)?)
        case expand(Bool)
    }

    let id: String
    let title: String
    var detail: String? = nil
    let control: Control
    var enabled: Bool = true

    init(id: String, title: String, detail: String? = nil, control: Control, enabled: Bool = true) {
        self.id = id
        self.title = loc(title)
        self.detail = detail.map(loc)
        self.control = control
        self.enabled = enabled
    }

    var isFocusable: Bool {
        guard enabled else { return false }
        switch control {
        case .info, .custom, .banner: return false
        default: return true
        }
    }
}

struct SettingsGroup: Identifiable {
    let id: String
    let title: String
    let items: [SettingsItem]

    init(id: String, title: String, items: [SettingsItem]) {
        self.id = id
        self.title = loc(title)
        self.items = items
    }
}

final class SettingsValues: ObservableObject {
    private var defaults: UserDefaults { .standard }

    func refresh() { objectWillChange.send() }

    func bool(_ key: String, _ fallback: Bool, _ apply: ((Bool) -> Void)? = nil) -> Binding<Bool> {
        Binding(
            get: { UserDefaults.standard.object(forKey: key) as? Bool ?? fallback },
            set: { [weak self] value in
                self?.objectWillChange.send()
                UserDefaults.standard.set(value, forKey: key)
                apply?(value)
            })
    }

    func string(_ key: String, _ fallback: String, _ apply: ((String) -> Void)? = nil) -> Binding<String> {
        Binding(
            get: { UserDefaults.standard.string(forKey: key) ?? fallback },
            set: { [weak self] value in
                self?.objectWillChange.send()
                UserDefaults.standard.set(value, forKey: key)
                apply?(value)
            })
    }

    func intChoice(_ key: String, _ fallback: Int, _ apply: ((Int) -> Void)? = nil) -> Binding<String> {
        Binding(
            get: { String(UserDefaults.standard.object(forKey: key) as? Int ?? fallback) },
            set: { [weak self] value in
                guard let number = Int(value) else { return }
                self?.objectWillChange.send()
                UserDefaults.standard.set(number, forKey: key)
                apply?(number)
            })
    }

    func double(_ key: String, _ fallback: Double, _ apply: ((Double) -> Void)? = nil) -> Binding<Double> {
        Binding(
            get: { UserDefaults.standard.object(forKey: key) as? Double ?? fallback },
            set: { [weak self] value in
                self?.objectWillChange.send()
                UserDefaults.standard.set(value, forKey: key)
                apply?(value)
            })
    }

    func intSlider(_ key: String, _ fallback: Int, _ apply: ((Int) -> Void)? = nil) -> Binding<Double> {
        Binding(
            get: { Double(UserDefaults.standard.object(forKey: key) as? Int ?? fallback) },
            set: { [weak self] value in
                let rounded = Int(value.rounded())
                self?.objectWillChange.send()
                UserDefaults.standard.set(rounded, forKey: key)
                apply?(rounded)
            })
    }

    func custom<T>(get: @escaping () -> T, set: @escaping (T) -> Void) -> Binding<T> {
        Binding(
            get: get,
            set: { [weak self] value in
                self?.objectWillChange.send()
                set(value)
            })
    }
}

struct SettingsSwitch: View {
    let isOn: Bool

    var body: some View {
        Capsule()
            .fill(isOn ? Color(UIColor.systemGreen) : Color(UIColor.tertiarySystemFill))
            .frame(width: 51, height: 31)
            .overlay(
                Circle()
                    .fill(Color.white)
                    .frame(width: 27, height: 27)
                    .shadow(color: Color.black.opacity(0.2), radius: 2, y: 1)
                    .padding(2),
                alignment: isOn ? .trailing : .leading)
            .animation(.easeInOut(duration: 0.2), value: isOn)
            .accessibilityHidden(true)
    }
}
