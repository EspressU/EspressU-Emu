import SwiftUI
import Combine

final class AccountSheetRouter: ObservableObject {
    static let shared = AccountSheetRouter()
    @Published var showingCreateAccount = false
    @Published private(set) var revision = 0
    func sheetClosed() { revision += 1 }
}
