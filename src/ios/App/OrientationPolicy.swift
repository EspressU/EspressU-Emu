// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

enum OrientationPolicy {
    private static var inGame = false

    private static var isPhone: Bool { UIDevice.current.userInterfaceIdiom == .phone }

    static var mask: UIInterfaceOrientationMask {
        guard isPhone else { return .landscape }
        return inGame ? .landscape : .allButUpsideDown
    }

    static func setInGame(_ value: Bool) {
        guard inGame != value else { return }
        inGame = value
        apply()
    }

    static func apply() {
        guard isPhone else { return }
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        for scene in scenes {
            let target: UIInterfaceOrientationMask = inGame ? .landscape : .portrait
            if #available(iOS 16.0, *) {
                scene.windows.first?.rootViewController?.setNeedsUpdateOfSupportedInterfaceOrientations()
                if inGame ? scene.interfaceOrientation.isPortrait : scene.interfaceOrientation.isLandscape {
                    scene.requestGeometryUpdate(.iOS(interfaceOrientations: target)) { _ in }
                }
            } else if inGame ? scene.interfaceOrientation.isPortrait : scene.interfaceOrientation.isLandscape {
                let value: UIInterfaceOrientation = inGame ? .landscapeRight : .portrait
                UIDevice.current.setValue(value.rawValue, forKey: "orientation")
            }
        }
        if #unavailable(iOS 16.0) {
            UIViewController.attemptRotationToDeviceOrientation()
        }
    }
}

final class AppOrientationDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        OrientationPolicy.mask
    }
}
