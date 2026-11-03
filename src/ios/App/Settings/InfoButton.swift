// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI

struct InfoButton: View {
    let title: String
    let text: String
    @State private var showing = false

    var body: some View {
        Button {
            showing = true
        } label: {
            Image(systemName: "info.circle")
                .font(.system(size: 15, weight: .semibold))
                .foregroundColor(EspressUTheme.accentText)
                .frame(width: 30, height: 30)
                .contentShape(Rectangle())
        }
        .buttonStyle(.borderless)
        .accessibilityLabel("About \(title)")
        .sheet(isPresented: $showing) {
            EspressUNavigation {
                ZStack {
                    EspressUTheme.backgroundGradient
                        .ignoresSafeArea()

                    ScrollView {
                        EspressUCard {
                            Text(text)
                                .font(.system(size: 15))
                                .lineSpacing(3)
                                .foregroundColor(EspressUTheme.brownDarkest)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(18)
                        }
                        .padding(16)
                    }
                }
                .navigationTitle(title)
                .espressuOpaqueNavigationBar()
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { showing = false }
                    }
                }
            }
        }
    }
}

extension InfoButton {
    static func footer(_ short: String, title: String, text: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            footerText(short)
            Spacer(minLength: 4)
            InfoButton(title: title, text: text)
                .padding(.trailing, -7)
        }
        .padding(.top, 2)
    }

    static func footer(_ short: String) -> some View {
        footerText(short)
            .padding(.top, 2)
    }

    private static func footerText(_ short: String) -> some View {
        Text(short)
            .font(.footnote)
            .foregroundColor(EspressUTheme.secondaryText)
            .lineSpacing(2.5)
            .fixedSize(horizontal: false, vertical: true)
    }
}
