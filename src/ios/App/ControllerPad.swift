// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Dispatch

struct OptimizedControlPanel: View {
    let skin: WiiUControllerSkin
    let onInput: (String, Bool) -> Void
    let onStick: (Int, CGPoint) -> Void
    @Binding var isEditingLayout: Bool
    var isPaused: Bool = false
    var topInset: CGFloat = 0
    var portrait: Bool = false

    @AppStorage(ControllerLayoutSettings.scaleKey)
    private var userScale = ControllerLayoutSettings.defaultScale
    @AppStorage(ControllerLayoutSettings.opacityKey)
    private var padOpacity = ControllerLayoutSettings.defaultOpacity
    @AppStorage(ControllerLayoutSettings.leftOffsetXKey) private var leftOffsetX = 0.0
    @AppStorage(ControllerLayoutSettings.leftOffsetYKey) private var leftOffsetY = 0.0
    @AppStorage(ControllerLayoutSettings.rightOffsetXKey) private var rightOffsetX = 0.0
    @AppStorage(ControllerLayoutSettings.rightOffsetYKey) private var rightOffsetY = 0.0
    @AppStorage(ControllerLayoutSettings.rightStickOffsetXKey) private var rightStickOffsetX = 0.0
    @AppStorage(ControllerLayoutSettings.rightStickOffsetYKey) private var rightStickOffsetY = 0.0
    @AppStorage(ControllerLayoutSettings.leftStickOffsetXKey) private var leftStickOffsetX = 0.0
    @AppStorage(ControllerLayoutSettings.leftStickOffsetYKey) private var leftStickOffsetY = 0.0
    @AppStorage(ControllerLayoutSettings.stickSpacingKey)
    private var stickSpacing = ControllerLayoutSettings.defaultStickSpacing
    @AppStorage(ControllerLayoutSettings.shoulderOffsetKey)
    private var shoulderOffset = ControllerLayoutSettings.defaultShoulderOffset
    @AppStorage(ControllerLayoutSettings.comfortControlsKey)
    private var comfortControls = ControllerLayoutSettings.defaultComfortControls
    @AppStorage(ControllerLayoutSettings.joystickKey)
    private var joystickMode = ControllerLayoutSettings.defaultJoystick
    @AppStorage(ControllerLayoutSettings.individualEditModeKey)
    private var individualEditMode = ControllerLayoutSettings.defaultIndividualEditMode

    private func layoutControls(_ controls: [ControllerGeometry.Control]) -> [ControllerGeometry.Control] {
        portrait ? ControllerGeometry.Portrait.cluster(controls) : controls
    }

