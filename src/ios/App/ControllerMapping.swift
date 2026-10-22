import SwiftUI
import GameController

/// Which physical controller button drives each GamePad button. Only the changes are stored, so every button
/// that was never touched keeps following the built-in layout.
enum PadMapping {
    static let storageKey = "espressu.controls.padMapping"

    struct Slot: Identifiable {
        let pad: CemuBridgePadMapButton
        let name: String
        let label: String
        var id: Int { Int(pad.rawValue) }
    }

    static let slots: [Slot] = [
        Slot(pad: CEMU_BRIDGE_PAD_A, name: "A", label: "A"),
        Slot(pad: CEMU_BRIDGE_PAD_B, name: "B", label: "B"),
        Slot(pad: CEMU_BRIDGE_PAD_X, name: "X", label: "X"),
        Slot(pad: CEMU_BRIDGE_PAD_Y, name: "Y", label: "Y"),
        Slot(pad: CEMU_BRIDGE_PAD_L, name: "L", label: "L"),
        Slot(pad: CEMU_BRIDGE_PAD_R, name: "R", label: "R"),
        Slot(pad: CEMU_BRIDGE_PAD_ZL, name: "ZL", label: "ZL"),
        Slot(pad: CEMU_BRIDGE_PAD_ZR, name: "ZR", label: "ZR"),
        Slot(pad: CEMU_BRIDGE_PAD_PLUS, name: "Plus", label: "+"),
        Slot(pad: CEMU_BRIDGE_PAD_MINUS, name: "Minus", label: "\u{2212}"),
        Slot(pad: CEMU_BRIDGE_PAD_UP, name: "D-pad up", label: "\u{25B2}"),
        Slot(pad: CEMU_BRIDGE_PAD_DOWN, name: "D-pad down", label: "\u{25BC}"),
        Slot(pad: CEMU_BRIDGE_PAD_LEFT, name: "D-pad left", label: "\u{25C0}"),
        Slot(pad: CEMU_BRIDGE_PAD_RIGHT, name: "D-pad right", label: "\u{25B6}"),
        Slot(pad: CEMU_BRIDGE_PAD_STICK_L, name: "Left stick click", label: "L3"),
        Slot(pad: CEMU_BRIDGE_PAD_STICK_R, name: "Right stick click", label: "R3"),
    ]

    static func slot(_ pad: Int) -> Slot? { slots.first { $0.id == pad } }

    static let deviceNames = [
        "A", "B", "X", "Y", "Left bumper", "Right bumper", "Left trigger", "Right trigger",
        "Options", "Menu", "Left stick click", "Right stick click",
        "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    ]

    static func deviceName(_ device: Int) -> String {
        deviceNames.indices.contains(device) ? deviceNames[device] : "Unknown"
    }

    static func overrides() -> [Int: Int] {
        guard let stored = UserDefaults.standard.dictionary(forKey: storageKey) as? [String: Int] else { return [:] }
        var result: [Int: Int] = [:]
        for (key, value) in stored {
            if let pad = Int(key), slot(pad) != nil, deviceNames.indices.contains(value) { result[pad] = value }
        }
        return result
    }

    static func defaultDevice(for pad: Int) -> Int {
        Int(cemu_bridge_pad_mapping_default(Int32(pad)))
    }

    static func device(for pad: Int) -> Int {
        overrides()[pad] ?? defaultDevice(for: pad)
    }

    static func isRemapped(_ pad: Int) -> Bool { overrides()[pad] != nil }

    static func set(pad: Int, device: Int) {
        var stored = overrides()
        if device == defaultDevice(for: pad) { stored[pad] = nil } else { stored[pad] = device }
        save(stored)
        cemu_bridge_pad_mapping_set(Int32(pad), Int32(device == defaultDevice(for: pad) ? -1 : device))
    }

    static func reset(pad: Int) {
        var stored = overrides()
        stored[pad] = nil
        save(stored)
        cemu_bridge_pad_mapping_set(Int32(pad), -1)
    }

    static func resetAll() {
        UserDefaults.standard.removeObject(forKey: storageKey)
        cemu_bridge_pad_mapping_clear_all()
    }

    /// Hands the saved changes to the core, which applies them whenever a controller is wired up.
    static func pushToBridge() {
        for (pad, device) in overrides() { cemu_bridge_pad_mapping_set(Int32(pad), Int32(device)) }
    }

    private static func save(_ stored: [Int: Int]) {
        if stored.isEmpty {
            UserDefaults.standard.removeObject(forKey: storageKey)
        } else {
            UserDefaults.standard.set(Dictionary(uniqueKeysWithValues: stored.map { (String($0.key), $0.value) }), forKey: storageKey)
        }
    }

    /// The physical buttons currently held on the first connected controller, in the numbering the core uses.
    static func pressedDeviceButtons() -> Set<Int> {
        guard let pad = GCController.controllers().first(where: { $0.extendedGamepad != nil })?.extendedGamepad else { return [] }
        var pressed = Set<Int>()
        let readings: [(Int, Bool)] = [
            (0, pad.buttonA.isPressed), (1, pad.buttonB.isPressed), (2, pad.buttonX.isPressed), (3, pad.buttonY.isPressed),
            (4, pad.leftShoulder.isPressed), (5, pad.rightShoulder.isPressed),
            (6, pad.leftTrigger.isPressed), (7, pad.rightTrigger.isPressed),
            (8, pad.buttonOptions?.isPressed ?? false), (9, pad.buttonMenu.isPressed),
            (10, pad.leftThumbstickButton?.isPressed ?? false), (11, pad.rightThumbstickButton?.isPressed ?? false),
            (12, pad.dpad.up.isPressed), (13, pad.dpad.down.isPressed), (14, pad.dpad.left.isPressed), (15, pad.dpad.right.isPressed),
        ]
        for (device, down) in readings where down { pressed.insert(device) }
        return pressed
    }

    static var controllerName: String? {
        GCController.controllers().first(where: { $0.extendedGamepad != nil }).map { $0.vendorName ?? "Controller" }
    }
}

/// Waits for the next press on the physical controller while one GamePad button is being reassigned. The Settings
/// page shows its state; while it listens the menu ignores the controller, so the press is not taken as navigation.
final class ControllerMappingSession: ObservableObject {
    static let shared = ControllerMappingSession()
    static let timeout: TimeInterval = 10

