// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import GameController
import QuartzCore
import UIKit
import UIKit.UIGestureRecognizerSubclass

enum ControllerMenuCommand: Equatable {
    case up, down, left, right
    case confirm
    case back
    case previousPage, nextPage
    case menu
    case alternate
    case select
}

@MainActor
final class ControllerMenuInput: ObservableObject {
    @Published private(set) var isConnected = ControllerPresence.isConnected()
    @Published private(set) var showsFocus = ControllerPresence.isConnected() && ControllerTouchWatcher.padInUse
    @Published private(set) var controllers: [GCController] = []
    @Published private(set) var lastPressedSymbol: String?

    var onCommand: ((ControllerMenuCommand) -> Void)?
    var isBlocked: (() -> Bool)?
    var everyFrame: (() -> Void)?
    var isExclusive = false
    private(set) static var exclusiveCount = 0
    private var countedExclusive = false

    private var link: CADisplayLink?
    private var held: [ControllerMenuCommand: CFTimeInterval] = [:]
    private var repeatAt: [ControllerMenuCommand: CFTimeInterval] = [:]
    private var otherHeld: Set<String> = []
    private var pausedUntilRelease = false
    private var paused = false
    private var observers: [NSObjectProtocol] = []

    private let firstRepeat: CFTimeInterval = 0.38
    private let nextRepeat: CFTimeInterval = 0.11
    private let stickThreshold: Float = 0.55

    func start() {
        guard link == nil else { return }
        if isExclusive && !countedExclusive {
            countedExclusive = true
            Self.exclusiveCount += 1
        }
        refreshControllers()
        ControllerTouchWatcher.installIfNeeded()
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: ControllerTouchWatcher.changed, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.updateShowsFocus() }
        })
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                let departing = name == .GCControllerDidDisconnect ? note.object as? GCController : nil
                Task { @MainActor in self?.refreshControllers(excluding: departing) }
            })
        }
        Self.running += 1
        cemu_bridge_log_checkpoint("ControllerMenuInput: start (running \(Self.running), exclusive \(isExclusive))")
        let link = CADisplayLink(target: DisplayLinkProxy(self), selector: #selector(DisplayLinkProxy.tick))
        link.add(to: .main, forMode: .common)
        self.link = link
        pausedUntilRelease = true
    }

    private static var running = 0

    func stop() {
        if link != nil { Self.running -= 1 }
        if countedExclusive {
            countedExclusive = false
            Self.exclusiveCount -= 1
        }
        link?.invalidate()
        link = nil
        observers.forEach(NotificationCenter.default.removeObserver)
        observers.removeAll()
        held.removeAll()
        repeatAt.removeAll()
    }

    func pause() { paused = true }
    func resume() {
        paused = false
        pausedUntilRelease = true
    }

    private func refreshControllers(excluding departing: GCController? = nil) {
        controllers = GCController.controllers().filter { $0 !== departing && $0.extendedGamepad != nil }
        isConnected = !controllers.isEmpty
        updateShowsFocus()
    }

    private func updateShowsFocus() {
        let value = isConnected && ControllerTouchWatcher.padInUse
        if showsFocus != value { showsFocus = value }
    }

    fileprivate func tick() {
        everyFrame?()
        guard !paused else { return }
        if isBlocked?() == true {
            pausedUntilRelease = true
            held.removeAll()
            repeatAt.removeAll()
            return
        }
        var pressed: [ControllerMenuCommand: String] = [:]
        var otherPressed: Set<String> = []
        for controller in controllers {
            guard let pad = controller.extendedGamepad else { continue }
            func mark(_ command: ControllerMenuCommand, _ element: GCControllerElement, _ on: Bool) {
                if on, pressed[command] == nil { pressed[command] = element.sfSymbolsName ?? "circle.fill" }
            }
            let stick = pad.leftThumbstick
            mark(.up, pad.dpad, pad.dpad.up.isPressed || stick.yAxis.value > stickThreshold)
            mark(.down, pad.dpad, pad.dpad.down.isPressed || stick.yAxis.value < -stickThreshold)
            mark(.left, pad.dpad, pad.dpad.left.isPressed || stick.xAxis.value < -stickThreshold)
            mark(.right, pad.dpad, pad.dpad.right.isPressed || stick.xAxis.value > stickThreshold)
            mark(.confirm, pad.buttonA, pad.buttonA.isPressed)
            mark(.back, pad.buttonB, pad.buttonB.isPressed)
            mark(.previousPage, pad.leftShoulder, pad.leftShoulder.isPressed)
            mark(.nextPage, pad.rightShoulder, pad.rightShoulder.isPressed)
            mark(.menu, pad.buttonMenu, pad.buttonMenu.isPressed)
            mark(.alternate, pad.buttonY, pad.buttonY.isPressed)
            if let options = pad.buttonOptions { mark(.select, options, options.isPressed) }
            for element in [pad.buttonX, pad.leftTrigger, pad.rightTrigger] where element.isPressed {
                otherPressed.insert(element.sfSymbolsName ?? "circle.fill")
            }
        }
        for symbol in otherPressed.subtracting(otherHeld) { lastPressedSymbol = symbol }
        otherHeld = otherPressed

        if pausedUntilRelease {
            if pressed.isEmpty && otherPressed.isEmpty { pausedUntilRelease = false }
            held.removeAll()
            return
        }

        let now = CACurrentMediaTime()
        for command in held.keys where pressed[command] == nil {
            held[command] = nil
            repeatAt[command] = nil
        }
        for (command, symbol) in pressed {
            if held[command] == nil {
                held[command] = now
                repeatAt[command] = now + firstRepeat
                lastPressedSymbol = symbol
                if !ControllerTouchWatcher.padInUse {
                    ControllerTouchWatcher.setPadInUse(true)
                    continue
                }
                onCommand?(command)
            } else if Self.repeats(command), let due = repeatAt[command], now >= due {
                repeatAt[command] = now + nextRepeat
                onCommand?(command)
            }
        }
    }

    private static func repeats(_ command: ControllerMenuCommand) -> Bool {
        switch command {
        case .up, .down, .left, .right: return true
        default: return false
        }
    }
}