    init(skin: WiiUControllerSkin,
         onInput: @escaping (String, Bool) -> Void,
         onStick: @escaping (Int, CGPoint) -> Void,
         isEditingLayout: Binding<Bool>,
         isPaused: Bool = false,
         topInset: CGFloat = 0,
         portrait: Bool = false) {
        self.skin = skin
        self.onInput = onInput
        self.onStick = onStick
        self._isEditingLayout = isEditingLayout
        self.isPaused = isPaused
        self.topInset = topInset
        self.portrait = portrait
        func key(_ key: String) -> String { portrait ? ControllerLayoutSettings.portraitKey(key) : key }
        _leftOffsetX = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.leftOffsetXKey))
        _leftOffsetY = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.leftOffsetYKey))
        _rightOffsetX = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.rightOffsetXKey))
        _rightOffsetY = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.rightOffsetYKey))
        _rightStickOffsetX = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.rightStickOffsetXKey))
        _rightStickOffsetY = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.rightStickOffsetYKey))
        _leftStickOffsetX = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.leftStickOffsetXKey))
        _leftStickOffsetY = AppStorage(wrappedValue: 0.0, key(ControllerLayoutSettings.leftStickOffsetYKey))
    }

    var body: some View {
        GeometryReader { proxy in
            let unit = portrait
                ? ControllerGeometry.Portrait.diameter(in: proxy.size, joystick: joystickMode) * CGFloat(min(userScale, 1))
                : ControllerGeometry.automaticDiameter(in: proxy.size) * CGFloat(userScale)

            let comfortActive = comfortControls && joystickMode && !portrait
            let leftStickControls = comfortActive ? ControllerGeometry.leftStickClusterComfort : ControllerGeometry.leftStickCluster
            let rightStickControls = comfortActive ? ControllerGeometry.rightStickClusterComfort : ControllerGeometry.rightStickCluster
            let stickShift = portrait ? 0 : ControllerGeometry.stickShift(
                spacing: stickSpacing, containerWidth: proxy.size.width, unit: unit,
                left: leftStickControls, right: rightStickControls)
            let individualEdit = individualEditMode && !portrait
            let leftStickAnchor = portrait ? ControllerGeometry.Portrait.stickAnchorOffset : CGPoint(
                x: ControllerGeometry.leftStickAnchorOffset.x - stickShift,
                y: ControllerGeometry.leftStickAnchorOffset.y)
            let rightStickAnchor = portrait ? ControllerGeometry.Portrait.stickAnchorOffset : CGPoint(
                x: ControllerGeometry.rightStickAnchorOffset.x + stickShift,
                y: ControllerGeometry.rightStickAnchorOffset.y)

            let shoulderDrop = ControllerLayoutSettings.effectiveShoulderOffset(shoulderOffset)
            let topReserve = max(0, topInset - proxy.frame(in: .global).minY)

            ZStack(alignment: .topLeading) {
                ControlCluster(
                    controls: layoutControls(comfortActive ? ControllerGeometry.leftClusterComfort : ControllerGeometry.leftCluster),
                    edge: .leading,
                    portrait: portrait,
                    skin: skin,
                    unit: unit,
                    shoulderOffset: shoulderDrop,
                    topReserve: topReserve,
                    container: proxy.size,
                    isEditingLayout: isEditingLayout,
                    individualEditMode: individualEdit,
                    offsetX: $leftOffsetX,
                    offsetY: $leftOffsetY,
                    onInput: onInput,
                    onStick: { onStick(0, $0) }
                )

                if joystickMode {
                    ControlCluster(
                        controls: leftStickControls,
                        edge: .leading,
                        portrait: portrait,
                        anchorOffset: leftStickAnchor,
                        skin: skin,
                        unit: unit,
                        shoulderOffset: shoulderDrop,
                        topReserve: topReserve,
                        container: proxy.size,
                        isEditingLayout: isEditingLayout,
                        individualEditMode: individualEdit,
                        offsetX: $leftStickOffsetX,
                        offsetY: $leftStickOffsetY,
                        onInput: onInput,
                        onStick: { onStick(0, $0) }
                    )
                }

                ControlCluster(
                    controls: layoutControls(comfortActive ? ControllerGeometry.rightClusterComfort : ControllerGeometry.rightCluster),
                    edge: .trailing,
                    portrait: portrait,
                    skin: skin,
                    unit: unit,
                    shoulderOffset: shoulderDrop,
                    topReserve: topReserve,
                    container: proxy.size,
                    isEditingLayout: isEditingLayout,
                    individualEditMode: individualEdit,
                    offsetX: $rightOffsetX,
                    offsetY: $rightOffsetY,
                    onInput: onInput,
                    onStick: { onStick(1, $0) }
                )

                if joystickMode {
                    ControlCluster(
                        controls: rightStickControls,
                        edge: .trailing,
                        portrait: portrait,
                        anchorOffset: rightStickAnchor,
                        skin: skin,
                        unit: unit,
                        shoulderOffset: shoulderDrop,
                        topReserve: topReserve,
                        container: proxy.size,
                        isEditingLayout: isEditingLayout,
                        individualEditMode: individualEdit,
                        offsetX: $rightStickOffsetX,
                        offsetY: $rightStickOffsetY,
                        onInput: onInput,
                        onStick: { onStick(1, $0) }
                    )
                }
            }
            .opacity(isEditingLayout ? 1.0 : max(padOpacity, 0.15))
        }
        .onDisappear { cemu_bridge_release_all_buttons() }
        .allowsHitTesting(!isPaused)
        .onChange(of: joystickMode) { _ in cemu_bridge_release_all_buttons() }
        .onChange(of: comfortControls) { _ in cemu_bridge_release_all_buttons() }
    }
}

private struct ControlCluster: View {
    let controls: [ControllerGeometry.Control]
    let edge: HorizontalEdge
    var portrait: Bool = false
    var anchorOffset: CGPoint = .zero
    let skin: WiiUControllerSkin
    let unit: CGFloat
    let shoulderOffset: Double
    var topReserve: CGFloat = 0
    let container: CGSize
    let isEditingLayout: Bool
    let individualEditMode: Bool
    @Binding var offsetX: Double
    @Binding var offsetY: Double
    let onInput: (String, Bool) -> Void
    let onStick: (CGPoint) -> Void

