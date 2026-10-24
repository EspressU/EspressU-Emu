// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Dispatch

extension Color {
    init(_ rgba: EspressURGBA) {
        self.init(.sRGB, red: rgba.r, green: rgba.g, blue: rgba.b, opacity: rgba.a)
    }
}

struct PreviewControllerPad: View {
    @ObservedObject var store: PreviewPadStore
    let onInput: (String, Bool) -> Void
    let onStick: (Int, CGPoint) -> Void
    @Binding var isEditingLayout: Bool

    var body: some View {
        GeometryReader { proxy in
            let insets = proxy.safeAreaInsets
            let full = proxy.frame(in: .local)
            let safeArea = CGRect(x: full.minX + insets.leading, y: full.minY + insets.top,
                                  width: full.width - insets.leading - insets.trailing,
                                  height: full.height - insets.top - insets.bottom)
            let resolved = store.resolve(container: proxy.size, safeArea: safeArea,
                                         pointsPerInch: DeviceMetrics.current().pointsPerInch)
            let colours = store.colourFile

            ZStack(alignment: .topLeading) {
                ForEach(PadGroup.allCases) { group in
                    PreviewGroupView(group: group, resolved: resolved, colours: colours,
                                     store: store, isEditingLayout: isEditingLayout,
                                     onInput: onInput, onStick: onStick)
                }
            }
            .opacity(isEditingLayout ? 1.0 : 0.85)
        }
        .allowsHitTesting(true)
    }
}

private struct PreviewGroupView: View {
    let group: PadGroup
    let resolved: PreviewResolved
    let colours: EspressUColourFile
    @ObservedObject var store: PreviewPadStore
    let isEditingLayout: Bool
    let onInput: (String, Bool) -> Void
    let onStick: (Int, CGPoint) -> Void

    @State private var dragOrigin: GroupPlacement?
    @State private var scaleOrigin: GroupPlacement?

    var body: some View {
        ZStack {
            if isEditingLayout, !editRect.isNull {
                Path(editRect)
                    .fill(Color.white.opacity(0.001))
                    .gesture(editGesture)
            }
            ForEach(group.controlIDs, id: \.self) { id in
                if let placement = resolved.controls[id] {
                    PreviewControlView(id: id, placement: placement, colours: colours,
                                       isEditingLayout: isEditingLayout, resolved: resolved,
                                       onInput: onInput, onStick: onStick)
                }
            }
            if let caption = group.caption, let anchor = resolved.controls[group.anchorControl]?.centre {
                let ink = Color(colours.glyph(group.anchorControl))
                let halo = LegibleInk.on(ink, light: .white, dark: .black).opacity(0.75)
                Text(caption)
                    .font(.system(size: 10, weight: .bold, design: .rounded))
                    .foregroundColor(ink)
                    .shadow(color: halo, radius: 1)
                    .shadow(color: halo, radius: 1)
                    .position(x: anchor.x, y: anchor.y + captionOffset(for: group))
                    .allowsHitTesting(false)
            }
        }
    }

    private var editRect: CGRect {
        group.controlIDs
            .compactMap { resolved.controls[$0] }
            .map { CGRect(x: $0.centre.x - $0.boundingSize.width / 2,
                          y: $0.centre.y - $0.boundingSize.height / 2,
                          width: $0.boundingSize.width, height: $0.boundingSize.height) }
            .reduce(CGRect.null) { $0.union($1) }
    }

    private var editGesture: some Gesture {
        SimultaneousGesture(moveGesture, resizeGesture)
    }

    private func captionOffset(for group: PadGroup) -> CGFloat {
        (resolved.controls[group.anchorControl].map { $0.boundingSize.height / 2 + 10 }) ?? 16
    }

    private var moveGesture: some Gesture {
        DragGesture(minimumDistance: 2)
            .onChanged { value in
                let origin = dragOrigin ?? store.adjustment(for: group)
                if dragOrigin == nil { dragOrigin = origin }
                let unit = max(resolved.unit, 1)
                var next = origin
                next.dx += (value.translation.width / unit) * group.inboardSign
                next.dy -= value.translation.height / unit
                store.setAdjustment(next, for: group)
            }
            .onEnded { _ in dragOrigin = nil }
    }

    private var resizeGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                let origin = scaleOrigin ?? store.adjustment(for: group)
                if scaleOrigin == nil { scaleOrigin = origin }
                var next = origin
                next.scale = origin.scale * value
                store.setAdjustment(next, for: group)
            }
            .onEnded { _ in scaleOrigin = nil }
    }
}

private struct PreviewControlView: View {
    let id: String
    let placement: PadLayout.Placement
    let colours: EspressUColourFile
    let isEditingLayout: Bool
    let resolved: PreviewResolved
    let onInput: (String, Bool) -> Void
    let onStick: (Int, CGPoint) -> Void

