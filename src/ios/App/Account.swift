import Foundation

struct Account: Identifiable, Hashable {
    let persistentId: UInt32
    var miiName: String
    var birthYear: UInt16
    var birthMonth: UInt8
    var birthDay: UInt8
    var gender: Int
    var email: String
    var country: Int
    var isValidOnline: Bool

    var id: UInt32 { persistentId }
    var persistentIdHex: String { String(persistentId, radix: 16) }
    var displayName: String { miiName.isEmpty ? "default" : miiName }
    var displayNameWithId: String { "\(displayName) (\(persistentIdHex))" }

    init?(fields: [Substring]) {
        guard fields.count >= 9, let persistentId = UInt32(fields[0], radix: 16) else {
            return nil
        }

        self.persistentId = persistentId
        self.miiName = String(fields[1])
        self.birthYear = UInt16(fields[2]) ?? 0
        self.birthMonth = UInt8(fields[3]) ?? 0
        self.birthDay = UInt8(fields[4]) ?? 0
        self.gender = Int(fields[5]) ?? 0
        self.email = String(fields[6])
        self.country = Int(fields[7]) ?? 0
        self.isValidOnline = fields[8] == "1"
    }

    static func loadAll() -> [Account] {
        let raw = String(cString: cemu_bridge_accounts_list())
        guard !raw.isEmpty else { return [] }
        return raw.split(separator: "\u{1E}").compactMap { record in
            Account(fields: record.split(separator: "\u{1F}", omittingEmptySubsequences: false))
        }
    }
}
