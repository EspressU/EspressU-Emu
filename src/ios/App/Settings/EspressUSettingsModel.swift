// SPDX-License-Identifier: MPL-2.0

import SwiftUI

enum SettingsCategory: Int, CaseIterable, Identifiable {
    case jit, performance, graphics, controls, screenSound, gamesData, system

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .jit:         return "JIT"
        case .performance: return "Performance"
        case .graphics:    return "Graphics"
        case .controls:    return "Controls"
        case .screenSound: return "Screen & Sound"
        case .gamesData:   return "Games & Data"
        case .system:      return "System"
        }
    }

    var summary: String {
        switch self {
        case .jit:         return "Built-in JIT: on or off, status and pairing."
        case .performance: return "How games run: the CPU and heat."
        case .graphics:    return "Picture quality, frame pacing and shaders."
        case .controls:    return "Controllers and motion."
        case .screenSound: return "Screen layout, brightness, volume and the microphone."
        case .gamesData:   return "Your library, keys, files, backups and online."
        case .system:      return "Look, help and about EspressU."
        }
    }

    var hint: String {
        switch self {
        case .jit:         return "Status, pairing"
        case .performance: return "CPU cores, speed, heat"
        case .graphics:    return "Resolution, shaders"
        case .controls:    return "Touch, controllers, motion"
        case .screenSound: return "Layout, brightness, volume"
        case .gamesData:   return "Library, keys, backups"
        case .system:      return "Look, help, about"
        }
    }

    var icon: String {
        switch self {
        case .jit:         return "bolt.fill"
        case .performance: return "speedometer"
        case .graphics:    return "photo.on.rectangle"
        case .controls:    return "gamecontroller.fill"
        case .screenSound: return "speaker.wave.2.fill"
        case .gamesData:   return "folder.fill"
        case .system:      return "gearshape.fill"
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
        case .emulatedClock: return "How fast the Wii U's clock runs."
        case .figures:       return "Toy figure portals for Skylanders, Disney Infinity and LEGO Dimensions."
        case .overlay:       return "What EspressU shows on top of a game."
        case .graphicPacks:  return "Mods and fixes for your games."
        case .linkedFolders: return "Game folders that stay where they are."
        case .keys:          return "Needed for encrypted dumps (.wud, .wux)."
        case .files:         return "Where EspressU keeps games, saves and more."
        case .backup:        return "Save every setting to a file, or load one."
        case .account:       return "Your Wii U account for saves and online play."
        case .diagnostics:   return "For bug reports."
        case .about:         return "Credits and open-source licences."
        case .figureManager: return "Load, create or clear figures."
        case .networkService: return "Which online service games connect to."
        case .licences:      return "The open-source code inside EspressU."
        case .graphicPackGame: return "Packs made for this game. Changes apply the next time you start a game."
        case .graphicPack:   return "Changes apply the next time you start a game."
        case .controllerMapping: return "Choose which controller button does what."
        }
    }

    var title: String {
        switch self {
        case .emulatedClock: return "Emulated clock"
        case .figures:       return "Figure portals"
        case .overlay:       return "Overlay and notifications"
        case .graphicPacks:  return "Graphic packs"
        case .linkedFolders: return "Linked folders"
        case .keys:          return "Wii U keys"
        case .files:         return "Your files"
        case .backup:        return "Back up and restore"
        case .account:       return "Account and online"
        case .diagnostics:   return "Device report and logs"
        case .about:         return "About EspressU"
        case .figureManager: return "Manage figures"
        case .networkService: return "Network service"
        case .licences:      return "Open-source licences"
        case .graphicPackGame: return "Graphic packs"
        case .graphicPack:   return "Pack"
        case .controllerMapping: return "Controller buttons"
        }
    }
}

struct SettingsOption: Identifiable {
    let id: String
    let title: String
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
            .fill(isOn ? Color(light: "#2FA36B", dark: "#34C77B") : EspressUTheme.wrapper)
            .frame(width: 52, height: 32)
            .overlay(
                Circle()
                    .fill(Color.white)
                    .frame(width: 26, height: 26)
                    .shadow(color: Color.black.opacity(0.25), radius: 1.5, y: 1)
                    .padding(3),
                alignment: isOn ? .trailing : .leading)
            .animation(.spring(response: 0.25, dampingFraction: 0.85), value: isOn)
            .accessibilityHidden(true)
    }
}
