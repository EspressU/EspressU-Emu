// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import GameController

private struct Dome: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        let w = r.width, h = r.height
        p.move(to: CGPoint(x: r.minX + w * 0.03, y: r.minY + h * 0.98))
        p.addCurve(to: CGPoint(x: r.minX + w * 0.50, y: r.minY + h * 0.02),
                   control1: CGPoint(x: r.minX + w * 0.06, y: r.minY + h * 0.46),
                   control2: CGPoint(x: r.minX + w * 0.24, y: r.minY + h * 0.02))
        p.addCurve(to: CGPoint(x: r.minX + w * 0.97, y: r.minY + h * 0.98),
                   control1: CGPoint(x: r.minX + w * 0.76, y: r.minY + h * 0.02),
                   control2: CGPoint(x: r.minX + w * 0.94, y: r.minY + h * 0.46))
        p.closeSubpath()
        return p
    }
}

private struct Wrapper: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        let w = r.width, h = r.height
        p.move(to: CGPoint(x: r.minX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.minY))
        p.addQuadCurve(to: CGPoint(x: r.minX + w * 0.80, y: r.minY + h),
                       control: CGPoint(x: r.minX + w * 0.93, y: r.minY + h * 0.6))
        p.addLine(to: CGPoint(x: r.minX + w * 0.20, y: r.minY + h))
        p.addQuadCurve(to: CGPoint(x: r.minX, y: r.minY),
                       control: CGPoint(x: r.minX + w * 0.07, y: r.minY + h * 0.6))
        p.closeSubpath()
        return p
    }
}

private struct WrapperFlutes: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        let w = r.width, h = r.height
        for i in 1..<6 {
            let t = CGFloat(i) / 6.0
            let topX = r.minX + w * t
            let botX = r.minX + w * (0.20 + 0.60 * t)
            p.move(to: CGPoint(x: topX, y: r.minY))
            p.addLine(to: CGPoint(x: botX, y: r.minY + h))
        }
        return p
    }
}

private struct Creases: Shape {
    func path(in r: CGRect) -> Path {
        var p = Path()
        let w = r.width, h = r.height
        for (x, len) in [(0.40, 0.30), (0.52, 0.38), (0.63, 0.34)] {
            p.move(to: CGPoint(x: r.minX + w * x, y: r.minY + h * 0.16))
            p.addQuadCurve(to: CGPoint(x: r.minX + w * (x - 0.03), y: r.minY + h * (0.16 + len)),
                           control: CGPoint(x: r.minX + w * (x + 0.03), y: r.minY + h * (0.16 + len * 0.5)))
        }
        return p
    }
}

private struct Smile: Shape {
    var openness: CGFloat
    var animatableData: CGFloat {
        get { openness }
        set { openness = newValue }
    }
    func path(in r: CGRect) -> Path {
        var p = Path()
        let drop = r.height * (0.55 + 0.45 * openness)
        p.move(to: CGPoint(x: r.minX, y: r.minY))
        p.addQuadCurve(to: CGPoint(x: r.maxX, y: r.minY),
                       control: CGPoint(x: r.midX, y: r.minY + drop))
        return p
    }
}

private struct Berry: Identifiable {
    let id: Int
    let x: CGFloat, y: CGFloat, r: CGFloat
    let square: Bool
}

private let kBerries: [Berry] = [
    Berry(id: 0, x: 0.24, y: 0.34, r: 0.062, square: false),
    Berry(id: 1, x: 0.40, y: 0.22, r: 0.048, square: false),
    Berry(id: 2, x: 0.66, y: 0.31, r: 0.045, square: false),
    Berry(id: 3, x: 0.77, y: 0.44, r: 0.042, square: false),
    Berry(id: 4, x: 0.60, y: 0.24, r: 0.046, square: true)
]

struct EspressUMark: View {
    var side: CGFloat = 120

