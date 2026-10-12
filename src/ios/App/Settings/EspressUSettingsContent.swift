// SPDX-License-Identifier: MPL-2.0

import SwiftUI
import UIKit

extension EspressUSettingsView {
    func groups(for category: SettingsCategory) -> [SettingsGroup] {
        var groups: [SettingsGroup]
        var advanced: [SettingsItem] = []
        var advancedHint = ""
        switch category {
        case .jit:
            groups = jitGroups
        case .performance:
            groups = performanceGroups
        case .graphics:
            groups = graphicsGroups
            advanced = graphicsAdvanced
            advancedHint = "Renderer, scaling filters, gamma."
        case .controls:
            groups = controlsGroups
        case .screenSound:
            groups = screenSoundGroups
            advanced = screenSoundAdvanced
            advancedHint = "Audio channels."
        case .gamesData:
            groups = gamesDataGroups
        case .system:
            groups = systemGroups
        }
        if !advanced.isEmpty {
            let open = expanded.contains(category)
            groups.append(SettingsGroup(id: "advanced.\(category.rawValue)", title: "", items: [
                SettingsItem(id: "advanced.toggle.\(category.rawValue)", title: "Advanced", detail: advancedHint,
                             control: .expand(open))
            ] + (open ? advanced : [])))
        }
        return groups
    }

    private var performanceGroups: [SettingsGroup] {
        let coreModeRaw = UserDefaults.standard.string(forKey: CoreMode.storageKey) ?? CoreMode.current.rawValue
        let coreMode = CoreMode(rawValue: coreModeRaw) ?? CoreMode.defaultValue
        let multicore = DeviceCapabilities.current.multicoreViable
        let cpu: [SettingsItem] = [
            SettingsItem(id: "cpu.cores", title: "CPU cores",
                         detail: multicore ? coreMode.summary : DeviceCapabilities.oneCoreOnlyText,
                         control: .choice([SettingsOption(id: CoreMode.auto.rawValue, title: "Auto"),
                                           SettingsOption(id: CoreMode.single.rawValue, title: "One"),
                                           SettingsOption(id: CoreMode.multi.rawValue, title: "Three")],
                                          values.custom(get: { coreModeRaw }, set: { raw in
                                              UserDefaults.standard.set(raw, forKey: CoreMode.storageKey)
                                              cemu_bridge_set_cpu_core_mode((CoreMode(rawValue: raw) ?? CoreMode.defaultValue).bridgeValue)
                                          })),
                         enabled: multicore),
            SettingsItem(id: "cpu.mode", title: "Running on", detail: String(cString: cemu_bridge_cpu_mode_detail()),
                         control: .info(Self.cpuModeTitle, nil)),
            SettingsItem(id: "cpu.memory", title: "Memory", detail: String(cString: cemu_bridge_memory_headroom_summary()),
                         control: .info("", nil)),
        ]

        let heat: [SettingsItem] = [
            SettingsItem(id: "heat.auto", title: "Cool down automatically",
                         detail: "Lowers resolution and CPU load when the device gets hot.",
                         control: .toggle(values.bool(ThermalMonitor.autoThrottleKey, ThermalMonitor.autoThrottleDefault))),
            SettingsItem(id: "heat.status", title: "Device heat", detail: "What iOS reports right now.",
                         control: .custom(AnyView(HeatStatusBadge()))),
        ]

        return [
            SettingsGroup(id: "cpu", title: "CPU", items: cpu),
            SettingsGroup(id: "heat", title: "Heat", items: heat),
        ]
    }

    private static var cpuModeTitle: String {
        switch cemu_bridge_cpu_mode() {
        case 2: return "Recompiler"
        case 1: return "Interpreter"
        default: return "Not decided yet"
        }
    }