    enum HorizontalEdge { case leading, trailing }

    @State private var dragOrigin: CGSize?

    private var box: CGRect { ControllerGeometry.bounds(of: controls) }

    private var anchor: CGPoint {
        let inset = (portrait ? ControllerGeometry.Portrait.centreFromNearEdge : ControllerGeometry.centreFromNearEdge) * unit
        let fromBottom = portrait ? ControllerGeometry.Portrait.centreFromBottom : ControllerGeometry.centreFromBottom
        return CGPoint(
            x: (edge == .leading ? inset : container.width - inset) + anchorOffset.x * unit,
            y: container.height - fromBottom * unit + anchorOffset.y * unit
        )
    }

    private var centre: CGPoint {
        clamped(CGPoint(x: anchor.x + CGFloat(offsetX), y: anchor.y + CGFloat(offsetY)))
    }

    private var shoulderShift: CGFloat {
        ControllerGeometry.shoulderShift(
            offset: shoulderOffset, centreY: centre.y, containerHeight: container.height,
            unit: unit, controls: controls, topInset: topReserve) * unit
    }

    private func clamped(_ point: CGPoint) -> CGPoint {
        let minX = -box.minX * unit
        let maxX = container.width - box.maxX * unit
        let minY = topReserve - box.minY * unit
        let maxY = container.height - box.maxY * unit
        return CGPoint(
            x: minX <= maxX ? min(max(point.x, minX), maxX) : (minX + maxX) / 2,
            y: minY <= maxY ? min(max(point.y, minY), maxY) : (minY + maxY) / 2
        )
    }

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
                .allowsHitTesting(false)

            if isEditingLayout && !individualEditMode {
                dragHandle
            }
            let shoulderDrop = shoulderShift
            ForEach(controls) { control in
                EditableControl(
                    control: control,
                    skin: skin,
                    unit: unit,
                    base: CGPoint(
                        x: centre.x + control.offset.x * unit,
                        y: centre.y + control.offset.y * unit
                            + (control.style == .shoulder ? shoulderDrop : 0)
                    ),
                    container: container,
                    topReserve: topReserve,
                    isEditingLayout: isEditingLayout,
                    individualEditMode: individualEditMode,
                    portrait: portrait,
                    onStick: onStick,
                    onInput: onInput
                )
            }
        }
    }

    private var dragHandle: some View {
        let width = box.width * unit
        let height = box.height * unit
        let midX = centre.x + box.midX * unit
        let midY = centre.y + box.midY * unit

        return RoundedRectangle(cornerRadius: unit * 0.3, style: .continuous)
            .fill(Color.white.opacity(0.10))
            .overlay(
                RoundedRectangle(cornerRadius: unit * 0.3, style: .continuous)
                    .strokeBorder(
                        Color.white.opacity(0.65),
                        style: StrokeStyle(lineWidth: 2, dash: [8, 6])
                    )
            )
            .frame(width: width, height: height)
            .position(x: midX, y: midY)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let origin = dragOrigin ?? CGSize(width: offsetX, height: offsetY)
                        if dragOrigin == nil { dragOrigin = origin }
                        offsetX = origin.width + value.translation.width
                        offsetY = origin.height + value.translation.height
                    }
                    .onEnded { _ in
                        dragOrigin = nil
                        let settled = centre
                        offsetX = Double(settled.x - anchor.x)
                        offsetY = Double(settled.y - anchor.y)
                    }
            )
    }
}

private struct EditableControl: View {
    let control: ControllerGeometry.Control
    let skin: WiiUControllerSkin
    let unit: CGFloat
    let base: CGPoint
    let container: CGSize
    let topReserve: CGFloat
    let isEditingLayout: Bool
    let individualEditMode: Bool
    var portrait: Bool = false
    let onStick: (CGPoint) -> Void
    let onInput: (String, Bool) -> Void

    @ObservedObject private var custom = ControllerCustomLayout.shared
    @State private var dragOrigin: ControlOverride?
    @State private var scaleOrigin: Double?

    private var settings: ControlOverride { portrait ? .identity : custom.override(for: control.id) }

    private var placed: CGPoint {
        held(CGPoint(x: base.x + CGFloat(settings.dx), y: base.y + CGFloat(settings.dy)))
    }

