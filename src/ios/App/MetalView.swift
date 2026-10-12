// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import MetalKit
#if os(iOS)
import UIKit
#endif

final class DeviceContainerView: UIView {
    override func layoutSubviews() {
        super.layoutSubviews()
        DisplayRouter.shared.deviceContainerDidLayout(self)
    }
}

struct MetalViewIOS: UIViewRepresentable {
    var gameManager: GameManager

    func makeUIView(context: Context) -> UIView {
        let container = DisplayRouter.shared.sharedDeviceContainer()

        DisplayRouter.shared.startObserving()
        DisplayRouter.shared.attach(deviceContainer: container)
        DisplayRouter.shared.registerSurfaces(with: gameManager)

        return container
    }

    func updateUIView(_ uiView: UIView, context: Context) {
        DisplayRouter.shared.attach(deviceContainer: uiView)
        DisplayRouter.shared.registerSurfaces(with: gameManager)
    }
}

final class PadContainerView: UIView {
    override func layoutSubviews() {
        super.layoutSubviews()
        DisplayRouter.shared.localPadContainerDidLayout(self)
    }
}

struct PadMetalViewIOS: UIViewRepresentable {
    func makeUIView(context: Context) -> UIView {
        let container = DisplayRouter.shared.sharedLocalPadContainer()
        DisplayRouter.shared.attachLocalPadContainer(container)
        return container
    }

    func updateUIView(_ uiView: UIView, context: Context) {
        DisplayRouter.shared.attachLocalPadContainer(uiView)
    }
}
