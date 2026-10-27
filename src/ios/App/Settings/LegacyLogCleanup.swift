// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

import Foundation

/// Older versions left their logs loose between the games, saves and keys in EspressU's Documents folder,
/// and later ones kept CemuCrashLog.txt in Documents/Logs. Crash details now only go into the EspressU report
/// in Documents/Logs, so this removes those leftovers when the app opens. Only these exact file names are
/// touched; the current logs in Documents/Logs stay.
enum LegacyLogCleanup {
    private static let looseInDocuments = [
        "CemuCrashLog.txt", "log.txt",
        "EspressU-Report.txt", "EspressU-Report-previous.txt", "EspressU-Report-older.txt",
    ]
    private static let inLogsFolder = ["CemuCrashLog.txt"]

    static func run() {
        DispatchQueue.global(qos: .utility).async {
            let fileManager = FileManager.default
            guard let documents = fileManager.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
            let logs = documents.appendingPathComponent("Logs", isDirectory: true)
            for (folder, names) in [(documents, looseInDocuments), (logs, inLogsFolder)] {
                for name in names {
                    let file = folder.appendingPathComponent(name)
                    var isDirectory: ObjCBool = false
                    guard fileManager.fileExists(atPath: file.path, isDirectory: &isDirectory), !isDirectory.boolValue else { continue }
                    try? fileManager.removeItem(at: file)
                }
            }
        }
    }
}
