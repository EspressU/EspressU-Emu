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
        NavigationView {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 150), spacing: 16)], spacing: 16) {
                        ForEach(orderedThemes) { theme in
                            ThemeOptionCard(
                                theme: theme,
                                isSelected: theme.id == store.current.id,
                                matchesCurrentIcon: theme.iconId == currentIconId,
                                onSelect: {
                                    guard theme.id != store.current.id else { return }
                                    ScreenHaptics.selectionChanged()
                                    store.select(theme)
                                }
                            )
                        }
                    }
                    .padding(16)
                }
            }
            .navigationTitle("Theme")
            .espressuOpaqueNavigationBar()
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .navigationViewStyle(.stack)
    }
}

private struct ThemeOptionCard: View {
    let theme: EspressUThemeDefinition
    let isSelected: Bool
    let matchesCurrentIcon: Bool
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            EspressUCard {
                VStack(alignment: .leading, spacing: 8) {
                    ZStack(alignment: .topTrailing) {
                        swatchPreview
                            .frame(height: 56)
                            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                            .overlay(
                                RoundedRectangle(cornerRadius: 14, style: .continuous)
                                    .stroke(isSelected ? swatchAccent : swatchOutline, lineWidth: isSelected ? 3 : 1)
                            )

                        if isSelected {
                            Image(systemName: "checkmark.circle.fill")
                                .font(.system(size: 16, weight: .bold))
                                .foregroundColor(EspressUTheme.accentText)
                                .background(EspressUTheme.sparkleCream, in: Circle())
                                .offset(x: 6, y: -6)
                        }
                    }

                    Text(theme.name)
                        .font(.system(size: 13, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                        .lineLimit(1)

                    if matchesCurrentIcon {
                        Text("Matches your icon")
                            .font(.system(size: 10, weight: .semibold, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                    }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .scaleEffect(isSelected ? 1.02 : 1.0)
            .shadow(color: swatchAccent.opacity(isSelected ? 0.28 : 0), radius: 10, x: 0, y: 4)
            .animation(.easeOut(duration: 0.16), value: isSelected)
        }
        .buttonStyle(ScreenCardButtonStyle())
        .accessibilityLabel(theme.name)
        .accessibilityAddTraits(isSelected ? [.isButton, .isSelected] : .isButton)
    }

    private var swatchGradient: LinearGradient {
        let stops = theme.backgroundStopsLight
        guard stops.count >= 2 else {
            return LinearGradient(
                colors: [Color(hex: theme.backgroundTopLight), Color(hex: theme.backgroundBottomLight)],
                startPoint: .topLeading, endPoint: .bottomTrailing
            )
        }
        return LinearGradient(
            colors: stops.map { Color(hex: $0) },
            startPoint: .top, endPoint: .bottom
        )
    }

    private var swatchPreview: some View {
        swatchGradient
            .overlay(alignment: .bottomTrailing) {
                Circle()
                    .fill(Color(hex: theme.pixelBlueLight))
                    .frame(width: 18, height: 18)
                    .padding(6)
            }
    }

    private var swatchAccent: Color { Color(hex: theme.pixelBlueLight) }
    private var swatchOutline: Color { Color(hex: theme.wrapperLight) }
}
