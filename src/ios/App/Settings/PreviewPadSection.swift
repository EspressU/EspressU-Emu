// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

struct PreviewPadSection: View {
    @AppStorage(PreviewPadStore.enabledKey) private var previewPadEnabled = PreviewPadStore.defaultEnabled
    @ObservedObject private var previewPad = PreviewPadStore.shared
    @AppStorage(TouchLabSettings.schemeKey) private var touchLabScheme = TouchLabSettings.defaultScheme
    @State private var showingLayoutExporter = false
    @State private var layoutDocument = LazyLayoutDocument(file: nil)
    @State private var showingLayoutImporter = false
    @State private var showingColourExporter = false
    @State private var showingColourImporter = false
    @State private var previewFileErrorMessage: String?
    @State private var previewFileAlertTitle = "Error"
    @State private var showingResetAdjustmentsConfirmation = false

    private var previewLayoutPresetBinding: Binding<String> {
        Binding(get: { previewPad.layoutPreset.rawValue },
               set: { previewPad.layoutPreset = PreviewLayoutPreset(rawValue: $0) ?? .native })
    }
    private var previewColourPresetBinding: Binding<String> {
        Binding(get: { previewPad.customColours == nil ? previewPad.colourPreset.rawValue : Self.customColourTag },
               set: { if $0 != Self.customColourTag { previewPad.colourPreset = PreviewColourPreset(rawValue: $0) ?? .wiiUWhite } })
    }
    private var previewDisplayModeBinding: Binding<String> {
        Binding(get: { previewPad.displayMode.rawValue },
               set: { previewPad.displayMode = PadLayout.DisplayMode(rawValue: $0) ?? .fit })
    }
    private var previewLayoutPreset: PreviewLayoutPreset { previewPad.layoutPreset }
    private static let customColourTag = "custom"

    private var otherPadChosen: Bool {
        TouchLabSettings.isTouchLab(touchLabScheme)
    }

    private var toggleCaption: String {
        if previewPadEnabled && otherPadChosen {
            return "Not showing: a control style is chosen under On-screen Controls, and wins over this."
        }
        return previewPadEnabled
            ? "Replaces the normal pad. If controls don't respond, turn this off."
            : "Experimental. Replaces the normal pad while on."
    }

