import Foundation
import Network
import SwiftUI

final class DeviceConnection: ObservableObject {
    static let shared = DeviceConnection()

    enum Kind { case offline, wifi, cellular, other }

    @Published private(set) var kind: Kind = .wifi

    private let monitor = NWPathMonitor()
    private let queue = DispatchQueue(label: "espressu.device-connection")
    private var started = false

    func start() {
        guard !started else { return }
        started = true
        monitor.pathUpdateHandler = { [weak self] path in
            let kind: Kind
            if path.status != .satisfied {
                kind = .offline
            } else if path.usesInterfaceType(.wifi) {
                kind = .wifi
            } else if path.usesInterfaceType(.cellular) {
                kind = .cellular
            } else {
                kind = .other
            }
            cemu_bridge_set_device_network(Self.bridgeValue(kind))
            DispatchQueue.main.async { self?.kind = kind }
        }
        monitor.start(queue: queue)
    }

    private static func bridgeValue(_ kind: Kind) -> CemuBridgeDeviceNetwork {
        switch kind {
        case .offline: return CEMU_BRIDGE_DEVICE_NETWORK_OFFLINE
        case .wifi: return CEMU_BRIDGE_DEVICE_NETWORK_WIFI
        case .cellular: return CEMU_BRIDGE_DEVICE_NETWORK_CELLULAR
        case .other: return CEMU_BRIDGE_DEVICE_NETWORK_OTHER
        }
    }
}

struct DeviceConnectionStatusRow: View {
    @ObservedObject private var connection = DeviceConnection.shared

    private var deviceText: String {
        switch connection.kind {
        case .offline: return "Offline"
        case .wifi: return "Wi-Fi"
        case .cellular: return "Cellular"
        case .other: return "Connected"
        }
    }

    private var consoleText: String {
        _ = connection.kind
        return cemu_bridge_console_appears_connected()
            ? "The console will appear connected."
            : "The console will appear offline."
    }

    var body: some View {
        HStack {
            Text("Device connection")
            Spacer()
            Text("\(deviceText). \(consoleText)")
                .font(.footnote)
                .multilineTextAlignment(.trailing)
                .opacity(0.7)
        }
        .onAppear { DeviceConnection.shared.start() }
    }
}