    var body: some View {
        let domeH = side * 0.56
        let wrapH = side * 0.40
        let wrapW = side * 0.62

        ZStack {
            VStack(spacing: -side * 0.015) {
                Dome().fill(EspressUTheme.cremaTopGradient)
                    .frame(width: side, height: domeH)
                Wrapper().fill(EspressUTheme.cream)
                    .frame(width: wrapW, height: wrapH)
            }

            VStack(spacing: -side * 0.015) {
                Spacer().frame(width: side, height: domeH)
                WrapperFlutes().stroke(EspressUTheme.wrapper, lineWidth: side * 0.055)
                    .frame(width: wrapW, height: wrapH)
                    .clipShape(Wrapper())
            }

            berries(side: side, domeH: domeH, wrapH: wrapH)
            face(side: side, domeH: domeH, wrapH: wrapH)
        }
        .frame(width: side, height: domeH + wrapH)
        .accessibilityHidden(true)
    }

    private func berries(side: CGFloat, domeH: CGFloat, wrapH: CGFloat) -> some View {
        ZStack {
            ForEach(kBerries) { b in
                Group {
                    if b.square {
                        Rectangle().fill(EspressUTheme.pixelBlue)
                    } else {
                        Circle().fill(EspressUTheme.blueberryNavy)
                    }
                }
                .frame(width: side * b.r * 2, height: side * b.r * 2)
                .position(x: side * b.x, y: domeH * b.y)
            }
        }
        .frame(width: side, height: domeH)
        .offset(y: -wrapH / 2)
    }

    private func face(side: CGFloat, domeH: CGFloat, wrapH: CGFloat) -> some View {
        let eyeY = domeH * 0.62
        let eyeDX = side * 0.085
        return ZStack {
            ForEach([-1.0, 1.0], id: \.self) { s in
                Ellipse().fill(EspressUTheme.blushPink.opacity(0.85))
                    .frame(width: side * 0.10, height: side * 0.055)
                    .position(x: side * 0.5 + CGFloat(s) * side * 0.155, y: eyeY + domeH * 0.12)
            }
            ForEach([-1.0, 1.0], id: \.self) { s in
                ZStack {
                    Capsule().fill(Color.black)
                        .frame(width: side * 0.036, height: side * 0.052)
                    Circle().fill(Color.white)
                        .frame(width: side * 0.013, height: side * 0.013)
                        .offset(x: -side * 0.008, y: -side * 0.012)
                }
                .position(x: side * 0.5 + CGFloat(s) * eyeDX, y: eyeY)
            }
            Smile(openness: 1)
                .stroke(EspressUTheme.brownDark,
                        style: StrokeStyle(lineWidth: side * 0.016, lineCap: .round))
                .frame(width: side * 0.13, height: side * 0.07)
                .position(x: side * 0.5, y: eyeY + domeH * 0.19)
        }
        .frame(width: side, height: domeH)
        .offset(y: -wrapH / 2)
    }
}

struct LaunchIntroView: View {
    var isGameRunning: Bool = false
    var onFinished: () -> Void
    private static let minimumPlay: TimeInterval = 2.0

    @State private var sketch: CGFloat = 0
    @State private var inked: CGFloat = 0
    @State private var pop: CGFloat = 0
    @State private var pageOpacity: CGFloat = 0
    @State private var eyeShift: CGFloat = 0
    @State private var blink: CGFloat = 1
    @State private var grin: CGFloat = 0
    @State private var fade: CGFloat = 0
    @State private var started = false
    @State private var finished = false
    @State private var vanish = false
    @State private var showHint = false
    @State private var runTask: Task<Void, Never>?
    @State private var shownAt = Date()
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    private let chalk = Color(red: 0.98, green: 0.95, blue: 0.90)