    private func held(_ point: CGPoint) -> CGPoint {
        let size = ControllerGeometry.size(of: control)
        let halfWidth = size.width * unit * CGFloat(settings.scale) / 2
        let halfHeight = size.height * unit * CGFloat(settings.scale) / 2
        let minX = halfWidth, maxX = container.width - halfWidth
        let minY = topReserve + halfHeight, maxY = container.height - halfHeight
        return CGPoint(
            x: minX <= maxX ? min(max(point.x, minX), maxX) : (minX + maxX) / 2,
            y: minY <= maxY ? min(max(point.y, minY), maxY) : (minY + maxY) / 2
        )
    }

    var body: some View {
        Group {
            if control.style == .joystick {
                JoystickControl(
                    control: control,
                    skin: skin,
                    unit: unit * CGFloat(settings.scale),
                    isInteractive: !isEditingLayout,
                    onStick: onStick
                )
            } else {
                ControlButton(
                    control: control,
                    skin: skin,
                    unit: unit * CGFloat(settings.scale),
                    isInteractive: !isEditingLayout,
                    onInput: onInput
                )
            }
        }
        .position(placed)
        .overlay(
            Group {
                if isEditingLayout && individualEditMode {
                    Circle()
                        .strokeBorder(Color.white.opacity(0.5), style: StrokeStyle(lineWidth: 1.5, dash: [4, 4]))
                        .frame(width: unit * CGFloat(settings.scale) * 1.25, height: unit * CGFloat(settings.scale) * 1.25)
                        .position(placed)
                        .allowsHitTesting(false)
                }
            }
        )
        .gesture(isEditingLayout && individualEditMode ? editGesture : nil)
    }

    private var editGesture: some Gesture {
        let drag = DragGesture(minimumDistance: 0)
            .onChanged { value in
                let origin = dragOrigin ?? settings
                if dragOrigin == nil { dragOrigin = origin }
                let wanted = CGPoint(x: base.x + CGFloat(origin.dx) + value.translation.width,
                                     y: base.y + CGFloat(origin.dy) + value.translation.height)
                let limited = held(wanted)
                custom.move(control.id,
                            to: CGSize(width: limited.x - base.x - CGFloat(origin.dx),
                                       height: limited.y - base.y - CGFloat(origin.dy)),
                            from: origin)
            }
            .onEnded { _ in
                dragOrigin = nil
                custom.commit()
            }

        let pinch = MagnificationGesture()
            .onChanged { value in
                let origin = scaleOrigin ?? settings.scale
                if scaleOrigin == nil { scaleOrigin = origin }
                custom.setScale(origin * Double(value), for: control.id)
            }
            .onEnded { _ in
                scaleOrigin = nil
                custom.commit()
            }

        return SimultaneousGesture(drag, pinch)
    }
}

private struct ControlButton: View {
    let control: ControllerGeometry.Control
    let skin: WiiUControllerSkin
    let unit: CGFloat
    let isInteractive: Bool
    let onInput: (String, Bool) -> Void

    private static let neutralFill = Color(white: 0.85)
    private static let neutralLabel = Color(white: 0.22)

    var body: some View {
        HeldControl(onPressChange: { onInput(control.id, $0) }, isInteractive: isInteractive) { isPressed in
            ZStack {
                shape(isPressed: isPressed)
                Text(control.glyph)
                    .font(.system(size: fontSize, weight: .bold, design: .rounded))
                    .foregroundColor(labelColor)
            }
            .frame(width: size.width, height: size.height)
            .scaleEffect(isPressed ? 0.94 : 1.0)
            .animation(.easeInOut(duration: 0.05), value: isPressed)
        }
    }

    private var size: CGSize {
        switch control.shape {
        case .circle(let diameter):
            return CGSize(width: diameter * unit, height: diameter * unit)
        case .roundedRect(let box, _):
            return CGSize(width: box.width * unit, height: box.height * unit)
        }
    }

    @ViewBuilder
    private func shape(isPressed: Bool) -> some View {
        let fill = fillColor.opacity(isPressed ? 1.0 : 0.88)
        let line = max(1, unit * 0.05)

        switch control.shape {
        case .circle:
            Circle()
                .fill(fill)
                .overlay(Circle().strokeBorder(Color.black.opacity(0.45), lineWidth: line))
        case .roundedRect(_, let radius):
            RoundedRectangle(cornerRadius: radius * unit, style: .continuous)
                .fill(fill)
                .overlay(
                    RoundedRectangle(cornerRadius: radius * unit, style: .continuous)
                        .strokeBorder(Color.black.opacity(0.45), lineWidth: line)
                )
        }
    }