    var body: some View {
        Section {
            Toggle(isOn: $previewPadEnabled) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Use the new pad system")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(toggleCaption)
                        .font(.system(size: 12))
                        .foregroundColor(previewPadEnabled && !otherPadChosen ? EspressUTheme.cautionText : EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)

            if previewPadEnabled {
                previewControls
            }
        } header: {
            SettingsSectionHeader("Preview: New Pad System",
                                  icon: "wrench.and.screwdriver", accent: .preview)
        } footer: {
            InfoButton.footer(
                "Try a new movable, resizable pad layout. It replaces the normal pad while on, so turn it off if controls stop responding.",
                title: "Preview: New Pad System",
                text: "Every group (shoulders, sticks, d-pad, A/B/X/Y, Start, Select, HOME) can be dragged and pinch-resized. Tap the move icon in the top bar during a game.\n\nFit floats the controls over the screen. Native sizes the picture to fit around the controls: small on a phone, life-size on an iPad.\n\nThis pad is experimental.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .fileExporter(isPresented: $showingLayoutExporter,
                     document: layoutDocument,
                     contentType: .espressuLayout,
                     defaultFilename: previewLayoutPreset.title) { _ in }
        .fileImporter(isPresented: $showingLayoutImporter, allowedContentTypes: [.espressuLayout]) { result in
            importLayout(result)
        }
        .fileExporter(isPresented: $showingColourExporter,
                     document: EspressUColourDocument(previewPad.colourFile),
                     contentType: .espressuColour,
                     defaultFilename: previewPad.colourFile.name) { _ in }
        .fileImporter(isPresented: $showingColourImporter, allowedContentTypes: [.espressuColour]) { result in
            importColour(result)
        }
        .alert(previewFileAlertTitle,
              isPresented: Binding(get: { previewFileErrorMessage != nil },
                                   set: { if !$0 { previewFileErrorMessage = nil } }),
              presenting: previewFileErrorMessage) { _ in
            Button("OK", role: .cancel) { previewFileErrorMessage = nil }
        } message: { message in
            Text(message)
        }
        .confirmationDialog("Reset dragged/resized groups?", isPresented: $showingResetAdjustmentsConfirmation, titleVisibility: .visible) {
            Button("Reset dragged/resized groups", role: .destructive) {
                PreviewPadStore.shared.resetAdjustments()
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Every group goes back to the preset's own positions and sizes. The preset, colours and picture mode you picked are untouched.")
        }
    }

    @ViewBuilder private var previewControls: some View {
        Picker("Layout", selection: previewLayoutPresetBinding) {
            ForEach(PreviewLayoutPreset.allCases) { preset in
                Text(preset.title).tag(preset.rawValue)
            }
        }
        .pickerStyle(.menu)
        .tint(EspressUTheme.accentText)
        Text(previewLayoutPreset.summary)
            .font(.system(size: 12))
            .foregroundColor(EspressUTheme.secondaryText)

        Picker("Colours", selection: previewColourPresetBinding) {
            ForEach(PreviewColourPreset.allCases) { preset in
                Text(preset.file.name).tag(preset.rawValue)
            }
            if let custom = previewPad.customColours {
                Text(custom.name).tag(Self.customColourTag)
            }
        }
        .pickerStyle(.menu)
        .tint(EspressUTheme.accentText)

        Picker("Picture", selection: previewDisplayModeBinding) {
            Text("Fit").tag(PadLayout.DisplayMode.fit.rawValue)
            Text("Native").tag(PadLayout.DisplayMode.native.rawValue)
        }
        .pickerStyle(.segmented)

        Button(role: .destructive) {
            showingResetAdjustmentsConfirmation = true
        } label: {
            DestructiveSettingsLabel(title: "Reset dragged/resized groups", systemImage: "arrow.counterclockwise")
        }

        Button {
            layoutDocument = LazyLayoutDocument(file: currentLayoutFile())
            showingLayoutExporter = true
        } label: {
            Label("Export layout (.espresslyt)", systemImage: "square.and.arrow.up")
        }
        Button {
            showingLayoutImporter = true
        } label: {
            Label("Import layout (.espresslyt)", systemImage: "square.and.arrow.down")
        }
        Button {
            showingColourExporter = true
        } label: {
            Label("Export colours (.espressclr)", systemImage: "square.and.arrow.up")
        }
        Button {
            showingColourImporter = true
        } label: {
            Label("Import colours (.espressclr)", systemImage: "square.and.arrow.down")
        }
    }

    private func currentLayoutFile() -> EspressULayoutFile {
        PreviewPadStore.shared.effectiveLayoutFile(
            container: CGSize(width: 1366, height: 1024),
            safeArea: CGRect(x: 0, y: 0, width: 1366, height: 1004),
            pointsPerInch: 132)
    }

    private func importLayout(_ result: Result<URL, Error>) {
        do {
            let url = try result.get()
            guard url.startAccessingSecurityScopedResource() else {
                previewFileAlertTitle = "Error"
                previewFileErrorMessage = "Couldn't access that file."
                return
            }
            defer { url.stopAccessingSecurityScopedResource() }
            let data = try Data(contentsOf: url)
            let file = try EspressULayoutFile.decode(data)
            PreviewPadStore.shared.applyImportedLayout(file)
        } catch {
            previewFileAlertTitle = "Error"
            previewFileErrorMessage = "Couldn't import that layout file: \(error.localizedDescription)"
        }
    }

    private func importColour(_ result: Result<URL, Error>) {
        do {
            let url = try result.get()
            guard url.startAccessingSecurityScopedResource() else {
                previewFileAlertTitle = "Error"
                previewFileErrorMessage = "Couldn't access that file."
                return
            }
            defer { url.stopAccessingSecurityScopedResource() }
            let data = try Data(contentsOf: url)
            let file = try EspressUColourFile.decode(data)
            PreviewPadStore.shared.applyImportedColours(file)
        } catch {
            previewFileAlertTitle = "Error"
            previewFileErrorMessage = "Couldn't import that colour file: \(error.localizedDescription)"
        }
    }
}

private struct LazyLayoutDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.espressuLayout] }
    var file: EspressULayoutFile?

    init(file: EspressULayoutFile?) { self.file = file }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        file = try EspressULayoutFile.decode(data)
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        guard let file else { throw CocoaError(.fileWriteUnknown) }
        return FileWrapper(regularFileWithContents: try file.encoded())
    }
}