    var body: some View {
        GeometryReader { geo in
            let side = min(geo.size.width, geo.size.height) * 0.52
            ZStack {
                Color.black.ignoresSafeArea()

                RoundedRectangle(cornerRadius: side * 0.22, style: .continuous)
                    .fill(EspressUTheme.backgroundGradient)
                    .frame(width: side * 1.28, height: side * 1.28)
                    .opacity(pageOpacity * (1 - pop * 0.75))
                    .scaleEffect(1 - pop * 0.10)
                    .blur(radius: pop * 6)

                espressu(side: side)
                    .scaleEffect(0.82 + pop * 0.30)
                    .rotation3DEffect(.degrees(Double((1 - pop) * 34)),
                                      axis: (x: 1, y: 0, z: 0),
                                      perspective: 0.55)
                    .shadow(color: EspressUTheme.shadow.opacity(Double(pop) * 0.55),
                            radius: 26 * pop, x: 0, y: 20 * pop)

                Color.black.opacity(Double(fade)).ignoresSafeArea()

                skipHint
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
        .opacity(vanish ? 0 : 1)
        .contentShape(Rectangle())
        .onTapGesture { finish() }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Skip intro")
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { finish() }
        .onAppear {
            guard !started else { return }
            started = true
            shownAt = Date()
            if reduceMotion { finish(animated: false) } else { run() }
        }
        .onDisappear { runTask?.cancel() }
        .task(id: isGameRunning) { await endWhenGameIsRunning() }
        .task { await watchControllerForSkip() }
    }

    private var skipHint: some View {
        VStack {
            Spacer()
            Text("Tap to skip")
                .font(.system(.footnote, design: .rounded).weight(.semibold))
                .foregroundColor(.white.opacity(0.6))
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .opacity(showHint ? 1 : 0)
                .padding(.bottom, 24)
                .accessibilityHidden(true)
        }
        .allowsHitTesting(false)
    }

    private func finish(animated: Bool = true) {
        guard !finished else { return }
        finished = true
        runTask?.cancel()
        guard animated else { onFinished(); return }
        withAnimation(.easeOut(duration: 0.3)) { vanish = true }
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 330_000_000)
            onFinished()
        }
    }

    private func endWhenGameIsRunning() async {
        guard isGameRunning else { return }
        let remaining = Self.minimumPlay - Date().timeIntervalSince(shownAt)
        if remaining > 0 {
            try? await Task.sleep(nanoseconds: UInt64(remaining * 1_000_000_000))
        }
        guard !Task.isCancelled else { return }
        finish()
    }

    private func watchControllerForSkip() async {
        var wasDown = Self.skipButtonDown()
        while !Task.isCancelled && !finished {
            try? await Task.sleep(nanoseconds: 80_000_000)
            let down = Self.skipButtonDown()
            if down && !wasDown { finish(); return }
            wasDown = down
        }
    }

    private static func skipButtonDown() -> Bool {
        GCController.controllers().contains { controller in
            guard let pad = controller.extendedGamepad else { return false }
            return pad.buttonA.isPressed || pad.buttonB.isPressed || pad.buttonMenu.isPressed
        }
    }

    @ViewBuilder
    private func espressu(side: CGFloat) -> some View {
        let domeH = side * 0.56
        let wrapH = side * 0.40
        let wrapW = side * 0.62

        ZStack {
            VStack(spacing: -side * 0.015) {
                Dome().fill(EspressUTheme.cremaTopGradient)
                    .frame(width: side, height: domeH)
                Wrapper().fill(EspressUTheme.cream)
                    .frame(width: wrapW, height: wrapH)
            }
            .opacity(Double(inked))

            VStack(spacing: -side * 0.015) {
                Spacer().frame(width: side, height: domeH)
                WrapperFlutes().stroke(EspressUTheme.wrapper, lineWidth: side * 0.055)
                    .frame(width: wrapW, height: wrapH)
                    .clipShape(Wrapper())
            }
            .opacity(Double(inked))

            berries(side: side, domeH: domeH)
                .opacity(Double(inked))

            face(side: side, domeH: domeH)
                .opacity(Double(inked))

            VStack(spacing: -side * 0.015) {
                ZStack {
                    Dome().trim(from: 0, to: sketch)
                        .stroke(chalk, style: StrokeStyle(lineWidth: side * 0.012, lineCap: .round))
                    Creases().trim(from: 0, to: max(0, (sketch - 0.45) / 0.35))
                        .stroke(chalk.opacity(0.7), style: StrokeStyle(lineWidth: side * 0.008, lineCap: .round))
                }
                .frame(width: side, height: domeH)

                ZStack {
                    Wrapper().trim(from: 0, to: sketch)
                        .stroke(chalk, style: StrokeStyle(lineWidth: side * 0.012, lineCap: .round))
                    WrapperFlutes().trim(from: 0, to: max(0, (sketch - 0.5) / 0.4))
                        .stroke(chalk.opacity(0.6), style: StrokeStyle(lineWidth: side * 0.007, lineCap: .round))
                }
                .frame(width: wrapW, height: wrapH)
            }
            .opacity(Double(1 - inked * 0.85))
        }
        .frame(width: side, height: domeH + wrapH)
    }

