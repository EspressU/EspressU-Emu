// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import Metal

enum RendererAPI: Int, CaseIterable, Identifiable {
    case metal = 2
    case vulkan = 1

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .metal:  return "Metal"
        case .vulkan: return "Vulkan (Experimental)"
        }
    }

    static let storageKey = "espressu.render.graphicsAPI"
    static let defaultValue: RendererAPI = .metal
}

enum ScaleFilter: Int, CaseIterable, Identifiable {
    case linear = 0
    case bicubic = 1
    case bicubicHermite = 2
    case nearestNeighbor = 3

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .linear:         return "Linear"
        case .bicubic:        return "Bicubic"
        case .bicubicHermite: return "Bicubic Hermite"
        case .nearestNeighbor: return "Nearest Neighbor"
        }
    }
}

enum UpscaleFilterSetting {
    static let storageKey = "espressu.render.upscaleFilter"
    static let defaultValue = ScaleFilter.bicubic
}

enum DownscaleFilterSetting {
    static let storageKey = "espressu.render.downscaleFilter"
    static let defaultValue = ScaleFilter.linear
}

enum DisplayGammaSetting {
    static let storageKey = "espressu.render.displayGamma"
    static let defaultValue: Double = 2.2
    static let minValue: Double = 1.0
    static let maxValue: Double = 3.0
}

enum OverrideGammaSetting {
    static let storageKey = "espressu.render.overrideGammaValue"
    static let defaultValue: Double = 2.2
    static let minValue: Double = 1.0
    static let maxValue: Double = 3.0
}

enum MoltenVKBuild: String, CaseIterable, Identifiable {
    case v143 = "1.4.3"
    case v128 = "1.2.8"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .v143: return "1.4.3 (default)"
        case .v128: return "1.2.8"
        }
    }

    static let storageKey = "espressu.render.moltenVK"
    static let defaultValue: MoltenVKBuild = .v143
}

struct GraphicsSettingsSection: View {
    @AppStorage(RendererAPI.storageKey) private var rendererRaw = RendererAPI.defaultValue.rawValue
    @AppStorage(UpscaleFilterSetting.storageKey) private var upscaleRaw = UpscaleFilterSetting.defaultValue.rawValue
    @AppStorage(DownscaleFilterSetting.storageKey) private var downscaleRaw = DownscaleFilterSetting.defaultValue.rawValue
    @AppStorage(RenderScale.storageKey) private var renderScaleRaw = RenderScale.deviceDefault.rawValue
    @AppStorage(FavourPerformance.storageKey) private var favourPerformance = FavourPerformance.defaultValue
    @AppStorage(FullSpeedRenders.storageKey) private var fullSpeedRenders = FullSpeedRenders.defaultValue
    @AppStorage(FullSpeedRenders.shaderModeKey) private var fullSpeedShaderMode = FullSpeedRenders.defaultShaderMode.rawValue
    @AppStorage(FrameStretch.storageKey) private var frameStretchEnabled = FrameStretch.defaultValue
    @AppStorage(MoltenVKBuild.storageKey) private var moltenVKRaw = MoltenVKBuild.defaultValue.rawValue
    @AppStorage("espressu.render.upsideDown") private var upsideDownEnabled = false
    @AppStorage(DisplayGammaSetting.storageKey) private var displayGamma = DisplayGammaSetting.defaultValue
    @AppStorage("espressu.render.framebufferFetch") private var framebufferFetchEnabled = true
    @AppStorage("espressu.render.overrideAppGamma") private var overrideAppGammaEnabled = false
    @AppStorage(OverrideGammaSetting.storageKey) private var overrideGammaValue = OverrideGammaSetting.defaultValue
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    private var renderScale: RenderScale {
        RenderScale(rawValue: renderScaleRaw) ?? RenderScale.deviceDefault
    }

