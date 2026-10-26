// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import Combine
#if os(iOS)
import UIKit
import QuartzCore

enum DisplayLayoutSettings {
    static let swapKey = "espressu.display.swapTVPad"
    static let defaultSwap = false

    static let showSwapButtonKey = "espressu.display.showSwapButton"
    static let defaultShowSwapButton = true

    static let deviceShowsKey = "espressu.display.deviceShows"
    static let defaultDeviceShows = DeviceScreenMode.otherScreen
}

enum DeviceScreenMode: String, CaseIterable, Identifiable {
    case otherScreen
    case sameScreen
    case nothing

    var id: String { rawValue }

    var string: String {
        switch self {
        case .otherScreen: return "The other screen"
        case .sameScreen: return "The same screen as the TV"
        case .nothing: return "Nothing (controls only)"
        }
    }

    static var current: DeviceScreenMode {
        UserDefaults.standard.string(forKey: DisplayLayoutSettings.deviceShowsKey)
            .flatMap(DeviceScreenMode.init(rawValue:)) ?? DisplayLayoutSettings.defaultDeviceShows
    }
}

enum ExternalDisplaySystemSettings {
    static let enabledKey = "espressu.display.externalDisplaySystemEnabled"
    static let defaultEnabled = true
}

enum ScreenLayout: String, CaseIterable, Identifiable {
    case singleScreen
    case bothScreens
    case smallGamePadTopRight

    var id: String { rawValue }

    var string: String {
        switch self {
        case .singleScreen: return "Single Screen"
        case .bothScreens: return "Adaptive (Both Screens)"
        case .smallGamePadTopRight: return "Both Screens (GamePad Top Right)"
        }
    }

    var description: String {
        switch self {
        case .singleScreen:
            return "Shows one screen at a time. Tap the swap button to switch between TV and GamePad."
        case .bothScreens:
            return "Shows both screens: stacked in portrait, side by side in landscape."
        case .smallGamePadTopRight:
            return "Shows the TV screen with a small GamePad screen in the top-right corner."
        }
    }

    var showsBothScreens: Bool { self != .singleScreen }

    static var initialValue: ScreenLayout {
        UserDefaults.standard.string(forKey: LocalScreenLayoutSettings.layoutKey)
            .flatMap(ScreenLayout.init(rawValue:)) ?? .singleScreen
    }
}

enum LocalScreenLayoutSettings {
    static let layoutKey = "espressu.display.screenLayout"
    static let defaultLayout = ScreenLayout.singleScreen

    static let showSwapButtonKey = "espressu.display.showLocalSwapButton"
    static let defaultShowSwapButton = true
}

final class MetalLayerView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }

    private var boostHost: BrightnessBoostHost?

    override func layoutSubviews() {
        super.layoutSubviews()
        if !layer.isOpaque { layer.isOpaque = true }
        if boostHost == nil { boostHost = BrightnessBoostHost(view: self) }
        boostHost?.layout(in: self)
    }
}

@MainActor
final class DisplayRouter: ObservableObject {
    static let shared = DisplayRouter()

    enum Placement: Equatable {
        case deviceOnly
        case deviceMirrored
        case dualScreen
    }

    @Published private(set) var placement: Placement = .deviceOnly

    private var swapScreens: Bool {
        UserDefaults.standard.object(forKey: DisplayLayoutSettings.swapKey) as? Bool ?? DisplayLayoutSettings.defaultSwap
    }

    private var deviceMode: DeviceScreenMode { DeviceScreenMode.current }

    private var tvOnExternalDisplay: Bool {
        placement == .dualScreen && !(swapScreens && deviceMode == .otherScreen)
    }

    var gamePadTouchOnDevice: Bool {
        guard placement == .dualScreen else { return false }
        switch deviceMode {
        case .otherScreen: return !swapScreens
        case .sameScreen: return swapScreens
        case .nothing: return false
        }
    }

    private var tvRenderViewStorage: UIView?

    var tvRenderView: UIView {
        if let existing = tvRenderViewStorage {
            return existing
        }
        let view = MetalLayerView()
        view.backgroundColor = .black
        view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        tvRenderViewStorage = view
        return view
    }

    private var padRenderView: UIView?