    private var glyphInk: Color {
        LegibleInk.ensure(Color(colours.glyph(id)), on: Color(colours.fill(id)), minimum: LegibleInk.glyph)
    }

    var body: some View {
        switch placement {
        case .circle(let centre, let diameter):
            if id == "stickL" || id == "stickR" {
                PreviewStickView(id: id, centre: centre, diameter: diameter,
                                 colours: colours, isInteractive: !isEditingLayout, onStick: onStick,
                                 clickID: id == "stickL" ? "L3" : "R3", onInput: onInput)
            } else if id.hasPrefix("knob") {
                EmptyView()
            } else {
                HeldControl(onPressChange: { onInput(id, $0) }, isInteractive: !isEditingLayout,
                            hitShape: hitShape(for: id, diameter: diameter)) { isPressed in
                    ZStack {
                        Circle()
                            .fill(Color(colours.fill(id)).opacity(colours.alpha(id, pressed: isPressed)))
                            .overlay(Circle().strokeBorder(Color(colours.outline), lineWidth: max(1, diameter * 0.04)))
                        if ["X", "Y", "A", "B"].contains(id) {
                            Text(id)
                                .font(.system(size: diameter * 0.42, weight: .bold, design: .rounded))
                                .foregroundColor(glyphInk)
                        } else if id == "plus" {
                            Image(systemName: "plus").font(.system(size: diameter * 0.5, weight: .bold))
                                .foregroundColor(glyphInk)
                        } else if id == "minus" {
                            Image(systemName: "minus").font(.system(size: diameter * 0.5, weight: .bold))
                                .foregroundColor(glyphInk)
                        } else if id == "HOME" {
                            Image(systemName: "house.fill").font(.system(size: diameter * 0.4))
                                .foregroundColor(glyphInk)
                        }
                    }
                }
                .frame(width: diameter, height: diameter)
                .position(centre)
                .allowsHitTesting(!isEditingLayout)
            }

        case .pill(let centre, let size, let corner):
            HeldControl(onPressChange: { onInput(id, $0) }, isInteractive: !isEditingLayout) { isPressed in
                ZStack {
                    RoundedRectangle(cornerRadius: corner, style: .continuous)
                        .fill(Color(colours.fill(id)).opacity(colours.alpha(id, pressed: isPressed)))
                        .overlay(RoundedRectangle(cornerRadius: corner, style: .continuous)
                            .strokeBorder(Color(colours.outline), lineWidth: max(1, size.height * 0.06)))
                    Text(id).font(.system(size: size.height * 0.38, weight: .bold, design: .rounded))
                        .foregroundColor(glyphInk)
                }
            }
            .frame(width: size.width, height: size.height)
            .position(centre)
            .allowsHitTesting(!isEditingLayout)

        case .cross(let centre, let size, let arm):
            PreviewDpadView(centre: centre, size: size, arm: arm, colours: colours,
                            isInteractive: !isEditingLayout, onInput: onInput)
        }
    }

    private func hitShape(for id: String, diameter: CGFloat) -> HeldControlHitShape {
        guard id == "L3" || id == "R3", diameter > 0,
              let dpad = resolved.controls["dpad"], case .cross(_, let size, _) = dpad else {
            return .rectangle
        }
        let deadDiameter = 2 * PadLayout.dpadDeadZone * min(size.width, size.height) / 2
        return .circle(fraction: min(1, deadDiameter / diameter))
    }
}

private struct PreviewDpadView: View {
    let centre: CGPoint
    let size: CGSize
    let arm: CGFloat
    let colours: EspressUColourFile
    let isInteractive: Bool
    let onInput: (String, Bool) -> Void

    @State private var held: Set<String> = []
    @GestureState private var touching = false

    private func cross() -> Path {
        let w = size.width, h = size.height
        let a = arm / 2, hw = w / 2, hh = h / 2
        return Path { p in
            p.move(to: CGPoint(x: hw - a, y: 0)); p.addLine(to: CGPoint(x: hw + a, y: 0))
            p.addLine(to: CGPoint(x: hw + a, y: hh - a)); p.addLine(to: CGPoint(x: w, y: hh - a))
            p.addLine(to: CGPoint(x: w, y: hh + a)); p.addLine(to: CGPoint(x: hw + a, y: hh + a))
            p.addLine(to: CGPoint(x: hw + a, y: h)); p.addLine(to: CGPoint(x: hw - a, y: h))
            p.addLine(to: CGPoint(x: hw - a, y: hh + a)); p.addLine(to: CGPoint(x: 0, y: hh + a))
            p.addLine(to: CGPoint(x: 0, y: hh - a)); p.addLine(to: CGPoint(x: hw - a, y: hh - a))
            p.closeSubpath()
        }
    }

