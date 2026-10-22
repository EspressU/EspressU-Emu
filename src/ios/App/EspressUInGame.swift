// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

struct InGamePillButton: View {
    let symbol: String
    let title: String?
    let action: () -> Void

    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        Button(action: action) {
            HStack(spacing: 7) {
                Image(systemName: symbol)
                    .font(.system(size: 15, weight: .bold))
                if let title {
                    Text(title)
                        .font(.system(size: 15, weight: .bold, design: .rounded))
                }
            }
            .foregroundColor(.white)
            .padding(.horizontal, title == nil ? 0 : 14)
            .frame(minWidth: 40, minHeight: 40)
            .contentShape(Capsule())
        }
        .buttonStyle(InGamePillPressStyle())
        .opacity(isEnabled ? 1 : 0.4)
    }
}

/// Holds the GamePad microphone's blow input while it is pressed, for games that ask the player to blow.
/// The press lasts until the finger lifts, even if it slides off the button, and always ends when the button
/// goes away, the game pauses or the app leaves the foreground.
struct InGameBlowButton: View {
    var enabled = true
    @State private var blowing = false

    private let visualSize: CGFloat = 40
    private let hitSize: CGFloat = 64

    var body: some View {
        Image(systemName: "wind")
            .font(.system(size: 16, weight: .bold))
            .foregroundColor(.white)
            .frame(width: visualSize, height: visualSize)
            .background(Circle().fill(Color.white.opacity(blowing ? 0.26 : 0)))
            .espressUSurface(Circle())
            .scaleEffect(blowing ? 0.92 : 1)
            .animation(.easeOut(duration: 0.12), value: blowing)
            .opacity(enabled ? 1 : 0.4)
            .frame(width: hitSize, height: hitSize)
            .contentShape(Rectangle())
            .padding(-(hitSize - visualSize) / 2)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in if enabled { setBlowing(true) } }
                    .onEnded { _ in setBlowing(false) }
            )
            .onChange(of: enabled) { isEnabled in
                if !isEnabled { setBlowing(false) }
            }
            .onDisappear { setBlowing(false) }
            .onReceive(NotificationCenter.default.publisher(for: UIApplication.willResignActiveNotification)) { _ in
                setBlowing(false)
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Blow into the microphone")
            .accessibilityHint("Hold to blow.")
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { pulse() }
    }

    private func setBlowing(_ on: Bool) {
        guard on != blowing else { return }
        blowing = on
        cemu_bridge_set_mic_blow(on)
        if on { EspressUHaptics.tap() } else { EspressUHaptics.select() }
    }

    private func pulse() {
        setBlowing(true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { setBlowing(false) }
    }
}

private struct InGamePillPressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(Color.white.opacity(configuration.isPressed ? 0.18 : 0), in: Capsule())
            .scaleEffect(configuration.isPressed ? 0.95 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}