    private(set) var padSurfaceScale: Double = 1.0

    private var localVisibleOutputs: (tv: Bool, pad: Bool)?

    private var sharedDeviceContainerStorage: UIView?

    private var sharedLocalPadContainerStorage: UIView?

    func sharedDeviceContainer() -> UIView {
        if let existing = sharedDeviceContainerStorage { return existing }
        let container = DeviceContainerView()
        container.backgroundColor = .black
        sharedDeviceContainerStorage = container
        return container
    }

    func sharedLocalPadContainer() -> UIView {
        if let existing = sharedLocalPadContainerStorage { return existing }
        let container = PadContainerView()
        container.backgroundColor = .black
        sharedLocalPadContainerStorage = container
        return container
    }

    private weak var deviceContainer: UIView?

    private weak var localPadContainer: UIView?

    private var screenLayout: ScreenLayout {
        (UserDefaults.standard.string(forKey: LocalScreenLayoutSettings.layoutKey))
            .flatMap(ScreenLayout.init(rawValue:)) ?? LocalScreenLayoutSettings.defaultLayout
    }

    private var lastLoggedOutputs: String?
    private var outputCheckGeneration = 0

    private var externalWindow: UIWindow?
    private var observing = false
    private var tvSurfaceRegistered = false

    private var isReparentingTV = false

    private var lastDeviceContainerLayoutSize: CGSize?

    private var pendingContainerResize: DispatchWorkItem?
    private var lastAppliedContainerResize: TimeInterval = 0

    private var lastLocalPadContainerLayoutSize: CGSize?

    private init() {}

    func startObserving() {
        guard !observing else { return }
        observing = true

        #if compiler(>=6.4)
        if #available(iOS 27.0, *) {
            ExternalDisplaySceneAccessory.registerIfNeeded()
        }
        #endif

        ThermalMonitor.shared.startObserving()

