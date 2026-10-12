import Foundation

@MainActor
final class EmulationEngine: ObservableObject {
    @Published private(set) var isRunning: Bool = false
    @Published private(set) var currentGame: String = ""
    @Published private(set) var statusText: String = ""

    let coreAvailable: Bool

    init() {
        coreAvailable = cemu_bridge_core_available()
        statusText = String(cString: cemu_bridge_status_text())
    }

    func initialize(mlcPath: String) {
        EmulationEngine.initializeBlocking(mlcPath: mlcPath)
        statusText = String(cString: cemu_bridge_status_text())
    }

    @discardableResult
    func boot(path: String) -> CemuBridgeStatus {
        currentGame = URL(fileURLWithPath: path).lastPathComponent
        let status = EmulationEngine.bootBlocking(path: path)
        isRunning = cemu_bridge_is_title_running()
        statusText = String(cString: cemu_bridge_status_text())
        return status
    }

    nonisolated static func initializeBlocking(mlcPath: String) {
        mlcPath.withCString { cemu_bridge_initialize($0) }
    }

    nonisolated static func bootBlocking(path: String) -> CemuBridgeStatus {
        path.withCString { cemu_bridge_boot_title($0) }
    }

    nonisolated static func reloadAndCountKeys() -> Int {
        Int(cemu_bridge_reload_and_count_keys())
    }

    func refreshStatus() {
        isRunning = cemu_bridge_is_title_running()
        statusText = String(cString: cemu_bridge_status_text())
    }

    func pause() {
        cemu_bridge_pause()
        isRunning = cemu_bridge_is_title_running()
    }

    func resume() {
        cemu_bridge_resume()
        isRunning = cemu_bridge_is_title_running()
    }

    func stop() {
        cemu_bridge_shutdown_title()
        isRunning = cemu_bridge_is_title_running()
        statusText = String(cString: cemu_bridge_status_text())
    }

    deinit {
        cemu_bridge_shutdown_title()
    }
}
