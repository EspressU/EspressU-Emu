// SPDX-License-Identifier: MPL-2.0

import SwiftUI

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

private struct InGamePillPressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(Color.white.opacity(configuration.isPressed ? 0.18 : 0), in: Capsule())
            .scaleEffect(configuration.isPressed ? 0.95 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}