        let center = NotificationCenter.default
        var names: [Notification.Name] = [
            UIScreen.modeDidChangeNotification,
            UIScene.willConnectNotification,
            UIScene.didDisconnectNotification,
            UIScene.didActivateNotification,
        ]
        if #unavailable(iOS 16.0) {
            names += [UIScreen.didConnectNotification, UIScreen.didDisconnectNotification]
        }
        for name in names {
            center.addObserver(forName: name, object: nil, queue: .main) { note in
                let noteName = note.name
                Task { @MainActor in
                    DisplayRouter.shared.handleScreenChange(noteName)
                }
            }
        }

        log("display routing armed; \(describeScreens())")
    }

    func attach(deviceContainer container: UIView) {
        guard deviceContainer !== container else { return }
        deviceContainer = container
        applyPlacement(reason: "the emulator view mounted")
    }

    func registerSurfaces(with gameManager: GameManager) {
        guard !tvSurfaceRegistered else { return }

        let geometry = tvGeometry()

        guard gameManager.registerRenderSurface(
            uiView: tvRenderView,
            width: cInt(geometry.size.width),
            height: cInt(geometry.size.height),
            dpiScale: geometry.scale
        ) else { return }

        tvSurfaceRegistered = true
        applyPlacement(reason: "the TV surface was registered")
        let placed = tvGeometry()
        log("routing the Wii U TV surface to \(tvOnExternalDisplay ? "the external display" : "this device") at \(cInt(placed.size.width))x\(cInt(placed.size.height)) points, \(placed.scale)x scale (placement=\(placementName))")
    }

    func titleStopped() {
        tvRenderViewStorage?.removeFromSuperview()
        tvRenderViewStorage = nil
        padRenderView?.removeFromSuperview()
        padRenderView = nil
        externalWindow?.isHidden = true
        externalWindow = nil
        tvSurfaceRegistered = false
        localVisibleOutputs = nil
        lastLoggedOutputs = nil
        cemu_bridge_set_output_sources(false, false)
        lastDeviceContainerLayoutSize = nil
        lastLocalPadContainerLayoutSize = nil
        pendingContainerResize?.cancel()
        pendingContainerResize = nil
        lastAppliedContainerResize = 0
        ThermalMonitor.shared.titleStopped()
        log("title stopped; render surfaces will be rebuilt on the next launch")
    }

    private var placementName: String {
        switch placement {
        case .deviceOnly: return "deviceOnly"
        case .deviceMirrored: return "deviceMirrored"
        case .dualScreen: return "dualScreen"
        }
    }

    private func handleScreenChange(_ name: Notification.Name) {
        applyPlacement(reason: "\(name.rawValue); \(describeScreens())")
    }

    private func applyPlacement(reason: String) {
        let systemEnabled = UserDefaults.standard.object(forKey: ExternalDisplaySystemSettings.enabledKey) as? Bool
            ?? ExternalDisplaySystemSettings.defaultEnabled
        let external = systemEnabled ? externalScreen() : nil
        let scene = external.flatMap { externalWindowScene(for: $0) }

        let desired: Placement
        if external != nil && scene != nil {
            desired = .dualScreen
        } else if external != nil {
            desired = .deviceMirrored
        } else {
            desired = .deviceOnly
        }

        let changed = desired != placement
        placement = desired

        let tvGoesExternal = !(desired == .dualScreen && swapScreens && deviceMode == .otherScreen)

        switch desired {
        case .dualScreen:
            if let external, let scene {
                if tvGoesExternal {
                    placeTVOnExternalDisplay(screen: external, scene: scene)
                } else {
                    placeTVOnDevice(keepExternalWindow: true)
                }
            }
        case .deviceMirrored, .deviceOnly:
            placeTVOnDevice(keepExternalWindow: false)
        }

        if cemu_bridge_has_pad_render_surface() {
            let padIsLocal = padRenderView?.superview === localPadContainer
            let padShouldBeLocal = tvSurfaceRegistered && desired != .dualScreen
            if padIsLocal != padShouldBeLocal {
                cemu_bridge_release_pad_render_surface()
                padRenderView?.isHidden = true
                padRenderView = nil
            }
        }

        syncPadSurface(tvGoesExternal: tvGoesExternal, external: external, scene: scene)
        syncLocalPadSurface()
        applyOutputs()

        let outputs = describeOutputs()
        if changed || !tvSurfaceRegistered || outputs != lastLoggedOutputs {
            lastLoggedOutputs = outputs
            switch desired {
            case .dualScreen:
                log("display change (\(reason)) -> placement=dualScreen: \(outputs)")
                scheduleOutputCheck()
            case .deviceMirrored:
                log("display change (\(reason)) -> placement=deviceMirrored: an external display is connected but this app has no window scene for it, which is what AirPlay/screen mirroring looks like from inside the app. The Wii U TV screen stays on this device and reaches the external display through the mirror; the GamePad screen is not rendered. \(outputs)")
            case .deviceOnly:
                log("display change (\(reason)) -> placement=deviceOnly: \(outputs)")
            }
        }
    }

    private func applyOutputs() {
        guard placement == .dualScreen else {
            cemu_bridge_set_output_sources(false, false)
            return
        }
        let swapped = swapScreens
        let mode = deviceMode
        cemu_bridge_set_output_sources(swapped && mode != .otherScreen, !swapped && mode == .sameScreen)
        cemu_bridge_set_visible_outputs(true, cemu_bridge_has_pad_render_surface())
    }

    private func describeOutputs() -> String {
        let surfaces = "TV surface on \(tvSurfaceRegistered ? (tvOnExternalDisplay ? "the external display" : "this device") : "nothing yet"), "
            + "GamePad surface \(describePadSurface())"
        switch placement {
        case .deviceOnly:
            return "external display: none; this device: the Wii U screens per Screen Layout (\(screenLayout.rawValue)) [\(surfaces)]"
        case .deviceMirrored:
            return "external display: mirror of this device; this device: Wii U TV screen [\(surfaces)]"
        case .dualScreen:
            let swapped = swapScreens
            let external = swapped ? "Wii U GamePad screen" : "Wii U TV screen"
            let device: String
            switch deviceMode {
            case .otherScreen: device = swapped ? "Wii U TV screen" : "Wii U GamePad screen"
            case .sameScreen: device = external
            case .nothing: device = "no game picture, controls only"
            }
            return "external display shows the \(external); this device shows \(device) (swapped=\(swapped), device mode=\(deviceMode.rawValue)) [\(surfaces)]"
        }
    }

    private func describePadSurface() -> String {
        guard cemu_bridge_has_pad_render_surface() else { return "none" }
        let host: String
        if let view = padRenderView?.superview {
            host = (view === deviceContainer || view === localPadContainer) ? "on this device" : "on the external display"
        } else {
            host = "unattached"
        }
        return "\(host), layer active=\(cemu_bridge_pad_layer_active())"
    }

    private func scheduleOutputCheck() {
        guard placement == .dualScreen, tvSurfaceRegistered, cemu_bridge_has_pad_render_surface() else { return }
        outputCheckGeneration += 1
        let generation = outputCheckGeneration
        let presentsBefore = cemu_bridge_pad_present_count()
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.outputCheckGeneration == generation,
                  self.placement == .dualScreen, cemu_bridge_has_pad_render_surface() else { return }
            if !cemu_bridge_pad_layer_active() {
                cemu_bridge_ensure_pad_layer()
                self.log("check: the GamePad surface was registered but had no layer behind it, so nothing could draw to it; gave it one (layer active now: \(cemu_bridge_pad_layer_active()))")
            }
            let presented = cemu_bridge_pad_present_count() &- presentsBefore
            self.log("check, 2 s after the change: GamePad surface \(self.describePadSurface()), \(presented) frames presented to it")
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0, execute: work)
    }

    func reapplyRenderScale(reason: String) {
        resizeTVSurfaceIfRegistered()
        resizePadSurfaceIfRegistered()
        log("render scale re-applied: \(reason)")
    }

    func rerouteForScreenLayoutChange() {
        guard placement == .dualScreen else { return }
        if cemu_bridge_has_pad_render_surface() {
            cemu_bridge_release_pad_render_surface()
            padRenderView?.isHidden = true
            padRenderView = nil
        }
        applyPlacement(reason: "screen layout changed")
    }

    func reapplyForExternalDisplaySystemToggle() {
        applyPlacement(reason: "external display system toggled")
    }

    func toggleScreenLayoutFromSwapButton() {
        UserDefaults.standard.set(!swapScreens, forKey: DisplayLayoutSettings.swapKey)
        rerouteForScreenLayoutChange()
    }

    private func placeTVOnDevice(keepExternalWindow: Bool) {
        guard let container = deviceContainer else { return }
        guard let tvRenderView = tvRenderViewStorage else { return }
        if tvRenderView.superview !== container {
            isReparentingTV = true
            tvRenderView.removeFromSuperview()
            tvRenderView.frame = container.bounds
            container.addSubview(tvRenderView)
            isReparentingTV = false
            resizeTVSurfaceIfRegistered()
        }
        if !keepExternalWindow, let externalWindow {
            externalWindow.isHidden = true
            self.externalWindow = nil
        }
    }

    private func placeTVOnExternalDisplay(screen: UIScreen, scene: UIWindowScene) {
        guard let host = externalDisplayHost(screen: screen, scene: scene) else { return }
        guard let tvRenderView = tvRenderViewStorage else { return }
        if tvRenderView.superview !== host {
            isReparentingTV = true
            tvRenderView.removeFromSuperview()
            tvRenderView.frame = host.bounds
            host.addSubview(tvRenderView)
            isReparentingTV = false
            resizeTVSurfaceIfRegistered()
        }
    }

    private func placePadOnExternalDisplay(view: UIView, screen: UIScreen, scene: UIWindowScene) {
        guard let host = externalDisplayHost(screen: screen, scene: scene) else { return }
        if view.superview !== host {
            host.addSubview(view)
        }
    }

    private func externalDisplayHost(screen: UIScreen, scene: UIWindowScene) -> UIView? {
        if externalWindow?.screen !== screen {
            externalWindow?.isHidden = true
            externalWindow = nil
        }
        if externalWindow == nil {
            let window = UIWindow(windowScene: scene)
            window.frame = screen.bounds
            window.backgroundColor = .black
            let root = UIViewController()
            root.view.backgroundColor = .black
            window.rootViewController = root
            window.isHidden = false
            externalWindow = window
        }
        return externalWindow?.rootViewController?.view
    }

    func deviceContainerDidLayout(_ container: UIView) {
        guard container === deviceContainer else { return }
        guard !isReparentingTV else { return }
        let size = container.bounds.size
        if let lastSize = lastDeviceContainerLayoutSize, lastSize == size { return }
        lastDeviceContainerLayoutSize = size

        guard PlatformCapabilities.expectsContinuousIPadResize else {
            applyContainerResize()
            return
        }
        throttleContainerResize()
    }

    private static let continuousResizeInterval: TimeInterval = 0.125

    private func throttleContainerResize() {
        let now = CACurrentMediaTime()
        pendingContainerResize?.cancel()
        pendingContainerResize = nil

        if now - lastAppliedContainerResize >= Self.continuousResizeInterval {
            lastAppliedContainerResize = now
            applyContainerResize()
            return
        }

        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.pendingContainerResize = nil
            self.lastAppliedContainerResize = CACurrentMediaTime()
            if let container = self.deviceContainer {
                self.lastDeviceContainerLayoutSize = container.bounds.size
            }
            self.applyContainerResize()
        }
        pendingContainerResize = work
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.continuousResizeInterval, execute: work)
    }

    private func applyContainerResize() {
        resizeTVSurfaceIfRegistered()
        resizePadSurfaceIfRegistered()
    }

    private func resizeTVSurfaceIfRegistered() {
        guard tvSurfaceRegistered else { return }
        if let container = deviceContainer, let tvRenderView = tvRenderViewStorage,
           tvRenderView.superview === container {
            tvRenderView.frame = container.bounds
        }
        let geometry = tvGeometry()
        cemu_bridge_resize_render_surface(
            cInt(geometry.size.width),
            cInt(geometry.size.height),
            geometry.scale,
            true
        )
    }

    private func resizePadSurfaceIfRegistered() {
        guard cemu_bridge_has_pad_render_surface() else { return }
        if let host = padRenderView?.superview {
            padRenderView?.frame = host.bounds
        }
        let geometry = padGeometry()
        if geometry.scale.isFinite, geometry.scale > 0,
           let layer = padRenderView?.layer as? CAMetalLayer,
           abs(Double(layer.contentsScale) - geometry.scale) > 0.001 {
            layer.contentsScale = CGFloat(geometry.scale)
        }
        cemu_bridge_resize_render_surface(
            cInt(geometry.size.width),
            cInt(geometry.size.height),
            geometry.scale,
            false
        )
    }

    private func padGeometry() -> (size: CGSize, scale: Double) {
        guard let host = padRenderView?.superview else {
            let size = UIScreen.main.bounds.size
            let scale = PadSurfaceScale.scale(forPoints: size, renderScale: UIScreen.main.effectiveRenderScale)
            padSurfaceScale = scale
            return (size, scale)
        }
        let size = host.bounds.size == .zero ? UIScreen.main.bounds.size : host.bounds.size
        let renderScale = (host.window?.screen ?? UIScreen.main).effectiveRenderScale
        let scale = PadSurfaceScale.scale(forPoints: size, renderScale: renderScale)
        padSurfaceScale = scale
        return (size, scale)
    }

    private func syncPadSurface(tvGoesExternal: Bool, external: UIScreen?, scene: UIWindowScene?) {
        guard tvSurfaceRegistered else { return }
        let wantPad = (placement == .dualScreen && deviceMode != .nothing)
        let padGoesExternal = wantPad && !tvGoesExternal
        var havePad = cemu_bridge_has_pad_render_surface()

        if wantPad, havePad {
            let expected: UIView? = padGoesExternal ? externalWindow : deviceContainer
            let placed = padRenderView.map { view in expected.map { view.isDescendant(of: $0) } ?? false } ?? false
            if !placed {
                cemu_bridge_release_pad_render_surface()
                padRenderView?.removeFromSuperview()
                padRenderView = nil
                havePad = false
            }
        }

        if wantPad, !havePad {
            let view = MetalLayerView()
            view.backgroundColor = .black

            if padGoesExternal {
                guard let external, let scene else { return }
                placePadOnExternalDisplay(view: view, screen: external, scene: scene)
                view.frame = externalWindow?.bounds ?? .zero
            } else {
                guard let container = deviceContainer else { return }
                view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
                view.frame = container.bounds
                container.addSubview(view)
            }
            padRenderView = view

            let surface = Unmanaged.passRetained(view).toOpaque()
            let geometry = padGeometry()
            cemu_bridge_register_pad_render_surface(surface, cInt(geometry.size.width), cInt(geometry.size.height), geometry.scale)
        } else if !wantPad, havePad {
            cemu_bridge_release_pad_render_surface()
            padRenderView?.isHidden = true
            padRenderView = nil
        }
    }

    func attachLocalPadContainer(_ container: UIView) {
        guard localPadContainer !== container else { return }
        localPadContainer = container
        syncLocalPadSurface()
    }

    func localPadContainerDidLayout(_ container: UIView) {
        guard container === localPadContainer else { return }
        let size = container.bounds.size
        if let lastSize = lastLocalPadContainerLayoutSize, lastSize == size { return }
        lastLocalPadContainerLayoutSize = size
        resizePadSurfaceIfRegistered()
    }

    private func syncLocalPadSurface() {
        let wantLocalPad = tvSurfaceRegistered && placement != .dualScreen
        let havePad = cemu_bridge_has_pad_render_surface()

        if wantLocalPad, !havePad, let container = localPadContainer {
            let view = MetalLayerView()
            view.backgroundColor = .black
            view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            view.frame = container.bounds
            container.addSubview(view)
            padRenderView = view

            let surface = Unmanaged.passRetained(view).toOpaque()
            let geometry = padGeometry()
            cemu_bridge_register_pad_render_surface(surface, cInt(geometry.size.width), cInt(geometry.size.height), geometry.scale)
            let visible = localVisibleOutputs ?? (tv: true, pad: false)
            cemu_bridge_set_visible_outputs(visible.tv, visible.pad)
        } else if !wantLocalPad, havePad, padRenderView?.superview === localPadContainer {
            cemu_bridge_release_pad_render_surface()
            padRenderView?.isHidden = true
            padRenderView = nil
        }
    }

    func updateLocalVisibleOutputs(showTV: Bool, showPad: Bool) {
        guard placement != .dualScreen else { return }
        localVisibleOutputs = (tv: showTV, pad: showPad)
        if !showPad { cemu_bridge_release_all_buttons() }
        cemu_bridge_set_visible_outputs(showTV, showPad)
    }

    private var connectedScreens: [UIScreen] {
        if #available(iOS 16.0, *) {
            var screens = [UIScreen.main]
            for case let scene as UIWindowScene in UIApplication.shared.connectedScenes {
                if !screens.contains(where: { $0 === scene.screen }) { screens.append(scene.screen) }
            }
            return screens
        } else {
            return UIScreen.screens
        }
    }

    private func externalScreen() -> UIScreen? {
        connectedScreens.first { $0 !== UIScreen.main }
    }

    func externalSceneDidChange(reason: String) {
        applyPlacement(reason: reason)
    }

    private func externalWindowScene(for screen: UIScreen) -> UIWindowScene? {
        UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first { $0.screen === screen }
    }

    private func tvGeometry() -> (size: CGSize, scale: Double) {
        if tvOnExternalDisplay, let window = externalWindow {
            return (window.bounds.size, window.screen.effectiveRenderScale)
        }
        let containerSize = deviceContainer?.bounds.size ?? .zero
        let size = containerSize == .zero ? UIScreen.main.bounds.size : containerSize
        let scale = (deviceContainer?.window?.screen ?? UIScreen.main).effectiveRenderScale
        return (size, scale)
    }

    private func describeScreens() -> String {
        let parts = connectedScreens.map { screen -> String in
            let role = screen === UIScreen.main ? "main" : (screen.mirrored != nil ? "external (mirroring this device)" : "external")
            return "\(role) \(Int(screen.bounds.width))x\(Int(screen.bounds.height))@\(screen.scale)x"
        }
        return "screens: [\(parts.joined(separator: ", "))]"
    }

    private func log(_ message: String) {
        cemu_bridge_log_line("iOS display: " + message)
    }
}
#endif

func cInt(_ value: CGFloat) -> Int32 {
    guard value.isFinite else { return 0 }
    return Int32(max(0, min(value, 100_000)))
}
