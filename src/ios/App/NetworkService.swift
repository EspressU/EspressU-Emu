import Foundation

enum NetworkService: Int, CaseIterable, Identifiable {
    case offline = 0
    case nintendo = 1
    case pretendo = 2
    case custom = 3

    var id: Int { rawValue }
    static let onlineCases: [NetworkService] = [.nintendo, .pretendo, .custom]

    init(_ service: CemuBridgeNetworkService) {
        switch service {
        case CEMU_BRIDGE_NETWORK_NINTENDO: self = .nintendo
        case CEMU_BRIDGE_NETWORK_PRETENDO: self = .pretendo
        case CEMU_BRIDGE_NETWORK_CUSTOM: self = .custom
        default: self = .offline
        }
    }

    var bridgeValue: CemuBridgeNetworkService {
        switch self {
        case .offline: return CEMU_BRIDGE_NETWORK_OFFLINE
        case .nintendo: return CEMU_BRIDGE_NETWORK_NINTENDO
        case .pretendo: return CEMU_BRIDGE_NETWORK_PRETENDO
        case .custom: return CEMU_BRIDGE_NETWORK_CUSTOM
        }
    }

    var string: String {
        switch self {
        case .offline: return loc("Offline")
        case .nintendo: return loc("Nintendo Network (shut down)")
        case .pretendo: return loc("Pretendo Network")
        case .custom: return loc("Custom")
        }
    }

    var accountHelp: String {
        switch self {
        case .offline: return loc("Online play is off for this account.")
        case .nintendo: return loc("Connect to Nintendo's original Wii U servers, which are no longer running.")
        case .pretendo: return loc("Connect to the Pretendo Network Service, a community-run replacement for Nintendo's original Wii U online services.")
        case .custom: return loc("Connect to a custom Network Service, set up in network_services.xml.")
        }
    }
}
