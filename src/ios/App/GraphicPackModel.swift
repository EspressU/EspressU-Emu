// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
#if canImport(Metal)
import Metal
#endif
#if canImport(UIKit)
import UIKit
#endif

struct GraphicPack: Identifiable, Hashable {
    let path: String
    let name: String
    let virtualPath: String
    var enabled: Bool
    let defaultEnabled: Bool
    let universal: Bool
    let version: Int
    let titleIds: Set<String>
    let rendererFilter: String
    let vendorFilter: String
    let hasGLSLShaders: Bool
    let hasMetalShaders: Bool
    let hasGLSLOutputShaders: Bool
    let presetCount: Int
    let brief: String

    var id: String { path }

    var isImported: Bool { path.contains("/imported/") }

    var game: String {
        let parts = virtualPath.split(separator: "/").map(String.init)
        if universal && parts.count < 2 { return GraphicPack.allGames }
        return parts.count >= 2 ? parts[0] : GraphicPack.otherGame
    }

    var category: String {
        let parts = virtualPath.split(separator: "/").map(String.init)
        guard parts.count >= 2 else { return "Other" }
        return GraphicPack.normalizedCategory(parts[1])
    }

    var displayName: String {
        if name == virtualPath {
            let parts = virtualPath.split(separator: "/").map(String.init)
            if parts.count >= 3 { return parts[2...].joined(separator: " / ") }
            if let last = parts.last { return last }
        }
        return name
    }

    static let allGames = "All games"
    static let otherGame = "Other"

    static let categoryOrder = ["Graphics", "Enhancements", "Mods", "Workarounds"]

    static func normalizedCategory(_ raw: String) -> String {
        switch raw.lowercased() {
        case "graphics", "graphic", "resolution", "resolutions": return "Graphics"
        case "enhancements", "enhancement": return "Enhancements"
        case "mods", "mod", "cheats", "cheat": return "Mods"
        case "workarounds", "workaround", "fixes", "!override", "override": return "Workarounds"
        default:
            let trimmed = raw.trimmingCharacters(in: CharacterSet(charactersIn: "!"))
            return trimmed.isEmpty ? "Other" : trimmed
        }
    }

    static func categoryRank(_ category: String) -> Int {
        categoryOrder.firstIndex(of: category) ?? categoryOrder.count
    }

    func appliesTo(titleId: UInt64?) -> Bool {
        if universal { return true }
        guard let titleId else { return false }
        return titleIds.contains(String(format: "%016llx", titleId))
    }

    static func parseList(_ raw: String) -> [GraphicPack] {
        guard !raw.isEmpty else { return [] }
        return raw.split(separator: "\u{1E}", omittingEmptySubsequences: true).compactMap { record in
            let f = record.split(separator: "\u{1F}", omittingEmptySubsequences: false).map(String.init)
            guard f.count >= 15, !f[0].isEmpty else { return nil }
            return GraphicPack(
                path: f[0],
                name: f[1],
                virtualPath: f[2],
                enabled: f[3] == "1",
                defaultEnabled: f[4] == "1",
                universal: f[5] == "1",
                version: Int(f[6]) ?? 0,
                titleIds: Set(f[7].split(separator: ",").map { $0.lowercased() }),
                rendererFilter: f[8],
                vendorFilter: f[9],
                hasGLSLShaders: f[10] == "1",
                hasMetalShaders: f[11] == "1",
                hasGLSLOutputShaders: f[12] == "1",
                presetCount: Int(f[13]) ?? 0,
                brief: f[14]
            )
        }
    }

    enum Support: Equatable {
        case full
        case partial(String)
        case inactive(String)

        var isFull: Bool { if case .full = self { return true } else { return false } }
    }

