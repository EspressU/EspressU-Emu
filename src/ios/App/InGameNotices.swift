// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct BootBackButton: View {
    let action: () -> Void

    var body: some View {
        VStack {
            HStack {
                Button(action: action) {
                    HStack(spacing: 6) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 14, weight: .semibold))
                        Text("Back")
                            .font(.system(size: 14, weight: .semibold, design: .rounded))
                    }
                    .foregroundColor(.white)
                    .padding(.horizontal, 14)
                    .frame(minHeight: 44)
                    .espressUSurface(Capsule())
                    .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .environment(\.colorScheme, .dark)
                .accessibilityHint("Stops starting the game and returns to your games.")
                Spacer()
            }
            Spacer()
        }
        .padding(WindowSafeArea.padding(minimum: 12))
    }
}

struct HeatNoticeModifier: ViewModifier {
    let show: (String) -> Void
    @State private var wasHot = false

    private static var isHot: Bool {
        let state = ProcessInfo.processInfo.thermalState
        return state == .serious || state == .critical
    }

    func body(content: Content) -> some View {
        content
            .onAppear { wasHot = Self.isHot }
            .onReceive(NotificationCenter.default.publisher(for: ProcessInfo.thermalStateDidChangeNotification)) { _ in
                let hot = Self.isHot
                defer { wasHot = hot }
                guard !Self.autoCoolDown else { return }
                if hot && !wasHot {
                    show(Self.hotMessage)
                } else if !hot && wasHot {
                    show("The device has cooled down. The game should run at full speed again.")
                }
            }
    }

    @MainActor private static var autoCoolDown: Bool {
        UserDefaults.standard.object(forKey: ThermalMonitor.autoThrottleKey) as? Bool
            ?? ThermalMonitor.autoThrottleDefault
    }

    @MainActor private static var hotMessage: String {
        "The device is hot, so iOS is slowing the game down. After you quit, try Cool down automatically (Settings, CPU) or a lower Resolution (Settings, Graphics)."
    }
}
