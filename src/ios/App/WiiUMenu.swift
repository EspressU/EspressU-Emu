// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation

enum WiiUMenu {

    static var documentsURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
    }
    static var mlcRootURL: URL? { documentsURL?.appendingPathComponent("mlc") }
    static var mlc01URL: URL? { mlcRootURL?.appendingPathComponent("mlc01") }
    static var cafeLibsURL: URL? { mlcRootURL?.appendingPathComponent("cafeLibs") }

    enum ImportError: LocalizedError {
        case accessDenied
        case nothingRecognised
        case notTheMenu(String)
        case wrongSize(String, expected: Int)
        case notEnoughSpace(needed: Int64, available: Int64)
        case failed(String)

        var errorDescription: String? {
            switch self {
            case .accessDenied:
                return "Couldn't access that item."
            case .nothingRecognised:
                return "Nothing recognisable in that folder. Pick the package folder (the one containing mlc01 and cafeLibs), an mlc01 folder, a sys folder, a cafeLibs folder, or the Wii U Menu title folder itself."
            case .notTheMenu(let id):
                return "That title folder isn't a Wii U Menu (title id \(id)). The Menu is 0005001010040000, 0005001010040100 or 0005001010040200."
            case .wrongSize(let name, let expected):
                return "\(name) should be exactly \(expected) bytes."
            case .notEnoughSpace(let needed, let available):
                let format = ByteCountFormatter()
                return "Not enough free space: the import needs about \(format.string(fromByteCount: needed)) and this device has \(format.string(fromByteCount: available)) free."
            case .failed(let message):
                return message
            }
        }
    }

    private static let otpSize = 1024
    private static let seepromSize = 512

    static func importConsoleFiles(from urls: [URL]) throws -> [String] {
        guard let root = mlcRootURL else { throw ImportError.accessDenied }
        let fm = FileManager.default
        try? fm.createDirectory(at: root, withIntermediateDirectories: true)
        var lines: [String] = []
        var seenOTP = false, seenSeeprom = false
        for url in urls {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else { throw ImportError.accessDenied }
            let name = url.lastPathComponent.lowercased()
            let target: String
            if name.contains("otp") && !name.contains("seeprom") {
                target = "otp.bin"
            } else if name.contains("seeprom") {
                target = "seeprom.bin"
            } else if data.count == otpSize {
                target = "otp.bin"
            } else if data.count == seepromSize {
                target = "seeprom.bin"
            } else {
                throw ImportError.failed("\(url.lastPathComponent) isn't otp.bin or seeprom.bin. otp.bin is \(otpSize) bytes and seeprom.bin is \(seepromSize) bytes.")
            }
            let expected = target == "otp.bin" ? otpSize : seepromSize
            guard data.count == expected else { throw ImportError.wrongSize(target, expected: expected) }
            let destination = root.appendingPathComponent(target)
            if fm.fileExists(atPath: destination.path) {
                let backup = root.appendingPathComponent("\(target).backup-\(timestamp())")
                try fm.moveItem(at: destination, to: backup)
                lines.append("Existing \(target) saved as \(backup.lastPathComponent).")
            }
            do {
                try data.write(to: destination, options: .atomic)
            } catch {
                throw ImportError.failed("Couldn't save \(target): \(error.localizedDescription)")
            }
            if target == "otp.bin" { seenOTP = true } else { seenSeeprom = true }
        }
        if seenOTP { lines.append("otp.bin imported.") }
        if seenSeeprom { lines.append("seeprom.bin imported.") }
        return lines
    }

    static func timestamp() -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }
}
