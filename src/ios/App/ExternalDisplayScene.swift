// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
#if os(iOS)
import UIKit
#endif

#if os(iOS)

#if compiler(>=6.4)

@available(iOS 27.0, *)
final class ExternalDisplaySceneDelegate: UIResponder, UIWindowSceneDelegate {
    func scene(_ scene: UIScene,
              willConnectTo session: UISceneSession,
              options connectionOptions: UIScene.ConnectionOptions) {
        DisplayRouter.shared.externalSceneDidChange(reason: "external scene connected")
    }

    func sceneDidDisconnect(_ scene: UIScene) {
        DisplayRouter.shared.externalSceneDidChange(reason: "external scene disconnected")
    }
}

@available(iOS 27.0, *)
@MainActor
enum ExternalDisplaySceneAccessory {

    private static var registration: UISceneAccessoryRegistration?

    private static var isRegistered: Bool { registration != nil }

    static func registerIfNeeded() {
        guard !isRegistered else { return }
        guard let host = rootViewController() else { return }

        let configuration = UISceneConfiguration(
            name: "EspressU External Display",
            sessionRole: .windowExternalDisplayNonInteractive)
        configuration.delegateClass = ExternalDisplaySceneDelegate.self

        let accessory = UISceneAccessory.externalNonInteractive(sceneConfiguration: configuration)
        registration = host.registerSceneAccessory(accessory)
        cemu_bridge_log_line("iOS display: registered a non-interactive external-display scene accessory (iOS 27+)")
    }

    private static func rootViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        let active = scenes.first { $0.activationState == .foregroundActive } ?? scenes.first
        return active?.windows.first(where: { $0.isKeyWindow })?.rootViewController
            ?? active?.windows.first?.rootViewController
    }
}

#endif
#endif