    static let groupLayout: [(title: String, pads: [CemuBridgePadMapButton])] = [
        ("Face buttons", [CEMU_BRIDGE_PAD_A, CEMU_BRIDGE_PAD_B, CEMU_BRIDGE_PAD_X, CEMU_BRIDGE_PAD_Y]),
        ("Shoulders", [CEMU_BRIDGE_PAD_L, CEMU_BRIDGE_PAD_R, CEMU_BRIDGE_PAD_ZL, CEMU_BRIDGE_PAD_ZR]),
        ("D-pad", [CEMU_BRIDGE_PAD_UP, CEMU_BRIDGE_PAD_DOWN, CEMU_BRIDGE_PAD_LEFT, CEMU_BRIDGE_PAD_RIGHT]),
        ("Sticks", [CEMU_BRIDGE_PAD_STICK_L, CEMU_BRIDGE_PAD_STICK_R]),
        ("System", [CEMU_BRIDGE_PAD_PLUS, CEMU_BRIDGE_PAD_MINUS]),
    ]

    @Published private(set) var listeningPad: Int?
    @Published private(set) var secondsLeft: Double = 0
    @Published private(set) var controllerName: String? = PadMapping.controllerName
    @Published private(set) var revision = 0

    var isListening: Bool { listeningPad != nil }

    private var held = Set<Int>()
    private var started = Date()
    private var timer: Timer?

    private init() {
        for name in [Notification.Name.GCControllerDidConnect, .GCControllerDidDisconnect] {
            NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.controllersChanged()
            }
        }
    }

    func listen(for pad: Int) {
        guard controllerName != nil, PadMapping.slot(pad) != nil else { return }
        held = PadMapping.pressedDeviceButtons()
        started = Date()
        secondsLeft = Self.timeout
        listeningPad = pad
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.04, repeats: true) { [weak self] _ in self?.tick() }
        EspressUHaptics.tap()
    }

    func cancel() {
        timer?.invalidate()
        timer = nil
        if listeningPad != nil { listeningPad = nil }
    }

    func reset(pad: Int) {
        PadMapping.reset(pad: pad)
        EspressUHaptics.select()
        revision += 1
    }

    func resetAll() {
        cancel()
        PadMapping.resetAll()
        revision += 1
    }

    private func controllersChanged() {
        controllerName = PadMapping.controllerName
        if controllerName == nil { cancel() }
    }

    private func tick() {
        guard let pad = listeningPad else {
            cancel()
            return
        }
        let elapsed = Date().timeIntervalSince(started)
        secondsLeft = max(0, Self.timeout - elapsed)
        if elapsed > Self.timeout {
            cancel()
            return
        }
        let pressed = PadMapping.pressedDeviceButtons()
        held.formIntersection(pressed)
        if let next = pressed.subtracting(held).sorted().first {
            PadMapping.set(pad: pad, device: next)
            cancel()
            EspressUHaptics.tap()
            revision += 1
        }
    }
}
