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
        case .disabled:     return "Off"
        case .topLeft:      return "Top Left"
        case .topCenter:    return "Top Centre"
        case .topRight:     return "Top Right"
        case .bottomLeft:   return "Bottom Left"
        case .bottomCenter: return "Bottom Centre"
        case .bottomRight:  return "Bottom Right"
        }
    }
}
