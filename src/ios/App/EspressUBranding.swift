// SPDX-License-Identifier: MPL-2.0

import SwiftUI

struct EspressULogo: View {
    var side: CGFloat = 120

    var body: some View {
        Image("AppIcon-preview")
            .resizable()
            .scaledToFill()
            .frame(width: side, height: side)
            .clipShape(RoundedRectangle(cornerRadius: side * 0.225, style: .continuous))
            .accessibilityLabel("EspressU")
    }
}

struct EspressUIntroView: View {
    var isGameRunning: Bool = false
    var onFinished: () -> Void

    private static let fullPlay: TimeInterval = 2.4
    private static let minimumPlay: TimeInterval = 1.2

    @State private var appeared = false
    @State private var fading = false
    @State private var finished = false
    @State private var shownAt = Date()

    var body: some View {
        GeometryReader { geo in
            let side = min(geo.size.width, geo.size.height) * 0.32
            ZStack {
                Color.black.ignoresSafeArea()
                VStack(spacing: side * 0.14) {
                    EspressULogo(side: side)
                        .shadow(color: .black.opacity(0.5), radius: side * 0.12, y: side * 0.05)
                    Text("EspressU")
                        .font(.system(size: side * 0.2, weight: .heavy, design: .rounded))
                        .foregroundColor(.white)
                }
                .scaleEffect(appeared ? 1 : 0.86)
                .opacity(appeared ? 1 : 0)
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
        .opacity(fading ? 0 : 1)
        .contentShape(Rectangle())
        .onTapGesture { finish() }
        .onAppear {
            shownAt = Date()
            withAnimation(.spring(response: 0.6, dampingFraction: 0.75)) { appeared = true }
            DispatchQueue.main.asyncAfter(deadline: .now() + Self.fullPlay) { finish() }
        }
        .onValueChange(of: isGameRunning) { running in
            guard running else { return }
            let remaining = max(0, Self.minimumPlay - Date().timeIntervalSince(shownAt))
            DispatchQueue.main.asyncAfter(deadline: .now() + remaining) { finish() }
        }
        .accessibilityAddTraits(.isButton)
        .accessibilityHint("Skips the intro.")
    }

    private func finish() {
        guard !finished else { return }
        finished = true
        withAnimation(.easeOut(duration: 0.35)) { fading = true }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { onFinished() }
    }
}