    private func berries(side: CGFloat, domeH: CGFloat) -> some View {
        ZStack {
            ForEach(kBerries) { b in
                Group {
                    if b.square {
                        Rectangle().fill(EspressUTheme.pixelBlue)
                    } else {
                        Circle().fill(EspressUTheme.blueberryNavy)
                    }
                }
                .frame(width: side * b.r * 2, height: side * b.r * 2)
                .position(x: side * b.x, y: domeH * b.y)
            }
        }
        .frame(width: side, height: domeH)
        .offset(y: -(side * 0.40) / 2)
    }

    private func face(side: CGFloat, domeH: CGFloat) -> some View {
        let eyeY = domeH * 0.62
        let eyeDX = side * 0.085
        return ZStack {
            ForEach([-1.0, 1.0], id: \.self) { s in
                Ellipse().fill(EspressUTheme.blushPink.opacity(0.85))
                    .frame(width: side * 0.10, height: side * 0.055)
                    .position(x: side * 0.5 + CGFloat(s) * side * 0.155, y: eyeY + domeH * 0.12)
            }
            ForEach([-1.0, 1.0], id: \.self) { s in
                ZStack {
                    Capsule().fill(Color.black)
                        .frame(width: side * 0.036, height: side * 0.052 * blink)
                    Circle().fill(Color.white)
                        .frame(width: side * 0.013, height: side * 0.013 * blink)
                        .offset(x: -side * 0.008, y: -side * 0.012)
                }
                .position(x: side * 0.5 + CGFloat(s) * eyeDX + eyeShift * side * 0.018, y: eyeY)
            }
            Smile(openness: grin)
                .stroke(EspressUTheme.brownDark,
                        style: StrokeStyle(lineWidth: side * 0.016, lineCap: .round))
                .frame(width: side * 0.13, height: side * 0.07)
                .position(x: side * 0.5, y: eyeY + domeH * 0.19)
        }
        .frame(width: side, height: domeH)
        .offset(y: -(side * 0.40) / 2)
    }

    private func run() {
        withAnimation(.easeIn(duration: 0.4).delay(0.5)) { showHint = true }
        runTask = Task { @MainActor in
            do {
                withAnimation(.easeOut(duration: 0.5)) { pageOpacity = 0.55 }
                withAnimation(.easeInOut(duration: 2.1)) { sketch = 1 }
                try await Task.sleep(nanoseconds: 2_150_000_000)

                withAnimation(.easeIn(duration: 0.65)) { inked = 1 }
                try await Task.sleep(nanoseconds: 620_000_000)

                withAnimation(.interpolatingSpring(stiffness: 120, damping: 11)) { pop = 1 }
                try await Task.sleep(nanoseconds: 520_000_000)

                withAnimation(.easeInOut(duration: 0.42)) { eyeShift = -1 }
                try await Task.sleep(nanoseconds: 470_000_000)
                withAnimation(.easeInOut(duration: 0.5)) { eyeShift = 1 }
                try await Task.sleep(nanoseconds: 540_000_000)
                withAnimation(.easeInOut(duration: 0.28)) { eyeShift = 0 }
                withAnimation(.easeInOut(duration: 0.09)) { blink = 0.08 }
                try await Task.sleep(nanoseconds: 110_000_000)
                withAnimation(.easeInOut(duration: 0.12)) { blink = 1 }
                try await Task.sleep(nanoseconds: 160_000_000)
                withAnimation(.spring(response: 0.35, dampingFraction: 0.6)) { grin = 1 }
                try await Task.sleep(nanoseconds: 620_000_000)

                withAnimation(.easeIn(duration: 0.55)) { fade = 1 }
                try await Task.sleep(nanoseconds: 580_000_000)
                guard !finished else { return }
                finished = true
                onFinished()
            } catch {
            }
        }
    }
}
