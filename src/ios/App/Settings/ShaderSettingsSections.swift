import SwiftUI

struct ShaderCompilationSection: View {
    @AppStorage("espressu.shaders.asyncCompile") private var asyncShaderCompile = true

    var body: some View {
        Section {
            Toggle(isOn: $asyncShaderCompile) {
                Text("Compile shaders in the background")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .tint(EspressUTheme.accentText)
            .onValueChange(of: asyncShaderCompile) { newValue in
                cemu_bridge_set_async_shader_compile(newValue)
            }
        } header: {
            SettingsSectionHeader("Shader Compilation", icon: "hammer", accent: .core)
        } footer: {
            InfoButton.footer(
                "On keeps the game running while a shader builds; things may flicker the first time they appear. Off waits for each shader and stutters instead.",
                title: "Shader Compilation",
                text: "On, the game keeps running while new shaders are built, and you may see something flicker or appear late the first time it is drawn. Off, the game waits for each one, which stutters instead.\n\nNano Assault Neo breaks with this on. Set it off for that game only: long-press it in your library.\n\nFavour accuracy (under CPU) always waits for each shader, whatever this is set to. It applies the next time you start a game.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct ShaderCacheSection: View {
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue
    @AppStorage("espressu.shaders.asyncCompile") private var asyncShaderCompile = true
    @State private var learnedCacheBytes: Int64 = 0
    @State private var compiledCacheBytes: Int64 = 0
    @State private var lastUpdated: Date?
    @State private var confirmClearCompiled = false
    @State private var confirmClearLearned = false
    @State private var cacheStatusMessage: String?
    @State private var cacheStatusIsError = false

    var body: some View {
        Section {
            Toggle(isOn: $asyncShaderCompile) {
                Text("Compile shaders in the background")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
            }
            .tint(EspressUTheme.accentText)
            .onValueChange(of: asyncShaderCompile) { newValue in
                cemu_bridge_set_async_shader_compile(newValue)
            }
            SettingsRow(label: "Size on disk", value: Self.formatBytes(learnedCacheBytes + compiledCacheBytes))
            SettingsRow(label: "Last updated", value: ShaderCacheInfo.formatDate(lastUpdated))
            if SettingsMode.isAdvanced(raw: settingsModeRaw) {
                SettingsRow(label: "Learned shaders", value: Self.formatBytes(learnedCacheBytes))
                SettingsRow(label: "Compiled shaders", value: Self.formatBytes(compiledCacheBytes))
            }
            Button { confirmClearCompiled = true } label: {
                Label("Clear compiled shaders", systemImage: "arrow.counterclockwise")
            }
            .disabled(compiledCacheBytes <= 0)
            Button(role: .destructive) { confirmClearLearned = true } label: {
                DestructiveSettingsLabel(title: "Clear everything, including learned", systemImage: "trash")
            }
            .disabled(learnedCacheBytes <= 0 && compiledCacheBytes <= 0)
            if let cacheStatusMessage {
                Text(cacheStatusMessage)
                    .font(.system(size: 12))
                    .foregroundColor(cacheStatusIsError ? .red : EspressUTheme.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
        } header: {
            SettingsSectionHeader("Shaders", icon: "externaldrive", accent: .core)
        } footer: {
            InfoButton.footer("What every game has saved so it starts faster. To clear one game, open its options. Clearing needs the game closed.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear(perform: refreshCacheStats)
        .confirmationDialog("Clear compiled shaders for every game?", isPresented: $confirmClearCompiled, titleVisibility: .visible) {
            Button("Clear compiled shaders") { clear(includeLearned: false) }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("Each game is slow to start once while it rebuilds. Nothing else is lost.")
        }
        .confirmationDialog("Clear learned shaders too?", isPresented: $confirmClearLearned, titleVisibility: .visible) {
            Button("Clear everything", role: .destructive) { clear(includeLearned: true) }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("This clears every game's shaders. Games will stutter while they rebuild them.")
        }
    }

    private func clear(includeLearned: Bool) {
        let freed = cemu_bridge_clear_shader_cache(0, includeLearned)
        if freed < 0 {
            cacheStatusMessage = "Close the game first, then clear the cache."
            cacheStatusIsError = true
        } else {
            cacheStatusMessage = "Freed \(Self.formatBytes(freed)). "
                + (includeLearned ? "Games will stutter while they relearn their shaders." : "The next launch of each game is slow once.")
            cacheStatusIsError = false
        }
        refreshCacheStats()
    }

    private func refreshCacheStats() {
        var learned: Int64 = 0
        var compiled: Int64 = 0
        _ = cemu_bridge_shader_cache_stats(0, &learned, &compiled)
        learnedCacheBytes = learned
        compiledCacheBytes = compiled
        DispatchQueue.global(qos: .userInitiated).async {
            let newest = ShaderCacheInfo.directory.flatMap { ShaderCacheInfo.newestModification(in: $0, namePrefix: "") }
            DispatchQueue.main.async { lastUpdated = newest }
        }
    }

    private static func formatBytes(_ bytes: Int64) -> String { ShaderCacheInfo.formatBytes(bytes) }
}
