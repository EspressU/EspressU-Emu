// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum ArtLocations {
    static var root: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        var url = base.appendingPathComponent("EspressU", isDirectory: true).appendingPathComponent("Art", isDirectory: true)
        let fm = FileManager.default
        if !fm.fileExists(atPath: url.path) {
            try? fm.createDirectory(at: url, withIntermediateDirectories: true)
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            try? url.setResourceValues(values)
        }
        return url
    }

    static var dataDirectory: URL { directory("GameData") }
    static var genericDirectory: URL { directory("Generic") }

    private static func directory(_ name: String) -> URL {
        let url = root.appendingPathComponent(name, isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
}

enum ArtTitleNormalizer {
    static func loose(_ title: String) -> String {
        let words = normalize(title.replacingOccurrences(of: "&", with: " and "))
            .split(separator: " ").filter { $0 != "the" && $0 != "and" }
        return words.joined().filter { $0.isLetter || $0.isNumber }
    }

    static func normalize(_ title: String) -> String {
        var t = title.lowercased()
        t = t.replacingOccurrences(of: "\\([^)]*\\)|\\[[^\\]]*\\]", with: "", options: .regularExpression)
        t = t.replacingOccurrences(of: "\u{2122}", with: "").replacingOccurrences(of: "\u{00AE}", with: "")
        t = t.trimmingCharacters(in: .whitespacesAndNewlines)
        if let range = t.range(of: ",\\s*the$", options: .regularExpression) { t.removeSubrange(range) }
        if let range = t.range(of: "^the\\s+", options: .regularExpression) { t.removeSubrange(range) }
        let kept = t.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) || CharacterSet.whitespacesAndNewlines.contains($0) || $0 == "_" }
        t = String(String.UnicodeScalarView(kept))
        return t.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    }
}

enum RegionCode: String, CaseIterable {
    case usa = "USA", eur = "EUR", jpn = "JPN", kor = "KOR", chn = "CHN", twn = "TWN"

    var name: String {
        switch self {
        case .usa: return "North America"
        case .eur: return "Europe"
        case .jpn: return "Japan"
        case .kor: return "South Korea"
        case .chn: return "China"
        case .twn: return "Taiwan"
        }
    }

    static func codes(in text: String?) -> Set<RegionCode> {
        guard let text = text?.lowercased(), !text.isEmpty else { return [] }
        var out = Set<RegionCode>()
        if text.contains("usa") || text.contains("ntsc-u") || text.contains("america") || text.contains("canada") || text == "us" { out.insert(.usa) }
        if text.contains("eur") || text.contains("pal") || text.contains("australia") || text.contains("uk") { out.insert(.eur) }
        if text.contains("jpn") || text.contains("japan") || text.contains("ntsc-j") { out.insert(.jpn) }
        if text.contains("kor") || text.contains("ntsc-k") { out.insert(.kor) }
        if text.contains("chn") || text.contains("china") { out.insert(.chn) }
        if text.contains("twn") || text.contains("taiwan") { out.insert(.twn) }
        return out
    }

    static func code(forGameTdbId id: String) -> RegionCode? {
        guard id.count >= 4 else { return nil }
        switch id[id.index(id.startIndex, offsetBy: 3)] {
        case "E": return .usa
        case "P", "D", "F", "I", "S", "U", "X", "Y", "Z": return .eur
        case "J": return .jpn
        case "K": return .kor
        case "W": return .twn
        default: return nil
        }
    }
}