    func support(on api: RendererAPI) -> Support {
        if !vendorFilter.isEmpty && vendorFilter != "apple" {
            return .inactive("Made for a different kind of graphics chip, so it never turns on here.")
        }
        switch rendererFilter {
        case "metal" where api != .metal:
            return .inactive("Only works with the Metal renderer. Switch the renderer in Settings > Graphics.")
        case "vulkan" where api != .vulkan:
            return .inactive("Only works with the Vulkan renderer. Switch the renderer in Settings > Graphics.")
        case "opengl":
            return .inactive("Made for OpenGL, which EspressU doesn't use.")
        default:
            break
        }
        switch api {
        case .metal:
            if hasGLSLShaders && !hasMetalShaders {
                return .partial("Replaces shaders that are written for OpenGL and Vulkan. Metal can't use them, so those changes are skipped and the picture can look wrong. The pack's other changes still apply.")
            }
            if hasGLSLOutputShaders && !hasMetalShaders {
                return .partial("Adds an output or scaling shader written for OpenGL and Vulkan. Metal can't use it, so that part is skipped.")
            }
            return .full
        case .vulkan:
            if hasMetalShaders && !hasGLSLShaders {
                return .partial("Its shaders are written for Metal only, so the Vulkan renderer skips them.")
            }
            if hasGLSLShaders && version <= 3 {
                return .partial("Its shaders predate Vulkan support, so the Vulkan renderer skips them.")
            }
            return .full
        }
    }

    var canCostSpeed: Bool { category == "Graphics" }

    var raisesFrameRate: Bool {
        let n = (name + " " + virtualPath).lowercased()
        return n.contains("fps") || n.contains("frame rate") || n.contains("framerate")
    }

    var isWorkaround: Bool { category == "Workarounds" }
}

struct GraphicPackPreset: Hashable {
    let category: String
    let name: String
    let active: Bool
    let visible: Bool
    let isDefault: Bool

    var height: Int? { GraphicPackPreset.height(fromName: name) }

    static func height(fromName name: String) -> Int? {
        let ns = name as NSString
        let full = NSRange(location: 0, length: ns.length)
        if let re = try? NSRegularExpression(pattern: #"(\d{3,4})\s*[xX×]\s*(\d{3,4})"#),
           let m = re.firstMatch(in: name, range: full),
           let a = Int(ns.substring(with: m.range(at: 1))), let b = Int(ns.substring(with: m.range(at: 2))) {
            let h = min(a, b)
            return (360...4320).contains(h) ? h : nil
        }
        if let re = try? NSRegularExpression(pattern: #"(?<![\d])(\d{3,4})\s*[pP](?![a-zA-Z])"#),
           let m = re.firstMatch(in: name, range: full),
           let h = Int(ns.substring(with: m.range(at: 1))) {
            return (360...4320).contains(h) ? h : nil
        }
        return nil
    }
}

struct GraphicPackDetails {
    var description: String
    var presets: [GraphicPackPreset]

    var categories: [String] {
        var seen = Set<String>()
        var order: [String] = []
        for p in presets where !seen.contains(p.category) {
            seen.insert(p.category)
            order.append(p.category)
        }
        return order
    }

    func presets(in category: String) -> [GraphicPackPreset] { presets.filter { $0.category == category } }

    func visiblePresets(in category: String) -> [GraphicPackPreset] {
        presets(in: category).filter(\.visible)
    }

    func isResolutionCategory(_ category: String) -> Bool {
        presets(in: category).filter { $0.height != nil }.count >= 2
    }

    static func parse(_ raw: String) -> GraphicPackDetails {
        let halves = raw.split(separator: "\u{1D}", maxSplits: 1, omittingEmptySubsequences: false)
        let description = halves.first.map(String.init) ?? ""
        var presets: [GraphicPackPreset] = []
        if halves.count > 1 {
            for record in halves[1].split(separator: "\u{1E}", omittingEmptySubsequences: true) {
                let f = record.split(separator: "\u{1F}", omittingEmptySubsequences: false).map(String.init)
                guard f.count >= 5 else { continue }
                presets.append(GraphicPackPreset(category: f[0], name: f[1], active: f[2] == "1",
                                                 visible: f[3] == "1", isDefault: f[4] == "1"))
            }
        }
        return GraphicPackDetails(description: description, presets: presets)
    }
}

struct GraphicPackDeviceProfile: Equatable {
    var gpuFamily: Int
    var memoryGB: Double
    var screenShortSidePixels: Int
    var lowPowerMode: Bool
    var isPad: Bool

    static let steps = [720, 1080, 1440, 2160]

    var score: Int {
        let memory: Int
        switch memoryGB {
        case ..<3.5: memory = 0
        case ..<5: memory = 1
        case ..<7: memory = 2
        case ..<11: memory = 3
        default: memory = 4
        }
        return gpuFamily + memory
    }

    var suggestedMaxHeight: Int {
        var index: Int
        switch score {
        case ..<4: index = 0
        case 4...5: index = 1
        default: index = 2
        }
        let screenCap = GraphicPackDeviceProfile.steps.lastIndex { $0 <= max(720, screenShortSidePixels) } ?? 0
        index = min(index, screenCap)
        if lowPowerMode { index = max(0, index - 1) }
        return GraphicPackDeviceProfile.steps[index]
    }

    var summary: String {
        let tall = suggestedMaxHeight
        let label = tall == 720 ? "the Wii U's own 720p" : "\(tall)p"
        var text = "For this \(isPad ? "iPad" : "iPhone") we suggest staying at \(label) or lower."
        if lowPowerMode { text += " Low Power Mode is on, so the suggestion is one step lower." }
        text += " This comes from the graphics chip, memory and screen, not a speed test, so treat it as a starting point."
        return text
    }

    private static let hardware: (family: Int, gigabytes: Double, short: Int, pad: Bool) = {
        var family = 1
        #if canImport(Metal)
        if let device = MTLCreateSystemDefaultDevice() {
            if #available(iOS 17.0, macOS 14.0, *), device.supportsFamily(.apple9) {
                family = 5
            } else if #available(iOS 16.0, macOS 13.0, *), device.supportsFamily(.apple8) {
                family = 4
            } else if device.supportsFamily(.apple7) {
                family = 3
            } else if device.supportsFamily(.apple6) {
                family = 2
            }
        }
        #endif
        let gigabytes = Double(ProcessInfo.processInfo.physicalMemory) / 1_073_741_824.0
        #if canImport(UIKit) && !os(tvOS)
        let bounds = UIScreen.main.nativeBounds
        return (family, gigabytes, Int(min(bounds.width, bounds.height)), UIDevice.current.userInterfaceIdiom == .pad)
        #else
        return (family, gigabytes, 1440, true)
        #endif
    }()

