// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct ThemePickerView: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var store = EspressUThemeStore.shared
    private let currentIconId = UIApplication.shared.alternateIconName
        .map { $0.replacingOccurrences(of: "AltIcon-", with: "") } ?? "original"

    private var orderedThemes: [EspressUThemeDefinition] {
        let all = EspressUThemePresets.all
        guard let matchIndex = all.firstIndex(where: { $0.iconId == currentIconId }) else { return all }
        var rest = all
        let match = rest.remove(at: matchIndex)
        return [match] + rest
    }

    var body: some View {
        EspressUNavigation {
            List {
                Section {
                    ForEach(orderedThemes) { theme in
                        Button {
                            guard theme.id != store.current.id else { return }
                            ScreenHaptics.selectionChanged()
                            store.select(theme)
                        } label: {
                            HStack(spacing: 12) {
                                Circle()
                                    .fill(Color(light: theme.pixelBlueLight, dark: theme.pixelBlueDark))
                                    .frame(width: 26, height: 26)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(theme.name)
                                        .foregroundColor(.primary)
                                    if theme.iconId == currentIconId {
                                        Text("Matches your icon")
                                            .font(.footnote)
                                            .foregroundColor(.secondary)
                                    }
                                }
                                Spacer(minLength: 8)
                                if theme.id == store.current.id {
                                    Image(systemName: "checkmark")
                                        .font(.body.weight(.semibold))
                                        .foregroundColor(EspressUTheme.pixelBlue)
                                }
                            }
                            .contentShape(Rectangle())
                        }
                        .accessibilityAddTraits(theme.id == store.current.id ? [.isButton, .isSelected] : .isButton)
                    }
                }
            }
            .navigationTitle("Accent Colour")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