    private var fillColor: Color {
        switch control.style {
        case .dpad:
            return skin.dpadColor
        case .face:
            return skin.buttonColors[control.id] ?? Color.gray
        case .shoulder, .system, .stick, .joystick:
            return Self.neutralFill
        }
    }

    private var labelColor: Color {
        switch control.style {
        case .dpad, .face:
            return LegibleInk.ensure(.white, on: fillColor, minimum: LegibleInk.glyph)
        case .shoulder, .system, .stick, .joystick:
            return Self.neutralLabel
        }
    }

    private var fontSize: CGFloat {
        switch control.style {
        case .dpad:     return unit * 0.32
        case .face:     return unit * 0.42
        case .system:   return unit * 0.46
        case .stick, .joystick: return unit * 0.30
        case .shoulder: return unit * (control.glyph.count > 1 ? 0.30 : 0.38)
        }
    }
}

struct HeldControl<Content: View>: View {
    let onPressChange: (Bool) -> Void
    var isInteractive: Bool = true
    var hitShape: HeldControlHitShape = .rectangle
    let content: (Bool) -> Content

    @GestureState private var isPressed = false
    @State private var downSent = false
    @State private var endedNormally = false
    @State private var pressBegan = Date()
    @State private var glowing = false
    private static var minimumLook: TimeInterval { 0.09 }

    @AppStorage(ControllerLayoutSettings.hapticsKey)
    private var hapticsEnabled = ControllerLayoutSettings.defaultHaptics

    var body: some View {
        content(isPressed || downSent || glowing)
            .contentShape(hitShape)
            .accessibilityAddTraits(.isButton)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .updating($isPressed) { _, state, _ in state = true }
                    .onChanged { _ in
                        PadDiagnostics.shared.recordRawTouch()
                        if !downSent {
                            downSent = true
                            report(true)
                        }
                    }
                    .onEnded { _ in
                        endedNormally = true
                        release()
                    }
            )
            .onChange(of: isPressed) { pressed in
                if !pressed { release() }
            }
            .onChange(of: downSent) { sent in
                if sent && !isPressed { release() }
            }
            .allowsHitTesting(isInteractive)
    }

    private func release() {
        guard downSent else { return }
        downSent = false
        report(false)
    }

    private func report(_ pressed: Bool) {
        let began = pressBegan
        if pressed { pressBegan = Date(); endedNormally = false; glowing = true }
        onPressChange(pressed)
        if pressed {
            PadDiagnostics.shared.recordPressBegan()
        } else {
            let reason: PadDiagnostics.ReleaseReason =
                !isInteractive ? .stoppedAcceptingTouches
                : (endedNormally ? .fingerLifted : .gestureCancelled)
            PadDiagnostics.shared.recordRelease(reason, heldSince: began)
        }
        if pressed, hapticsEnabled { PadHaptics.shared.fire() }
        if !pressed {
            let remaining = Self.minimumLook - Date().timeIntervalSince(pressBegan)
            if remaining > 0 {
                DispatchQueue.main.asyncAfter(deadline: .now() + remaining) { glowing = false }
            } else {
                glowing = false
            }
        }
    }
}

enum HeldControlHitShape: Shape {
    case rectangle
    case circle(fraction: CGFloat)

    func path(in rect: CGRect) -> Path {
        switch self {
        case .rectangle:
            return Path(rect)
        case .circle(let fraction):
            let d = min(rect.width, rect.height) * fraction
            return Path(ellipseIn: CGRect(x: rect.midX - d / 2, y: rect.midY - d / 2, width: d, height: d))
        }
    }
}

@MainActor
final class PadHaptics {
    static let shared = PadHaptics()

    #if canImport(UIKit)
    private let generator = UIImpactFeedbackGenerator(style: .rigid)

    private init() { generator.prepare() }

    func fire() {
        generator.impactOccurred()
        generator.prepare()
    }
    #else
    private init() {}
    func fire() {}
    #endif
}

private struct StickGateShape: InsettableShape {
    let gate: ControllerGeometry.StickGate
    var inset: CGFloat = 0

    func path(in rect: CGRect) -> Path {
        let radius = min(rect.width, rect.height) / 2 - inset
        guard radius > 0 else { return Path() }

        switch gate {
        case .round:
            return Circle().path(in: CGRect(x: rect.midX - radius, y: rect.midY - radius,
                                            width: radius * 2, height: radius * 2))
        }
    }