    static var current: GraphicPackDeviceProfile {
        GraphicPackDeviceProfile(gpuFamily: hardware.family, memoryGB: hardware.gigabytes,
                                 screenShortSidePixels: hardware.short,
                                 lowPowerMode: ProcessInfo.processInfo.isLowPowerModeEnabled, isPad: hardware.pad)
    }

    func suggestedPreset(in choices: [GraphicPackPreset]) -> GraphicPackPreset? {
        let withHeight = choices.compactMap { p in p.height.map { (p, $0) } }
        guard !withHeight.isEmpty else { return nil }
        let within = withHeight.filter { $0.1 <= suggestedMaxHeight }
        if let best = within.max(by: { $0.1 < $1.1 }) { return best.0 }
        return withHeight.min(by: { $0.1 < $1.1 })?.0
    }
}

enum GraphicPackStorage {
    static func freeBytes() -> Int64? {
        let url = GraphicPackPaths.documents
        if let values = try? url.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]),
           let important = values.volumeAvailableCapacityForImportantUsage, important > 0 {
            return important
        }
        if let values = try? url.resourceValues(forKeys: [.volumeAvailableCapacityKey]),
           let plain = values.volumeAvailableCapacity {
            return Int64(plain)
        }
        return nil
    }

    static func describe(_ bytes: Int64) -> String {
        ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)
    }

    static func require(_ needed: Int64, what: String) throws {
        let margin: Int64 = 20 * 1024 * 1024
        guard let free = freeBytes() else { return }
        if free < needed + margin {
            throw StorageError.notEnough(needed: needed + margin, free: free, what: what)
        }
    }

    enum StorageError: LocalizedError {
        case notEnough(needed: Int64, free: Int64, what: String)

        var errorDescription: String? {
            switch self {
            case .notEnough(let needed, let free, let what):
                return "Not enough free storage to \(what). It needs about \(GraphicPackStorage.describe(needed)) and this device has \(GraphicPackStorage.describe(free)) free. Free some space and try again."
            }
        }
    }
}
