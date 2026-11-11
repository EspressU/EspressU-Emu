import SwiftUI

enum ScreenPosition: Int, CaseIterable, Identifiable {
    case disabled = 0
    case topLeft = 1
    case topCenter = 2
    case topRight = 3
    case bottomLeft = 4
    case bottomCenter = 5
    case bottomRight = 6

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .disabled:     return loc("Off")
        case .topLeft:      return loc("Top Left")
        case .topCenter:    return loc("Top Centre")
        case .topRight:     return loc("Top Right")
        case .bottomLeft:   return loc("Bottom Left")
        case .bottomCenter: return loc("Bottom Centre")
        case .bottomRight:  return loc("Bottom Right")
        }
    }
}
