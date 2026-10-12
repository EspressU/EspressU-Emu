import Foundation

enum OnlineReadiness {
    static func missingItems(accountId: UInt32) -> [String] {
        if NetworkService(cemu_bridge_network_service(accountId)) == .custom { return [] }
        var missing: [String] = []
        switch cemu_bridge_account_online_error(accountId) {
        case 0: break
        case 2, 3: missing.append("a saved password for the linked Pretendo or Nintendo Network ID on this account")
        case 4: missing.append("a principal ID in this account")
        default: missing.append("a linked Pretendo or Nintendo Network ID on this account")
        }
        let fm = FileManager.default
        if let root = WiiUMenu.mlcRootURL {
            if !fm.fileExists(atPath: root.appendingPathComponent("otp.bin").path) { missing.append("otp.bin") }
            if !fm.fileExists(atPath: root.appendingPathComponent("seeprom.bin").path) { missing.append("seeprom.bin") }
        }
        return missing
    }

    static let howToFix = "A linked account has to come from a real Wii U. Link a Pretendo Network ID (PNID) on the console, then dump account.dat, otp.bin and seeprom.bin from it and import them with Import from Wii U in Settings > Account."

    private static func list(_ items: [String]) -> String {
        switch items.count {
        case 0: return ""
        case 1: return items[0]
        case 2: return "\(items[0]) and \(items[1])"
        default: return items.dropLast().joined(separator: ", ") + ", and " + items[items.count - 1]
        }
    }

    static func note(accountId: UInt32, service: NetworkService) -> String? {
        guard service == .pretendo || service == .nintendo else { return nil }
        let missing = missingItems(accountId: accountId)
        guard !missing.isEmpty else { return nil }
        return "Online play can't work yet. Missing: \(list(missing)). \(howToFix)"
    }

    private static var noticeShown = false

    static func launchNotice() -> String? {
        guard !noticeShown else { return nil }
        let accountId = cemu_bridge_active_account_persistent_id()
        guard NetworkService(cemu_bridge_network_service(accountId)) == .pretendo else { return nil }
        let missing = missingItems(accountId: accountId)
        guard !missing.isEmpty else { return nil }
        noticeShown = true
        return "Pretendo online play won't work yet. Missing: \(list(missing)). See Settings > Account."
    }
}
