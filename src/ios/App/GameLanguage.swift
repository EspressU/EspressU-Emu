// Copyright (c) 2026 EspressU.
// SPDX-License-Identifier: MPL-2.0

import Foundation

/// The Wii U system language a game sees (what the console's own System Settings would say).
/// "Automatic" follows the device language and falls back to English.
enum GameLanguage: String, CaseIterable, Identifiable {
    case automatic
    case japanese, english, french, german, italian, spanish
    case chineseSimplified, korean, dutch, portuguese, russian, chineseTraditional

    static let storageKey = "espressu.game.language"
    static let defaultValue = GameLanguage.automatic

    var id: String { rawValue }

    /// CafeConsoleLanguage value used by the core.
    private var cafeValue: Int32? {
        switch self {
        case .automatic: return nil
        case .japanese: return 0
        case .english: return 1
        case .french: return 2
        case .german: return 3
        case .italian: return 4
        case .spanish: return 5
        case .chineseSimplified: return 6
        case .korean: return 7
        case .dutch: return 8
        case .portuguese: return 9
        case .russian: return 10
        case .chineseTraditional: return 11
        }
    }

    /// Each language is shown in its own name so it can be found whatever the app language is.
    var nativeName: String {
        switch self {
        case .automatic: return "Automatic"
        case .japanese: return "日本語"
        case .english: return "English"
        case .french: return "Français"
        case .german: return "Deutsch"
        case .italian: return "Italiano"
        case .spanish: return "Español"
        case .chineseSimplified: return "简体中文"
        case .korean: return "한국어"
        case .dutch: return "Nederlands"
        case .portuguese: return "Português"
        case .russian: return "Русский"
        case .chineseTraditional: return "繁體中文"
        }
    }

    static var stored: GameLanguage {
        GameLanguage(rawValue: UserDefaults.standard.string(forKey: storageKey) ?? "") ?? defaultValue
    }

    /// Wii U language for the device language list (first match wins), English if none fits.
    static func fromDevice(_ preferred: [String] = Locale.preferredLanguages) -> GameLanguage {
        for tag in preferred {
            let locale = Locale(identifier: tag)
            let code = locale.language.languageCode?.identifier ?? ""
            switch code {
            case "ja": return .japanese
            case "en": return .english
            case "fr": return .french
            case "de": return .german
            case "it": return .italian
            case "es": return .spanish
            case "ko": return .korean
            case "nl": return .dutch
            case "pt": return .portuguese
            case "ru": return .russian
            case "zh":
                let script = locale.language.script?.identifier ?? ""
                let region = locale.region?.identifier ?? ""
                if script == "Hant" || ["TW", "HK", "MO"].contains(region) { return .chineseTraditional }
                return .chineseSimplified
            default: continue
            }
        }
        return .english
    }

    /// The language that reaches the game after resolving "Automatic".
    var resolved: GameLanguage { self == .automatic ? GameLanguage.fromDevice() : self }

    static func applyStoredToBridge() {
        cemu_bridge_set_console_language(stored.resolved.cafeValue ?? 1)
    }
}