    private var graphicsGroups: [SettingsGroup] {
        let defaults = UserDefaults.standard
        let favourPerformance = defaults.object(forKey: FavourPerformance.storageKey) as? Bool ?? FavourPerformance.defaultValue
        let scale = RenderScale(rawValue: defaults.string(forKey: RenderScale.storageKey) ?? "") ?? RenderScale.deviceDefault
        let steady = FullSpeedRenders.isEnabled

        var picture: [SettingsItem] = [
            SettingsItem(id: "gfx.resolution", title: "Resolution",
                         detail: favourPerformance ? "Favour performance is on, so at most Balanced." : scale.summary,
                         control: .choice(RenderScale.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                                          values.string(RenderScale.storageKey, RenderScale.deviceDefault.rawValue))),
            SettingsItem(id: "gfx.stretch", title: "Stretch to fill",
                         detail: "Fills the screen instead of keeping 16:9.",
                         control: .toggle(values.bool(FrameStretch.storageKey, FrameStretch.defaultValue) { cemu_bridge_set_stretch_to_fill($0) })),
            SettingsItem(id: "gfx.steady", title: "Steady frame rate",
                         detail: "Even frames at the game's own speed, never faster.",
                         control: .toggle(values.bool(FullSpeedRenders.storageKey, FullSpeedRenders.defaultValue) { _ in
                             FullSpeedRenders.applyToBridge()
                         })),
        ]
        if steady {
            picture.append(SettingsItem(id: "gfx.steadyShaders", title: "When a new effect appears",
                                        detail: FullSpeedRenders.shaderMode.summary,
                                        control: .choice([SettingsOption(id: String(FullSpeedRenders.ShaderMode.waitForIt.rawValue), title: "Wait"),
                                                          SettingsOption(id: String(FullSpeedRenders.ShaderMode.keepGoing.rawValue), title: "Keep going")],
                                                         values.intChoice(FullSpeedRenders.shaderModeKey, FullSpeedRenders.defaultShaderMode.rawValue) { _ in
                                                             FullSpeedRenders.applyToBridge()
                                                         })))
        }

        let shaders: [SettingsItem] = [
            SettingsItem(id: "shader.async", title: "Compile shaders in the background",
                         detail: "The game keeps running; new effects can flicker the first time.",
                         control: .toggle(values.bool("espressu.shaders.asyncCompile", true) { cemu_bridge_set_async_shader_compile($0) })),
            SettingsItem(id: "shader.size", title: "Saved shaders", detail: "What every game has saved so it starts faster.",
                         control: .info(ShaderCacheInfo.formatBytes(shaderLearned + shaderCompiled), nil)),
            SettingsItem(id: "shader.clearCompiled", title: "Clear compiled shaders",
                         detail: "Frees space. Each game is slow to start once.",
                         control: .action("Clear", destructive: false, confirm: true, run: { clearShaders(includeLearned: false) }),
                         enabled: shaderCompiled > 0),
            SettingsItem(id: "shader.clearAll", title: "Clear everything",
                         detail: "Also the learned shaders. Games stutter while they relearn them.",
                         control: .action("Clear", destructive: true, confirm: true, run: { clearShaders(includeLearned: true) }),
                         enabled: shaderLearned + shaderCompiled > 0),
            SettingsItem(id: "gfx.packs", title: "Graphic packs", detail: "Fixes and extras for each game.",
                         control: .link("", .graphicPacks)),
        ]

        return [
            SettingsGroup(id: "picture", title: "Picture", items: picture),
            SettingsGroup(id: "shaders", title: "Shaders", items: shaders),
        ]
    }

    private var graphicsAdvanced: [SettingsItem] {
        let defaults = UserDefaults.standard
        let renderer = defaults.object(forKey: RendererAPI.storageKey) as? Int ?? RendererAPI.defaultValue.rawValue
        let overrideGamma = defaults.object(forKey: "espressu.render.overrideAppGamma") as? Bool ?? false
        let filters = ScaleFilter.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }
        let gammaLabel: (Double) -> String = { String(format: "%.1f", $0) }

