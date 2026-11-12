// Copyright (c) 2026 EspressU.
// SPDX-License-Identifier: MPL-2.0

import Foundation

/// Looks up an English UI text in the string catalog (Resources/Localizable.xcstrings).
/// Unknown texts, like game names or developer messages, come back unchanged.
func loc(_ key: String) -> String {
    NSLocalizedString(key, comment: "")
}

/// Like `loc`, for texts with values in them. Write the English text with `%@` where a value goes,
/// translations keep the `%@` in the order that reads right (use `%1$@`, `%2$@` to reorder).
func locf(_ key: String, _ args: Any...) -> String {
    String(format: loc(key), arguments: args.map { String(describing: $0) })
}

/// Singular or plural text. The first value is always the count.
func locn(_ count: Int, _ one: String, _ other: String, _ more: Any...) -> String {
    locf(count == 1 ? one : other, [count] + more)
}

/// Texts the engine (CemuBridge) hands over in English. Each template is also listed in
/// tools/l10n/extra_strings.txt so it lands in the catalog; `%@` marks a value.
private let bridgeTemplateKeys: [String] = [
    "Not decided yet - the CPU path is chosen when the engine initializes, on the first launch.",
    "EspressU runs the PowerPC CPU with its interpreter, on %@. Cores: %@.",
    "one core",
    "three cores",
    "single-core",
    "multi-core",
    "one core because Favour accuracy is on",
    "one core because One-core mode is on",
    "one core, chosen in Settings",
    "one core: three were chosen in Settings, but this device has too little memory or too few cores to run three at once (%@)",
    "one core: this device has too little memory or too few cores to run three at once (%@)",
    "three cores, chosen in Settings (experimental; %@). This device has fewer than three performance cores, so expect it to run slower than one core",
    "three cores, chosen in Settings (experimental; %@)",
    "Auto picked one core: a three-core run of this title crashed or hung before",
    "Auto picked one core: the device is already hot (%@)",
    "Auto picked one core: too few performance cores (%@)",
    "Auto picked one core: this title's game profile asks for single-core (%@)",
    "Auto picked three cores: this title's game profile asks for multi-core (%@)",
    "Auto picked three cores: this device has the performance cores, memory and thermal room (%@)",
    "Auto picked one core: this device does not meet the cores, memory tier and thermal state needed for three (%@)",
    "%@ performance core(s), tier %@, thermal %@",
    "low",
    "standard",
    "high",
    "nominal",
    "fair",
    "serious",
    "critical",
    "unknown",
]

private let bridgeTemplates: [(key: String, regex: NSRegularExpression)] = bridgeTemplateKeys.compactMap { key in
    let parts = key.components(separatedBy: "%@").map { NSRegularExpression.escapedPattern(for: $0) }
    guard let regex = try? NSRegularExpression(pattern: "^" + parts.joined(separator: "(.*?)") + "$",
                                               options: [.dotMatchesLineSeparators]) else { return nil }
    return (key, regex)
}

/// Translates a text that comes from the engine, filling the values back in. Unknown texts stay as they are.
func locBridge(_ text: String) -> String {
    let ns = text as NSString
    let whole = NSRange(location: 0, length: ns.length)
    for template in bridgeTemplates {
        guard let match = template.regex.firstMatch(in: text, options: [], range: whole) else { continue }
        var values: [String] = []
        if match.numberOfRanges > 1 {
            for i in 1..<match.numberOfRanges { values.append(locBridge(ns.substring(with: match.range(at: i)))) }
        }
        if values.isEmpty { return loc(template.key) }
        return String(format: loc(template.key), arguments: values)
    }
    return loc(text)
}