    private func releaseAll() {
        for id in held { onInput(id, false) }
        held = []
    }

    var body: some View {
        cross()
            .fill(Color(colours.fill("dpad")))
            .overlay(cross().stroke(Color(colours.outline), lineWidth: max(1, arm * 0.06)))
            .frame(width: size.width, height: size.height)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .updating($touching) { _, state, _ in state = true }
                    .onChanged { value in
                        let next = PadLayout.dpadDirections(
                            at: value.location,
                            centre: CGPoint(x: size.width / 2, y: size.height / 2),
                            size: size)
                        for id in held.subtracting(next) { onInput(id, false) }
                        for id in next.subtracting(held) { onInput(id, true) }
                        held = next
                    }
                    .onEnded { _ in releaseAll() }
            )
            .allowsHitTesting(isInteractive)
            .position(centre)
            .onValueChange(of: touching) { down in
                if !down { releaseAll() }
            }
            .onValueChange(of: held) { now in
                if !now.isEmpty && !touching { releaseAll() }
            }
            .onValueChange(of: isInteractive) { active in
                if !active { releaseAll() }
            }
            .onDisappear { releaseAll() }
    }
}

private struct PreviewStickView: View {
    let id: String
    let centre: CGPoint
    let diameter: CGFloat
    let colours: EspressUColourFile
    let isInteractive: Bool
    let onStick: (Int, CGPoint) -> Void
    let clickID: String
    let onInput: (String, Bool) -> Void

    @State private var knobOffset: CGSize = .zero
    @State private var pushed = false
    @GestureState private var touching = false
    @State private var clickRelease: DispatchWorkItem?

    @AppStorage(ControllerLayoutSettings.hapticsKey)
    private var hapticsEnabled = ControllerLayoutSettings.defaultHaptics

    private var radius: CGFloat { diameter / 2 }
    private var knobRadius: CGFloat { diameter * 0.28 }
    private var travel: CGFloat { ControllerGeometry.stickTravel(ringDiameter: diameter) }

    private static let clickHoldSeconds = 0.12

    var body: some View {
        ZStack {
            Circle()
                .fill(Color(colours.fill("default")).opacity(0.5))
                .overlay(Circle().strokeBorder(Color(colours.outline), lineWidth: max(1, diameter * 0.03)))
            Circle()
                .fill(Color(colours.fill("default")))
                .frame(width: knobRadius * 2, height: knobRadius * 2)
                .offset(knobOffset)
        }
        .frame(width: diameter, height: diameter)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .updating($touching) { _, state, _ in state = true }
                .onChanged { value in
                    let dx = value.location.x - radius, dy = value.location.y - radius
                    let distance = (dx * dx + dy * dy).squareRoot()
                    let reach = travel * ControllerGeometry.StickGate.round.radiusFraction(atAngle: atan2(dy, dx))
                    let scale = distance > reach ? reach / distance : 1
                    knobOffset = CGSize(width: dx * scale, height: dy * scale)
                    let deflection = travel > 0 ? min(distance, reach) / travel : 0
                    if deflection > 0.14 { pushed = true }
                    let nx = travel > 0 ? max(-1, min(1, (dx * scale) / travel)) : 0
                    let ny = travel > 0 ? max(-1, min(1, -(dy * scale) / travel)) : 0
                    onStick(id == "stickL" ? 0 : 1, CGPoint(x: nx, y: ny))
                }
                .onEnded { _ in
                    if !pushed { click() }
                }
        )
        .allowsHitTesting(isInteractive)
        .position(centre)
        .onValueChange(of: touching) { down in
            if !down { recentre() }
        }
        .onValueChange(of: knobOffset) { offset in
            if offset != .zero && !touching { recentre() }
        }
        .onValueChange(of: isInteractive) { active in
            if !active { recentre() }
        }
        .onDisappear {
            clickRelease?.cancel()
            clickRelease = nil
            onInput(clickID, false)
            pushed = false
            knobOffset = .zero
            onStick(id == "stickL" ? 0 : 1, .zero)
        }
    }

    private func recentre() {
        pushed = false
        onStick(id == "stickL" ? 0 : 1, .zero)
        withAnimation(.spring(response: 0.2, dampingFraction: 0.6)) { knobOffset = .zero }
    }

    private func click() {
        clickRelease?.cancel()
        if hapticsEnabled { PadHaptics.shared.fire() }
        onInput(clickID, true)
        let release = DispatchWorkItem { onInput(clickID, false) }
        clickRelease = release
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.clickHoldSeconds, execute: release)
    }
}