        var items: [SettingsItem] = [
            SettingsItem(id: "gfx.renderer", title: "Renderer",
                         detail: "Metal is the fastest. Vulkan is experimental; applies to the next game.",
                         control: .choice([SettingsOption(id: String(RendererAPI.metal.rawValue), title: "Metal"),
                                           SettingsOption(id: String(RendererAPI.vulkan.rawValue), title: "Vulkan")],
                                          values.intChoice(RendererAPI.storageKey, RendererAPI.defaultValue.rawValue) { value in
                                              if value == RendererAPI.vulkan.rawValue {
                                                  UserDefaults.standard.removeObject(forKey: "espressu.render.vulkanFailedBuild")
                                                  UserDefaults.standard.removeObject(forKey: "espressu.render.vulkanFailureReason")
                                              }
                                          })),
        ]
        if renderer == RendererAPI.vulkan.rawValue {
            items.append(SettingsItem(id: "gfx.moltenvk", title: "MoltenVK", detail: "Applies the next time EspressU starts.",
                                      control: .choice(MoltenVKBuild.allCases.map { SettingsOption(id: $0.rawValue, title: $0.rawValue) },
                                                       values.string(MoltenVKBuild.storageKey, MoltenVKBuild.defaultValue.rawValue))))
        }
        items += [
            SettingsItem(id: "gfx.upscale", title: "Filter when enlarging", detail: "Used when the picture is drawn larger.",
                         control: .choice(filters, values.intChoice(UpscaleFilterSetting.storageKey, UpscaleFilterSetting.defaultValue.rawValue))),
            SettingsItem(id: "gfx.downscale", title: "Filter when shrinking", detail: "Used when the picture is drawn smaller.",
                         control: .choice(filters, values.intChoice(DownscaleFilterSetting.storageKey, DownscaleFilterSetting.defaultValue.rawValue))),
            SettingsItem(id: "gfx.flip", title: "Flip screen upside down", detail: "Only for screens that show the picture inverted.",
                         control: .toggle(values.bool("espressu.render.upsideDown", false) { cemu_bridge_set_render_upside_down($0) })),
        ]
        if renderer == RendererAPI.metal.rawValue {
            items.append(SettingsItem(id: "gfx.fetch", title: "Fast blending", detail: "A quicker way to blend on Metal. Applies to the next game.",
                                      control: .toggle(values.bool("espressu.render.framebufferFetch", true) { cemu_bridge_set_framebuffer_fetch($0) })))
        }
        items += [
            SettingsItem(id: "gfx.gamma", title: "Display gamma", detail: "2.2 is standard. Lower is brighter in the mid-tones.",
                         control: .slider(values.double(DisplayGammaSetting.storageKey, DisplayGammaSetting.defaultValue) { cemu_bridge_set_display_gamma(Float($0)) },
                                          DisplayGammaSetting.minValue...DisplayGammaSetting.maxValue, step: 0.1, label: gammaLabel)),
            SettingsItem(id: "gfx.overrideGamma", title: "Override the game's gamma", detail: "Replaces the gamma a game asks for.",
                         control: .toggle(values.bool("espressu.render.overrideAppGamma", false) { cemu_bridge_set_override_app_gamma($0) })),
        ]
        if overrideGamma {
            items.append(SettingsItem(id: "gfx.overrideGammaValue", title: "Override gamma", detail: nil,
                                      control: .slider(values.double(OverrideGammaSetting.storageKey, OverrideGammaSetting.defaultValue) { cemu_bridge_set_override_gamma_value(Float($0)) },
                                                       OverrideGammaSetting.minValue...OverrideGammaSetting.maxValue, step: 0.1, label: gammaLabel)))
        }
        return items
    }

    private func clearShaders(includeLearned: Bool) {
        let freed = cemu_bridge_clear_shader_cache(0, includeLearned)
        let id = includeLearned ? "shader.clearAll" : "shader.clearCompiled"
        notes[id] = freed < 0 ? "Close the game first, then clear." : "Freed \(ShaderCacheInfo.formatBytes(freed))."
        refreshShaderStats()
    }

    func refreshShaderStats() {
        var learned: Int64 = 0
        var compiled: Int64 = 0
        _ = cemu_bridge_shader_cache_stats(0, &learned, &compiled)
        shaderLearned = learned
        shaderCompiled = compiled
    }

    private var controlsGroups: [SettingsGroup] {
        let percent: (Double) -> String = { "\(Int(($0 * 100).rounded()))%" }

        let touch: [SettingsItem] = [
            SettingsItem(id: "touch.size", title: "Button size", detail: "Bigger buttons are easier to hit.",
                         control: .slider(values.double(ControllerLayoutSettings.scaleKey, ControllerLayoutSettings.defaultScale),
                                          ControllerLayoutSettings.minScale...ControllerLayoutSettings.maxScale, step: 0.05, label: percent)),
            SettingsItem(id: "touch.opacity", title: "Opacity", detail: "How see-through the buttons are.",
                         control: .slider(values.double(ControllerLayoutSettings.opacityKey, ControllerLayoutSettings.defaultOpacity),
                                          0.2...1.0, step: 0.05, label: percent)),
            SettingsItem(id: "touch.haptics", title: "Haptic feedback", detail: "A light tap when you press a button.",
                         control: .toggle(values.bool(ControllerLayoutSettings.hapticsKey, ControllerLayoutSettings.defaultHaptics))),
            SettingsItem(id: "touch.autoHide", title: "Hide with a controller",
                         detail: "Touch controls disappear while a controller is connected.",
                         control: .toggle(values.bool(ControllerLayoutSettings.autoHideWithControllerKey,
                                                      ControllerLayoutSettings.defaultAutoHideWithController))),
            SettingsItem(id: "touch.more", title: "Layout, sticks and more",
                         detail: "Move buttons, analog sticks, the controller style.",
                         control: .link("", .touchControls)),
        ]

        let defaults = UserDefaults.standard
        let motionOn = defaults.object(forKey: MotionSettings.enabledKey) as? Bool ?? MotionSettings.defaultEnabled
        let motionSource = defaults.object(forKey: MotionSettings.sourceKey) as? Int ?? MotionSettings.defaultSource
        let motionChoice = !motionOn ? "off"
            : (motionSource == MotionSettings.sourceController ? "controller" : (motionSource == MotionSettings.sourceDevice ? "device" : "auto"))
        var motion: [SettingsItem] = [
            SettingsItem(id: "motion.source", title: "Motion from", detail: "What moves the GamePad's gyro. Automatic uses a connected controller, otherwise this device.",
                         control: .choice([SettingsOption(id: "auto", title: "Automatic"),
                                           SettingsOption(id: "device", title: "This device"),
                                           SettingsOption(id: "controller", title: "Controller"),
                                           SettingsOption(id: "off", title: "Off")],
                                          values.custom(get: { motionChoice }, set: { choice in
                                              let d = UserDefaults.standard
                                              d.set(choice != "off", forKey: MotionSettings.enabledKey)
                                              if choice != "off" {
                                                  d.set(choice == "controller" ? MotionSettings.sourceController
                                                        : (choice == "device" ? MotionSettings.sourceDevice : MotionSettings.sourceAutomatic),
                                                        forKey: MotionSettings.sourceKey)
                                              }
                                              MotionSettings.applyToBridge()
                                          }))),
        ]
        if motionOn {
            motion += [
                SettingsItem(id: "motion.sensitivity", title: "Sensitivity", detail: "How far the aim moves when you tilt.",
                             control: .slider(values.double(MotionSettings.sensitivityKey, MotionSettings.defaultSensitivity) { _ in
                                 MotionSettings.applyToBridge()
                             }, MotionSettings.minSensitivity...MotionSettings.maxSensitivity, step: 0.25,
                                              label: { String(format: "%.2f×", $0) })),
            ]
        }

        let names = input.controllers.map { $0.vendorName ?? "Controller" }
        let controllers: [SettingsItem] = [
            SettingsItem(id: "pad.connected", title: "Connected controllers",
                         detail: names.isEmpty ? "Pair one in the iOS Bluetooth settings."
                             : names.enumerated().map { "Player \($0.offset + 1): \($0.element)" }.joined(separator: "\n"),
                         control: .info(names.isEmpty ? "None" : "\(names.count)",
                                        names.isEmpty ? nil : EspressUTheme.accentText)),
            SettingsItem(id: "pad.players", title: "More controllers are more players",
                         detail: "The first controller is Player 1 on the GamePad. The next ones play as Player 2 to 4. Applies the next time you start a game.",
                         control: .toggle(values.bool("espressu.input.extraPads", true))),
            SettingsItem(id: "pad.playerType", title: "Players 2 to 4 use",
                         detail: "Most games take a Pro Controller; some older ones want the Classic Controller.",
                         control: .choice([SettingsOption(id: "0", title: "Pro Controller"),
                                           SettingsOption(id: "1", title: "Classic Controller")],
                                          values.intChoice("espressu.input.extraPadType", 0)),
                         enabled: UserDefaults.standard.object(forKey: "espressu.input.extraPads") as? Bool ?? true),
            SettingsItem(id: "pad.figures", title: "Figure portals",
                         detail: "Skylanders, Disney Infinity and LEGO Dimensions.",
                         control: .link("", .figures)),
        ]

        return [
            SettingsGroup(id: "touch", title: "Touch controls", items: touch),
            SettingsGroup(id: "motion", title: "Motion", items: motion),
            SettingsGroup(id: "controllers", title: "Controllers", items: controllers),
        ]
    }

    private var screenSoundGroups: [SettingsGroup] {
        let defaults = UserDefaults.standard
        let layout = ScreenLayout(rawValue: defaults.string(forKey: LocalScreenLayoutSettings.layoutKey) ?? "") ?? .singleScreen

        var screen: [SettingsItem] = [
            SettingsItem(id: "screen.layout", title: "Screen layout", detail: layout.description,
                         control: .choice([SettingsOption(id: ScreenLayout.singleScreen.rawValue, title: "One screen"),
                                           SettingsOption(id: ScreenLayout.bothScreens.rawValue, title: "Both screens"),
                                           SettingsOption(id: ScreenLayout.smallGamePadTopRight.rawValue, title: "TV with small GamePad")],
                                          values.string(LocalScreenLayoutSettings.layoutKey, ScreenLayout.singleScreen.rawValue))),
        ]
        if layout == .singleScreen {
            screen.append(SettingsItem(id: "screen.swap", title: "Show swap button",
                                       detail: "A small button to switch between TV and GamePad.",
                                       control: .toggle(values.bool(LocalScreenLayoutSettings.showSwapButtonKey,
                                                                    LocalScreenLayoutSettings.defaultShowSwapButton))))
        }
        screen += [
            SettingsItem(id: "screen.boost", title: "Brightness boost",
                         detail: "Lightens the picture beyond the screen's maximum.",
                         control: .slider(values.double(BrightnessBoost.key, BrightnessBoost.defaultValue),
                                          0...BrightnessBoost.maxValue, step: 0.1, label: { BrightnessBoost.label(for: $0) })),
            SettingsItem(id: "screen.maxBrightness", title: "Max brightness while playing",
                         detail: "Full brightness during a game, back afterwards.",
                         control: .toggle(values.bool(ScreenBrightness.maxWhilePlayingKey, ScreenBrightness.defaultMaxWhilePlaying))),
        ]
        screen.append(SettingsItem(id: "screen.overlay", title: "Overlay and notifications",
                                   detail: "Frame rate counter and messages over the game.",
                                   control: .link("", .overlay)))

        let tvOn = defaults.object(forKey: AudioSettings.tvEnabledKey) as? Bool ?? AudioSettings.defaultTvEnabled
        let padOn = defaults.object(forKey: AudioSettings.padEnabledKey) as? Bool ?? AudioSettings.defaultPadEnabled
        let micOn = defaults.object(forKey: AudioSettings.microphoneEnabledKey) as? Bool ?? AudioSettings.defaultMicrophoneEnabled
        let volume: (Double) -> String = { "\(Int($0.rounded()))%" }
        var sound: [SettingsItem] = [
            SettingsItem(id: "audio.tv", title: "TV sound", detail: "The main game sound.",
                         control: .toggle(values.bool(AudioSettings.tvEnabledKey, AudioSettings.defaultTvEnabled) { cemu_bridge_set_tv_audio_enabled($0) })),
        ]
        if tvOn {
            sound.append(SettingsItem(id: "audio.tvVolume", title: "TV volume", detail: nil,
                                      control: .slider(values.intSlider(AudioSettings.tvVolumeKey, AudioSettings.defaultTvVolume) { cemu_bridge_set_tv_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound.append(SettingsItem(id: "audio.pad", title: "GamePad sound", detail: "What the game sends to the GamePad speaker.",
                                  control: .toggle(values.bool(AudioSettings.padEnabledKey, AudioSettings.defaultPadEnabled) { cemu_bridge_set_pad_audio_enabled($0) })))
        if padOn {
            sound.append(SettingsItem(id: "audio.padVolume", title: "GamePad volume", detail: nil,
                                      control: .slider(values.intSlider(AudioSettings.padVolumeKey, AudioSettings.defaultPadVolume) { cemu_bridge_set_pad_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound.append(SettingsItem(id: "audio.mic", title: "Use the microphone", detail: "Only for games that listen to the GamePad mic.",
                                  control: .toggle(values.custom(get: { micOn }, set: { on in setMicrophone(on) }))))
        if micOn {
            sound.append(SettingsItem(id: "audio.micVolume", title: "Microphone volume", detail: "50% is normal.",
                                      control: .slider(values.intSlider(AudioSettings.inputVolumeKey, AudioSettings.defaultInputVolume) { cemu_bridge_set_input_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound += [
            SettingsItem(id: "audio.silent", title: "Respect silent mode", detail: "No game sound while the device is on silent.",
                         control: .toggle(values.bool(MenuMusic.respectSilentModeKey, false) { _ in MenuMusic.shared.restart() })),
        ]

        return [
            SettingsGroup(id: "screen", title: "Screen", items: screen),
            SettingsGroup(id: "sound", title: "Sound", items: sound),
        ]
    }

    private var screenSoundAdvanced: [SettingsItem] {
        let channels = AudioChannelSetting.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }
        return [
            SettingsItem(id: "audio.tvChannels", title: "TV channels", detail: "Applies to the next game.",
                         control: .choice(channels, values.intChoice(AudioSettings.tvChannelsKey, AudioSettings.defaultTvChannels) {
                             cemu_bridge_set_tv_channels(Int32($0))
                         })),
            SettingsItem(id: "audio.padChannels", title: "GamePad channels", detail: "Applies to the next game.",
                         control: .choice(channels, values.intChoice(AudioSettings.padChannelsKey, AudioSettings.defaultPadChannels) {
                             cemu_bridge_set_pad_channels(Int32($0))
                         })),
        ]
    }

    private func setMicrophone(_ on: Bool) {
        let key = AudioSettings.microphoneEnabledKey
        guard on else {
            UserDefaults.standard.set(false, forKey: key)
            cemu_bridge_set_microphone_enabled(false)
            return
        }
        UserDefaults.standard.set(true, forKey: key)
        MicrophoneAccess.request { granted in
            DispatchQueue.main.async {
                values.refresh()
                if granted {
                    cemu_bridge_set_microphone_enabled(true)
                    notes["audio.mic"] = nil
                } else {
                    UserDefaults.standard.set(false, forKey: key)
                    cemu_bridge_set_microphone_enabled(false)
                    notes["audio.mic"] = "Allow the microphone for EspressU in iOS Settings first."
                }
            }
        }
    }

    private var gamesDataGroups: [SettingsGroup] {
        [
            SettingsGroup(id: "library", title: "Library", items: [
                SettingsItem(id: "lib.sort", title: "Sort by", detail: "The order of the games on the home screen.",
                             control: .choice(LibrarySortOrder.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                                              values.string("espressu.library.sortOrder", LibrarySortOrder.title.rawValue))),
                SettingsItem(id: "lib.linked", title: "Linked folders", detail: "Game folders that stay where they are.",
                             control: .link("", .linkedFolders)),
            ]),
            SettingsGroup(id: "files", title: "Your files", items: [
                SettingsItem(id: "data.keys", title: "Wii U keys", detail: "Needed for encrypted dumps (.wud, .wux).",
                             control: .link("", .keys)),
                SettingsItem(id: "data.files", title: "Files and storage", detail: "Where EspressU keeps games, saves and more.",
                             control: .link("", .files)),
                SettingsItem(id: "data.backup", title: "Back up and restore", detail: "Save every setting to a file, or load one.",
                             control: .link("", .backup)),
            ]),
            SettingsGroup(id: "online", title: "Online", items: [
                SettingsItem(id: "data.account", title: "Account", detail: "Your Wii U account for saves and online play.",
                             control: .link("", .account)),
            ]),
        ]
    }

    private var systemGroups: [SettingsGroup] {
        var look: [SettingsItem] = [
            SettingsItem(id: "sys.appearance", title: "Appearance", detail: "Follows the device, or always light or dark.",
                         control: .choice([SettingsOption(id: "system", title: "Device"),
                                           SettingsOption(id: "light", title: "Light"),
                                           SettingsOption(id: "dark", title: "Dark")],
                                          values.string(EspressUAppearance.storageKey, "system") { _ in
                                              EspressUAppearance.applyToWindows()
                                          })),
        ]
        if #available(iOS 26.0, *) {
            look.append(SettingsItem(id: "sys.glass", title: "Glass effects", detail: "Liquid Glass on bars and panels.",
                                     control: .toggle(values.custom(get: { !uiStyle.disableLiquidGlass },
                                                                    set: { uiStyle.disableLiquidGlass = !$0 }))))
        }

        let help: [SettingsItem] = [
            SettingsItem(id: "sys.guide", title: "Show the welcome guide again",
                         detail: "Walks through games, JIT and controllers.",
                         control: .action("Open", destructive: false, confirm: false, run: {
                             OnboardingState.reset()
                             close()
                             NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
                         })),
            SettingsItem(id: "sys.report", title: "Device report and logs",
                         detail: "For bug reports: device, settings and the last log lines.",
                         control: .link("", .diagnostics)),
        ]

        let about: [SettingsItem] = [
            SettingsItem(id: "sys.version", title: "Version", detail: "EspressU builds on Cemu.",
                         control: .info("\(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0") (\(Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "0"))", nil)),
            SettingsItem(id: "sys.about", title: "About and licences", detail: "Credits and open-source licences.",
                         control: .link("", .about)),
            SettingsItem(id: "sys.reset", title: "Reset all settings",
                         detail: "Back to how EspressU came, Built-in JIT and its pairing file included. Games, saves and keys stay. The welcome guide opens again.",
                         control: .action("Reset", destructive: true, confirm: true, run: {
                             SettingsDefaults.reset(includingPerGameOverrides: false)
                             UserDefaults.standard.set(SettingsMode.advanced.rawValue, forKey: SettingsMode.storageKey)
                             values.refresh()
                             close()
                             NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
                         })),
        ]

        return [
            SettingsGroup(id: "look", title: "Look", items: look),
            SettingsGroup(id: "help", title: "Help", items: help),
            SettingsGroup(id: "about", title: "About", items: about),
        ]
    }
}
