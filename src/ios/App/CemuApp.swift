import SwiftUI

@main
struct CemuApp: App {
    @UIApplicationDelegateAdaptor(AppOrientationDelegate.self) private var orientationDelegate
    @ObservedObject private var themeStore = EspressUThemeStore.shared
    @Environment(\.scenePhase) private var scenePhase
    @AppStorage(EspressUAppearance.storageKey) private var appearanceMode = "system"

    init() {
        DeviceCapabilities.bootstrap()
        DeviceConnection.shared.start()
        SettingsMode.chooseInitialModeIfNeeded()
        JITSettings.migrate()
        RemovedSettingsCleanup.run()
        MotionSettings.startObservingControllers()

        let info = Bundle.main.infoDictionary ?? [:]
        let version = info["CFBundleShortVersionString"] as? String ?? "?"
        let build = info["CFBundleVersion"] as? String ?? "?"
        let release = info["EspressUReleaseCommit"] as? String ?? "unstamped"
        let core = info["EspressUCoreFingerprint"] as? String ?? "unstamped"
        cemu_bridge_log_checkpoint("EspressU \(version) (build \(build)) release \(release), core \(core)")

        cemu_bridge_log_checkpoint("CemuApp.init() reached")

        if let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            for folder in ["keys", "Roms"] {
                try? FileManager.default.createDirectory(
                    at: documents.appendingPathComponent(folder),
                    withIntermediateDirectories: true
                )
            }
        }

        cemu_bridge_set_vsync_enabled(
            UserDefaults.standard.object(forKey: "espressu.render.vsync") as? Bool ?? true)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .id(themeStore.current.id)
                .preferredColorScheme(appearanceMode == "light" ? .light : (appearanceMode == "dark" ? .dark : nil))
                .espressuScrollEdgeBlurHidden()
                .onChange(of: scenePhase) { phase in
                    if phase != .active {
                        ControllerCustomLayout.shared.flushPending()
                        UserDefaults.standard.synchronize()
                    }
                }
                .onAppear {
                    cemu_bridge_log_checkpoint("ContentView.onAppear reached")
                    #if os(iOS)
                    DisplayRouter.shared.startObserving()
                    #endif
                }
        }
    }
}

extension View {
    @ViewBuilder func espressuScrollEdgeBlurHidden() -> some View {
        #if compiler(>=6.2)
        if #available(iOS 26.0, *) {
            self.scrollEdgeEffectHidden(true, for: .all)
        } else {
            self
        }
        #else
        self
        #endif
    }
}