    func inset(by amount: CGFloat) -> StickGateShape {
        StickGateShape(gate: gate, inset: inset + amount)
    }
}

private struct JoystickControl: View {
    let control: ControllerGeometry.Control
    let skin: WiiUControllerSkin
    let unit: CGFloat
    let isInteractive: Bool
    let onStick: (CGPoint) -> Void

    @AppStorage(ControllerLayoutSettings.deadzoneKey)
    private var deadzoneSetting = ControllerLayoutSettings.defaultDeadzone
    @AppStorage(ControllerLayoutSettings.stickCurveKey)
    private var curveSetting = ControllerLayoutSettings.defaultStickCurve
    @AppStorage(ControllerLayoutSettings.stickGateKey)
    private var gateSetting = ControllerLayoutSettings.defaultStickGateRaw

    @State private var knobOffset: CGSize = .zero
    @GestureState private var touching = false
    @State private var pushed = false

    private var deadzone: CGFloat {
        CGFloat(min(max(deadzoneSetting, ControllerLayoutSettings.minDeadzone),
                    ControllerLayoutSettings.maxDeadzone))
    }
    private var curve: CGFloat {
        CGFloat(min(max(curveSetting, ControllerLayoutSettings.minStickCurve),
                    ControllerLayoutSettings.maxStickCurve))
    }
    private var gate: ControllerGeometry.StickGate {
        ControllerGeometry.StickGate(rawValue: gateSetting) ?? ControllerLayoutSettings.defaultStickGate
    }

    private var base: CGFloat { ControllerGeometry.stickBaseDiameter * unit }
    private var knob: CGFloat { ControllerGeometry.stickKnobDiameter * unit }
    private var travel: CGFloat { ControllerGeometry.stickTravel(ringDiameter: base) }

    var body: some View {
        ZStack {
            StickGateShape(gate: gate)
                .fill(Color(white: 0.85).opacity(0.55))
                .overlay(
                    StickGateShape(gate: gate)
                        .strokeBorder(Color.black.opacity(0.45), lineWidth: max(1, unit * 0.05))
                )

            Circle()
                .fill(skin.dpadColor.opacity(pushed ? 1.0 : 0.9))
                .overlay(
                    Circle().strokeBorder(Color.black.opacity(0.45), lineWidth: max(1, unit * 0.05))
                )
                .frame(width: knob, height: knob)
                .offset(knobOffset)
        }
        .frame(width: base, height: base)
        .contentShape(Circle())
        .accessibilityLabel(control.id == "stickL" ? "Left stick" : "Camera stick")
        .gesture(
            DragGesture(minimumDistance: 0)
                .updating($touching) { _, state, _ in state = true }
                .onChanged { value in
                    let dx = value.location.x - base / 2
                    let dy = value.location.y - base / 2
                    let distance = (dx * dx + dy * dy).squareRoot()

                    let reach = travel * gate.radiusFraction(atAngle: atan2(dy, dx))

                    let scale = distance > reach ? reach / distance : 1
                    knobOffset = CGSize(width: dx * scale, height: dy * scale)

                    let deflection = travel > 0 ? min(distance, reach) / travel : 0
                    if deflection > ControllerGeometry.stickClickThreshold {
                        pushed = true
                    }
                    report(deflection: deflection, dx: dx, dy: dy, distance: distance)
                }
                .onEnded { _ in recentre() }
        )
        .allowsHitTesting(isInteractive)
        .onChange(of: touching) { down in
            if !down { recentre() }
        }
        .onChange(of: knobOffset) { offset in
            if offset != .zero && !touching { recentre() }
        }
        .onChange(of: isInteractive) { active in
            if !active { recentre() }
        }
        .onDisappear { recentre() }
    }

    private func report(deflection: CGFloat, dx: CGFloat, dy: CGFloat, distance: CGFloat) {
        let dead = deadzone
        guard deflection > dead, distance > 0 else {
            onStick(.zero)
            return
        }

        var magnitude = (deflection - dead) / (1 - dead)

        if curve != 1 {
            magnitude = CGFloat(pow(Double(magnitude), Double(curve)))
        }

        onStick(CGPoint(x: dx / distance * magnitude, y: -dy / distance * magnitude))
    }

    private func recentre() {
        pushed = false
        onStick(.zero)
        withAnimation(.spring(response: 0.18, dampingFraction: 0.7)) {
            knobOffset = .zero
        }
    }
}