@MainActor
enum ControllerTouchWatcher {
    static let changed = Notification.Name("espressu.controller.touchModeChanged")
    private(set) static var padInUse = true

    static func setPadInUse(_ value: Bool) {
        guard padInUse != value else { return }
        padInUse = value
        NotificationCenter.default.post(name: changed, object: nil)
    }

    static func installIfNeeded() {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
        for window in windows where !(window.gestureRecognizers ?? []).contains(where: { $0 is TouchSpy }) {
            window.addGestureRecognizer(TouchSpy())
        }
    }
}

private final class TouchSpy: UIGestureRecognizer, UIGestureRecognizerDelegate {
    init() {
        super.init(target: nil, action: nil)
        cancelsTouchesInView = false
        delaysTouchesBegan = false
        delaysTouchesEnded = false
        delegate = self
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        Task { @MainActor in ControllerTouchWatcher.setPadInUse(false) }
        state = .failed
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
        true
    }
}

private final class DisplayLinkProxy {
    weak var input: ControllerMenuInput?
    init(_ input: ControllerMenuInput) { self.input = input }
    @MainActor @objc func tick() { input?.tick() }
}

extension ControllerMenuInput {
    static var somethingPresented: Bool {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
        let window = windows.first { $0.isKeyWindow } ?? windows.first
        return window?.rootViewController?.presentedViewController != nil
    }
}

struct ControllerFocusGrid {
    var rows: [[String]]

    func move(_ id: String?, _ command: ControllerMenuCommand) -> String? {
        let rows = self.rows.filter { !$0.isEmpty }
        guard !rows.isEmpty else { return nil }
        guard let id, let r = rows.firstIndex(where: { $0.contains(id) }),
              let c = rows[r].firstIndex(of: id) else { return rows.last?.last }
        switch command {
        case .left: return rows[r][max(0, c - 1)]
        case .right: return rows[r][min(rows[r].count - 1, c + 1)]
        case .up, .down:
            let target = command == .up ? r - 1 : r + 1
            guard rows.indices.contains(target) else { return id }
            let fraction = rows[r].count > 1 ? Double(c) / Double(rows[r].count - 1) : 0.5
            let column = Int((fraction * Double(rows[target].count - 1)).rounded())
            return rows[target][min(max(column, 0), rows[target].count - 1)]
        default: return id
        }
    }
}

extension View {
    func controllerFocusRing<S: Shape>(_ focused: Bool, shape: S) -> some View {
        self
            .overlay(
                shape
                    .stroke(EspressUTheme.pixelBlue, lineWidth: 3)
                    .padding(-5)
                    .opacity(focused ? 1 : 0)
            )
            .scaleEffect(focused ? 1.02 : 1)
            .animation(.spring(response: 0.25, dampingFraction: 0.8), value: focused)
    }
}

struct ControllerButtonHint: View {
    let symbol: String
    let text: String

    var body: some View {
        HStack(spacing: 5) {
            Image(systemName: symbol)
                .font(.system(size: 17, weight: .semibold))
            Text(text)
                .font(.system(size: 14, weight: .semibold, design: .rounded))
        }
        .foregroundColor(EspressUTheme.brownMid)
        .accessibilityHidden(true)
    }
}

extension ControllerMenuInput {
    var confirmSymbol: String { controllers.first?.extendedGamepad?.buttonA.sfSymbolsName ?? "a.circle" }
    var backSymbol: String { controllers.first?.extendedGamepad?.buttonB.sfSymbolsName ?? "b.circle" }
    // The system's own glyphs for Menu and View/Options come out as a bare oval on some pads, so these two are fixed.
    var menuSymbol: String { "line.3.horizontal.circle" }
    var selectSymbol: String { "minus.circle" }
    var leftShoulderSymbol: String { controllers.first?.extendedGamepad?.leftShoulder.sfSymbolsName ?? "l1.rectangle.roundedbottom" }
    var rightShoulderSymbol: String { controllers.first?.extendedGamepad?.rightShoulder.sfSymbolsName ?? "r1.rectangle.roundedbottom" }
}