    var body: some View {
        Section {
            if advanced {
                fullSpeedRendersToggle
                if fullSpeedRenders {
                    fullSpeedShaderPicker
                }
                rendererPicker
                if rendererRaw == RendererAPI.vulkan.rawValue {
                    moltenVKPicker
                }
                upscalePicker
                downscalePicker
            }
            resolutionPicker
            if favourPerformance {
                Text("Favour performance is on (Settings > CPU), so the picture is drawn at Balanced at most, with linear scaling.")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            stretchToggle
            if advanced {
                upsideDownToggle
                if rendererRaw == RendererAPI.metal.rawValue {
                    framebufferFetchToggle
                }
                gammaSlider
                overrideGammaToggle
                if overrideAppGammaEnabled {
                    overrideGammaSlider
                }
            }
            meshShaderNote
        } header: {
            SettingsSectionHeader("Graphics", icon: "cube.transparent", accent: .core)
        } footer: {
            InfoButton.footer(
                "Metal is the default renderer. Vulkan (MoltenVK) may work better for some games; it applies on the next launch.",
                title: "Graphics",
                text: fullText)
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var rendererPicker: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Renderer")
                .font(.system(size: 13, weight: .semibold, design: .rounded))
            Picker("Renderer", selection: $rendererRaw) {
                ForEach(RendererAPI.allCases) { api in
                    Text(api.title).tag(api.rawValue)
                }
            }
            .pickerStyle(.segmented)
            .onValueChange(of: rendererRaw) { newValue in
                if newValue == RendererAPI.vulkan.rawValue {
                    UserDefaults.standard.removeObject(forKey: "espressu.render.vulkanFailedBuild")
                    UserDefaults.standard.removeObject(forKey: "espressu.render.vulkanFailureReason")
                }
            }
            Text("Experimental: Vulkan runs through MoltenVK on top of Metal. Some games may draw wrongly or stop, and if Vulkan fails the next launch switches back to Metal.")
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
            if let reason = UserDefaults.standard.string(forKey: "espressu.render.vulkanFailureReason"), !reason.isEmpty {
                Text("Last Vulkan failure: \(reason)")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            if let failedBuild = UserDefaults.standard.string(forKey: "espressu.render.vulkanFailedBuild"),
               rendererRaw == RendererAPI.metal.rawValue {
                Text("Vulkan didn't start on this device with MoltenVK \(failedBuild). Metal is in use. Choosing Vulkan again tries it again.")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        }
    }

    private var moltenVKPicker: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("MoltenVK")
                .font(.system(size: 13, weight: .semibold, design: .rounded))
            Picker("MoltenVK", selection: $moltenVKRaw) {
                ForEach(MoltenVKBuild.allCases) { build in
                    Text(build.title).tag(build.rawValue)
                }
            }
            .pickerStyle(.segmented)
            Text(moltenVKCaption)
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
        }
    }

    private var moltenVKCaption: String {
        let active = String(cString: cemu_bridge_active_moltenvk())
        if !active.isEmpty && active != moltenVKRaw {
            return "Running \(active) now. \(moltenVKRaw) is used from the next launch of EspressU."
        }
        return "Used by the Vulkan renderer only. A change applies the next time EspressU launches."
    }

    private var upscalePicker: some View {
        Picker("Filter when enlarging", selection: $upscaleRaw) {
            ForEach(ScaleFilter.allCases) { filter in
                Text(filter.title).tag(filter.rawValue)
            }
        }
        .pickerStyle(.menu)
        .tint(EspressUTheme.accentText)
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var downscalePicker: some View {
        Picker("Filter when shrinking", selection: $downscaleRaw) {
            ForEach(ScaleFilter.allCases) { filter in
                Text(filter.title).tag(filter.rawValue)
            }
        }
        .pickerStyle(.menu)
        .tint(EspressUTheme.accentText)
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var fullSpeedRendersToggle: some View {
        Toggle(isOn: $fullSpeedRenders) {
            VStack(alignment: .leading, spacing: 2) {
                Text("Steady frame rate")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                Text(fullSpeedRenders
                     ? "Rendering comes first: steady frames at the game's own Wii U frame rate, never faster."
                     : "Off: frames are shown the moment they're ready.")
                    .font(.system(size: 12))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: fullSpeedRenders) { _ in FullSpeedRenders.applyToBridge() }
    }

    private var fullSpeedShaderPicker: some View {
        VStack(alignment: .leading, spacing: 4) {
            Picker("First time a new effect appears", selection: $fullSpeedShaderMode) {
                ForEach(FullSpeedRenders.ShaderMode.allCases) { mode in
                    Text(mode.title).tag(mode.rawValue)
                }
            }
            .pickerStyle(.menu)
            .tint(EspressUTheme.accentText)
            .foregroundColor(EspressUTheme.brownDarkest)
            .onValueChange(of: fullSpeedShaderMode) { _ in FullSpeedRenders.applyToBridge() }
            Text((FullSpeedRenders.ShaderMode(rawValue: fullSpeedShaderMode) ?? FullSpeedRenders.defaultShaderMode).summary)
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var resolutionPicker: some View {
        Picker("Resolution", selection: $renderScaleRaw) {
            ForEach(RenderScale.allCases) { scale in
                Text(scale.title).tag(scale.rawValue)
            }
        }
        .pickerStyle(.menu)
        .tint(EspressUTheme.accentText)
        .foregroundColor(EspressUTheme.brownDarkest)
    }

    private var stretchToggle: some View {
        Toggle(isOn: $frameStretchEnabled) {
            Text("Stretch picture to fill the screen")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: frameStretchEnabled) { newValue in
            cemu_bridge_set_stretch_to_fill(newValue)
        }
    }

    private var upsideDownToggle: some View {
        Toggle(isOn: $upsideDownEnabled) {
            Text("Flip screen upside down")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: upsideDownEnabled) { newValue in
            cemu_bridge_set_render_upside_down(newValue)
        }
    }

    private var gammaSlider: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Display gamma")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text(String(format: "%.1f", displayGamma))
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            Slider(
                value: $displayGamma,
                in: DisplayGammaSetting.minValue...DisplayGammaSetting.maxValue
            )
            .accessibilityLabel("Display gamma")
            .onValueChange(of: displayGamma) { newValue in
                cemu_bridge_set_display_gamma(Float(newValue))
            }
        }
    }

    private var framebufferFetchToggle: some View {
        Toggle(isOn: $framebufferFetchEnabled) {
            Text("Fast blending")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: framebufferFetchEnabled) { newValue in
            cemu_bridge_set_framebuffer_fetch(newValue)
        }
    }

    private var overrideGammaToggle: some View {
        Toggle(isOn: $overrideAppGammaEnabled) {
            Text("Override the game's gamma")
                .font(.system(size: 15, weight: .semibold, design: .rounded))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: overrideAppGammaEnabled) { newValue in
            cemu_bridge_set_override_app_gamma(newValue)
        }
    }

    private var overrideGammaSlider: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Override gamma")
                    .font(.system(size: 13, weight: .semibold, design: .rounded))
                Spacer()
                Text(String(format: "%.1f", overrideGammaValue))
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            Slider(
                value: $overrideGammaValue,
                in: OverrideGammaSetting.minValue...OverrideGammaSetting.maxValue
            )
            .accessibilityLabel("Override gamma")
            .onValueChange(of: overrideGammaValue) { newValue in
                cemu_bridge_set_override_gamma_value(Float(newValue))
            }
        }
    }

    private var meshShadersUnsupported: Bool {
        !DeviceCapabilities.current.meshShaders
    }

    @ViewBuilder private var meshShaderNote: some View {
        if meshShadersUnsupported {
            Text("This device doesn't support mesh shaders, so some graphic packs may not render correctly. See Graphic Packs under Library.")
                .font(.system(size: 12))
                .foregroundColor(EspressUTheme.secondaryText)
        }
    }

    private var fullText: String {
        """
        Steady frame rate puts rendering ahead of everything else EspressU does: the graphics thread gets first call on the fastest cores, each frame is shown at the game's own Wii U frame rate (steady, and never faster than the console), and debugging extras are skipped. Every shader the game has used before is built while the game loads, so those never cause a hitch or a flicker. The first time something brand new appears, its shader has to be built: "Wait for it" holds the frame until it's ready (a short hitch, never a missing object), "Keep going" draws without it once (no hitch, but it can be missing for a moment). Either way it's saved, so it happens only once. This choice wins over Favour accuracy, Favour performance and Compile shaders in the background. Takes effect the next time you start a game.

        Metal is the default renderer. Vulkan (MoltenVK) goes through a translation layer and may work better for some games, at some cost to speed. Takes effect the next time you launch a game.

        MoltenVK is the layer that turns Vulkan into Metal, so it only matters with the Vulkan renderer. 1.4.3 is the default; 1.2.8 is an older build that some games run better on. A change applies the next time EspressU starts.

        Filter when enlarging is used when EspressU draws the game's picture larger than the game rendered it; Filter when shrinking is used when drawing it smaller. Bicubic (the default for enlarging) is smoother than linear; Bicubic Hermite sharpens that further; Nearest Neighbor keeps hard pixel edges with no blending at all. Linear is the default for shrinking.

        \(renderScale.summary)

        Resolution changes the size of the picture EspressU draws, not the resolution the game runs at - nothing about the emulation changes with it. Takes effect the next time you launch a game.

        Stretch picture to fill the screen fills the screen's own shape instead of keeping the Wii U's 1280x720 proportions, which otherwise letterboxes with bars on two sides. Off keeps the picture undistorted; on trades that for using every pixel. Takes effect on the very next frame.

        There's no VSync switch because iOS always syncs frames to the screen's own refresh, so the picture never tears and there is nothing to turn off. For even frame pacing, use Steady frame rate at the top of this page.

        Flip screen upside down turns both Wii U screens vertically before they reach the screen. Off for everyone except a panel or capture rig that presents the image inverted. Takes effect on the next frame.

        Fast blending lets some Metal shaders blend using the picture already on screen instead of an extra step, which is quicker. It is on by default, Metal only, and takes effect the next time you launch a game.

        Display gamma adjusts how bright the mid-tones look without changing pure black or pure white. 2.2 is the standard display gamma and the default; lower looks flatter and brighter in the mids, higher looks more contrasty and darker in the mids. Takes effect on the next frame.

        Override the game's gamma and Override gamma are a separate stage from Display gamma above, not a second copy of it: some games ask for their own gamma value, and this either adds Override gamma on top of that request (off) or replaces the game's request with Override gamma entirely (on) - before Display gamma is applied to the result. Off by default; most games never ask for a specific gamma at all, so this has nothing to override until one does.
        """
        + (meshShadersUnsupported ? "\n\nThis device doesn't support mesh shaders, so graphic packs that rely on geometry shaders or post-processing (RECTS) draws may not render correctly. Everything else works normally." : "")
    }
}
