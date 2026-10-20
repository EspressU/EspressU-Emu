// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum SettingsBackup {
    static let formatName = "EspressU settings"
    static let currentVersion = 1
    private static let maxFileBytes = 2_000_000

    enum BackupError: LocalizedError {
        case unreadable
        case notASettingsFile
        case newerVersion(Int)
        case nothingToImport

        var errorDescription: String? {
            switch self {
            case .unreadable:
                return "That file couldn't be read."
            case .notASettingsFile:
                return "That isn't an EspressU settings file."
            case .newerVersion(let version):
                return "That file is from a newer EspressU (settings file version \(version)). Update the app, then try again."
            case .nothingToImport:
                return "That file has no settings EspressU can use."
            }
        }
    }

    struct Preview {
        let settings: [String: Any]
        let perGame: [String: GameOverrides]
        let skippedCount: Int

        var settingCount: Int { settings.count }
        var gameCount: Int { perGame.count }
    }

    static func exportData() throws -> Data {
        let defaults = UserDefaults.standard
        var settings: [String: Any] = [:]
        for (key, value) in defaults.dictionaryRepresentation() where SettingsDefaults.isPortableSetting(key) {
            if let converted = jsonValue(value) { settings[key] = converted }
        }
        if ThermalSettings.isHoldingScale,
           let chosen = defaults.string(forKey: ThermalSettings.scaleKey) {
            settings[RenderScale.storageKey] = chosen
        }

        var perGame: [String: Any] = [:]
        if let data = defaults.data(forKey: PerGameSettingsStore.storageKey),
           let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            perGame = object
        }

        let info = Bundle.main.infoDictionary
        let version = (info?["CFBundleShortVersionString"] as? String ?? "0") + " (" + (info?["CFBundleVersion"] as? String ?? "0") + ")"
        let document: [String: Any] = [
            "format": formatName,
            "version": currentVersion,
            "app": version,
            "exported": ISO8601DateFormatter().string(from: Date()),
            "settings": settings,
            "perGame": perGame,
        ]
        return try JSONSerialization.data(withJSONObject: document, options: [.prettyPrinted, .sortedKeys])
    }

    static func exportFile() throws -> URL {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("settings-export-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let url = directory.appendingPathComponent("EspressU settings \(formatter.string(from: Date())).json")
        try exportData().write(to: url, options: .atomic)
        return url
    }

    static func preview(of url: URL) throws -> Preview {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url), data.count <= maxFileBytes else { throw BackupError.unreadable }
        return try preview(of: data)
    }

    static func preview(of data: Data) throws -> Preview {
        guard let root = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              [formatName, "EspressU settings", "EspressU settings"].contains(root["format"] as? String ?? "") else { throw BackupError.notASettingsFile }
        guard let version = (root["version"] as? NSNumber)?.intValue else { throw BackupError.notASettingsFile }
        guard version <= currentVersion else { throw BackupError.newerVersion(version) }

        var skipped = 0
        var settings: [String: Any] = [:]
        for (key, raw) in (root["settings"] as? [String: Any]) ?? [:] {
            guard SettingsDefaults.isPortableSetting(key),
                  let value = plistValue(raw),
                  isAcceptable(value, for: key) else {
                skipped += 1
                continue
            }
            settings[key] = value
        }

        var perGame: [String: GameOverrides] = [:]
        if let games = root["perGame"] as? [String: Any], !games.isEmpty {
            guard JSONSerialization.isValidJSONObject(games),
                  let bytes = try? JSONSerialization.data(withJSONObject: games),
                  let decoded = try? JSONDecoder().decode([String: GameOverrides].self, from: bytes) else {
                throw BackupError.notASettingsFile
            }
            perGame = decoded.filter { !$0.value.isIdentity }
        }

        guard !settings.isEmpty || !perGame.isEmpty else { throw BackupError.nothingToImport }
        return Preview(settings: settings, perGame: perGame, skippedCount: skipped)
    }

    @MainActor
    static func apply(_ preview: Preview) {
        let defaults = UserDefaults.standard
        ThermalMonitor.shared.titleStopped()
        for key in defaults.dictionaryRepresentation().keys where SettingsDefaults.isPortableSetting(key) {
            defaults.removeObject(forKey: key)
        }
        for (key, value) in preview.settings { defaults.set(value, forKey: key) }
        PerGameSettingsStore.shared.replaceOverrides(preview.perGame)
        AdvancedSettings.discardSnapshot()
        if !SettingsMode.isAdvanced { AdvancedSettings.clearAdvanced() }

        UIStyleStore.shared.reloadFromDefaults()
        ControllerCustomLayout.shared.reloadFromDefaults()
        PreviewPadStore.shared.reloadFromDefaults()
        DisplayRouter.shared.reapplyRenderScale(reason: "settings import")
        AdvancedSettings.applyToBridge()
        MotionSettings.applyToBridge()
        NotificationCenter.default.post(name: .espressuSettingsWereReset, object: nil)
    }

    private static func jsonValue(_ value: Any) -> Any? {
        switch value {
        case let number as NSNumber:
            if !isBoolean(number), !number.doubleValue.isFinite { return nil }
            return number
        case let text as String:
            return text
        case let data as Data:
            return ["$data": data.base64EncodedString()]
        case let list as [Any]:
            let converted = list.compactMap(jsonValue)
            return converted.count == list.count ? converted : nil
        case let dictionary as [String: Any]:
            let converted = dictionary.compactMapValues(jsonValue)
            return converted.count == dictionary.count ? converted : nil
        default:
            return nil
        }
    }

    private static func plistValue(_ value: Any) -> Any? {
        switch value {
        case let number as NSNumber:
            if !isBoolean(number), !number.doubleValue.isFinite { return nil }
            return number
        case let text as String:
            return text
        case let list as [Any]:
            let converted = list.compactMap(plistValue)
            return converted.count == list.count ? converted : nil
        case let dictionary as [String: Any]:
            if dictionary.count == 1, let encoded = dictionary["$data"] as? String {
                return Data(base64Encoded: encoded)
            }
            let converted = dictionary.compactMapValues(plistValue)
            return converted.count == dictionary.count ? converted : nil
        default:
            return nil
        }
    }

    private static func isBoolean(_ number: NSNumber) -> Bool {
        CFGetTypeID(number) == CFBooleanGetTypeID()
    }

    private static let allowedStrings: [String: Set<String>] = [
        RenderScale.storageKey: Set(RenderScale.allCases.map(\.rawValue)),
        MoltenVKBuild.storageKey: Set(MoltenVKBuild.allCases.map(\.rawValue)),
        CoreMode.storageKey: Set(CoreMode.allCases.map(\.rawValue)),
        SettingsMode.storageKey: Set(SettingsMode.allCases.map(\.rawValue)),
        TopBarAutoHide.handleSizeKey: Set(TopBarAutoHide.HandleSize.allCases.map(\.rawValue)),
        ThermalSettings.thresholdKey: Set(ThermalSettings.Threshold.allCases.map(\.rawValue)),
        LocalScreenLayoutSettings.layoutKey: Set(ScreenLayout.allCases.map(\.rawValue)),
    ]

    private static let allowedInts: [String: ClosedRange<Int>] = [
        RendererAPI.storageKey: 1...2,
        UpscaleFilterSetting.storageKey: 0...3,
        DownscaleFilterSetting.storageKey: 0...3,
        FullSpeedRenders.shaderModeKey: 0...1,
        TopBarAutoHide.hideDelayKey: 2...8,
    ]

    private static let allowedDoubles: [String: ClosedRange<Double>] = [
        DisplayGammaSetting.storageKey: DisplayGammaSetting.minValue...DisplayGammaSetting.maxValue,
        OverrideGammaSetting.storageKey: OverrideGammaSetting.minValue...OverrideGammaSetting.maxValue,
        MotionSettings.sensitivityKey: MotionSettings.minSensitivity...MotionSettings.maxSensitivity,
        ControllerLayoutSettings.deadzoneKey: ControllerLayoutSettings.minDeadzone...ControllerLayoutSettings.maxDeadzone,
        ControllerLayoutSettings.stickCurveKey: ControllerLayoutSettings.minStickCurve...ControllerLayoutSettings.maxStickCurve,
        ControllerLayoutSettings.scaleKey: ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale,
        ControllerLayoutSettings.opacityKey: 0.2...1.0,
    ]

    private static func isAcceptable(_ value: Any, for key: String) -> Bool {
        if let allowed = allowedStrings[key] {
            guard let text = value as? String else { return false }
            return allowed.contains(text)
        }
        if let range = allowedInts[key] {
            guard let number = value as? NSNumber, !isBoolean(number),
                  number.doubleValue == number.doubleValue.rounded() else { return false }
            return range.contains(number.intValue)
        }
        if let range = allowedDoubles[key] {
            guard let number = value as? NSNumber, !isBoolean(number) else { return false }
            return range.contains(number.doubleValue)
        }
        if let setting = AdvancedSettings.allSettings.first(where: { $0.key == key }), let fallback = setting.defaultValue {
            guard let number = fallback as? NSNumber else { return value is String }
            guard let given = value as? NSNumber else { return false }
            return isBoolean(number) == isBoolean(given)
        }
        return true
    }
}
