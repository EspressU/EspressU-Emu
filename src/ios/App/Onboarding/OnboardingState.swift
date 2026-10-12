// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

enum OnboardingState {
    static let completedKey = "espressu.onboarding.completed"

    static var hasCompleted: Bool {
        UserDefaults.standard.object(forKey: completedKey) as? Bool ?? false
    }

    static var shouldPresentOnFirstLaunch: Bool {
        !hasCompleted
    }

    static func markCompleted() {
        UserDefaults.standard.set(true, forKey: completedKey)
    }

    static func reset() {
        UserDefaults.standard.removeObject(forKey: completedKey)
    }
}

extension Notification.Name {
    static let espressuReopenOnboarding = Notification.Name("espressu.onboarding.reopen")
}

struct SettingsOnboardingRow: View {
    var onRequestReopen: () -> Void

    var body: some View {
        Button {
            OnboardingState.reset()
            onRequestReopen()
        } label: {
            Label("Show welcome guide again", systemImage: "hand.wave")
        }
        .accessibilityHint("Reopens the first-launch guide to keys, games, speed and controls.")
    }
}
