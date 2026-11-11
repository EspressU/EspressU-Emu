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
            advancedHint = loc("Renderer, scaling filters, gamma.")
        case .controls:
            groups = controlsGroups
        case .screenSound:
            groups = screenSoundGroups
            advanced = screenSoundAdvanced
            advancedHint = loc("Audio channels.")
        case .gamesData:
            groups = gamesDataGroups
        case .system:
            groups = systemGroups
        }
        if !advanced.isEmpty {
            let open = expanded.contains(category)
            groups.append(SettingsGroup(id: "advanced.\(category.rawValue)", title: "", items: [
                SettingsItem(id: "advanced.toggle.\(category.rawValue)", title: loc("Advanced"), detail: advancedHint,
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
            SettingsItem(id: "cpu.cores", title: loc("CPU cores"),
                         detail: multicore ? coreMode.summary : DeviceCapabilities.oneCoreOnlyText,
                         control: .choice([SettingsOption(id: CoreMode.auto.rawValue, title: loc("Auto")),
                                           SettingsOption(id: CoreMode.single.rawValue, title: loc("One")),
                                           SettingsOption(id: CoreMode.multi.rawValue, title: loc("Three"))],
                                          values.custom(get: { coreModeRaw }, set: { raw in
                                              UserDefaults.standard.set(raw, forKey: CoreMode.storageKey)
                                              cemu_bridge_set_cpu_core_mode((CoreMode(rawValue: raw) ?? CoreMode.defaultValue).bridgeValue)
                                          })),
                         enabled: multicore),
            SettingsItem(id: "cpu.mode", title: loc("Running on"), detail: locBridge(String(cString: cemu_bridge_cpu_mode_detail())),
                         control: .info(Self.cpuModeTitle, nil)),
            SettingsItem(id: "cpu.memory", title: loc("Memory"), detail: locBridge(String(cString: cemu_bridge_memory_headroom_summary())),
                         control: .info("", nil)),
        ]

        let heat: [SettingsItem] = [
            SettingsItem(id: "heat.auto", title: loc("Cool down automatically"),
                         detail: loc("Lowers resolution and CPU load when the device gets hot."),
                         control: .toggle(values.bool(ThermalMonitor.autoThrottleKey, ThermalMonitor.autoThrottleDefault))),
            SettingsItem(id: "heat.status", title: loc("Device heat"), detail: loc("What iOS reports right now."),
                         control: .custom(AnyView(HeatStatusBadge()))),
        ]

        let speed: [SettingsItem] = [
            SettingsItem(id: "cpu.accuracy", title: loc("Favour accuracy"),
                         detail: loc("Slower but more accurate: one CPU core and stricter GPU syncing. Can fix a game that glitches or crashes."),
                         control: .toggle(values.bool("espressu.cpu.favourAccuracy", false) { on in
                             cemu_bridge_set_favour_accuracy(on)
                             if on {
                                 UserDefaults.standard.set(false, forKey: FavourPerformance.storageKey)
                                 cemu_bridge_set_favour_performance(false)
                             }
                         })),
            SettingsItem(id: "cpu.performance", title: loc("Favour performance"),
                         detail: loc("As fast as possible: a softer picture, rougher lighting in some games, and shaders that may pop in."),
                         control: .toggle(values.bool(FavourPerformance.storageKey, FavourPerformance.defaultValue) { on in
                             cemu_bridge_set_favour_performance(on)
                             if on {
                                 UserDefaults.standard.set(false, forKey: "espressu.cpu.favourAccuracy")
                                 cemu_bridge_set_favour_accuracy(false)
                             }
                         })),
            SettingsItem(id: "cpu.onecore", title: loc("One-core mode"),
                         detail: loc("One CPU core, whatever CPU cores is set to."),
                         control: .toggle(values.bool(OneCoreMode.storageKey, OneCoreMode.defaultValue) { cemu_bridge_set_low_power_mode($0) })),
        ]

        let frameGeneration = values.bool(PerformanceTuning.frameGenerationKey, PerformanceTuning.frameGenerationDefault) { _ in
            PerformanceTuning.applyToBridge()
        }
        var experimental: [SettingsItem] = [
            SettingsItem(id: "tuning.asyncTextures", title: loc("Async texture loading"),
                         detail: loc("Converts textures in the background instead of pausing the game. New textures can show a plain colour for a moment."),
                         control: .toggle(values.bool(PerformanceTuning.asyncTexturesKey, PerformanceTuning.asyncTexturesDefault) { _ in
                             PerformanceTuning.applyToBridge()
                         })),
            SettingsItem(id: "tuning.frameGeneration", title: loc("Frame generation"),
                         detail: loc("Adds intermediate pictures to the TV output when there is room between real frames. Interpolation can add latency."),
                         control: .toggle(frameGeneration)),
        ]
        if frameGeneration.wrappedValue {
            experimental.append(SettingsItem(id: "tuning.showFrameGenBreakdown", title: loc("Show frame generation breakdown"),
                                             detail: loc("Shows how many of the displayed frames per second were generated."),
                                             control: .toggle(values.bool(OverlaySettings.showFrameGenBreakdownKey, OverlaySettings.defaultShowFrameGenBreakdown) {
                                                 cemu_bridge_set_overlay_show_framegen_breakdown($0)
                                             })))
        }

        return [
            SettingsGroup(id: "cpu", title: loc("CPU"), items: cpu),
            SettingsGroup(id: "speed", title: loc("Speed and accuracy"), items: speed),
            SettingsGroup(id: "heat", title: loc("Heat"), items: heat),
            SettingsGroup(id: "experimental", title: loc("Experimental"), items: experimental),
        ]
    }

    private static var cpuModeTitle: String {
        switch cemu_bridge_cpu_mode() {
        case 2: return loc("Recompiler")
        case 1: return loc("Interpreter")
        default: return loc("Not decided yet")
        }
    }

    private var graphicsGroups: [SettingsGroup] {
        let defaults = UserDefaults.standard
        let favourPerformance = defaults.object(forKey: FavourPerformance.storageKey) as? Bool ?? FavourPerformance.defaultValue
        let scale = RenderScale(rawValue: defaults.string(forKey: RenderScale.storageKey) ?? "") ?? RenderScale.deviceDefault
        let steady = FullSpeedRenders.isEnabled

        var picture: [SettingsItem] = [
            SettingsItem(id: "gfx.resolution", title: loc("Resolution"),
                         detail: favourPerformance ? loc("Favour performance is on, so at most Balanced.") : scale.summary,
                         control: .choice(RenderScale.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                                          values.string(RenderScale.storageKey, RenderScale.deviceDefault.rawValue))),
            SettingsItem(id: "gfx.stretch", title: loc("Stretch to fill"),
                         detail: loc("Fills the screen instead of keeping 16:9."),
                         control: .toggle(values.bool(FrameStretch.storageKey, FrameStretch.defaultValue) { cemu_bridge_set_stretch_to_fill($0) })),
            SettingsItem(id: "gfx.steady", title: loc("Steady frame rate"),
                         detail: loc("Even frames at the game's own speed, never faster."),
                         control: .toggle(values.bool(FullSpeedRenders.storageKey, FullSpeedRenders.defaultValue) { _ in
                             FullSpeedRenders.applyToBridge()
                         })),
        ]
        if steady {
            picture.append(SettingsItem(id: "gfx.steadyShaders", title: loc("When a new effect appears"),
                                        detail: FullSpeedRenders.shaderMode.summary,
                                        control: .choice([SettingsOption(id: String(FullSpeedRenders.ShaderMode.waitForIt.rawValue), title: loc("Wait")),
                                                          SettingsOption(id: String(FullSpeedRenders.ShaderMode.keepGoing.rawValue), title: loc("Keep going"))],
                                                         values.intChoice(FullSpeedRenders.shaderModeKey, FullSpeedRenders.defaultShaderMode.rawValue) { _ in
                                                             FullSpeedRenders.applyToBridge()
                                                         })))
        }

        let shaders: [SettingsItem] = [
            SettingsItem(id: "shader.async", title: loc("Compile shaders in the background"),
                         detail: loc("The game keeps running; new effects can flicker the first time."),
                         control: .toggle(values.bool("espressu.shaders.asyncCompile", true) { cemu_bridge_set_async_shader_compile($0) })),
            SettingsItem(id: "shader.size", title: loc("Saved shaders"), detail: loc("What every game has saved so it starts faster."),
                         control: .info(ShaderCacheInfo.formatBytes(shaderLearned + shaderCompiled), nil)),
            SettingsItem(id: "shader.clearCompiled", title: loc("Clear compiled shaders"),
                         detail: loc("Frees space. Each game is slow to start once."),
                         control: .action(loc("Clear"), destructive: false, confirm: true, run: { clearShaders(includeLearned: false) }),
                         enabled: shaderCompiled > 0),
            SettingsItem(id: "shader.clearAll", title: loc("Clear everything"),
                         detail: loc("Also the learned shaders. Games stutter while they relearn them."),
                         control: .action(loc("Clear"), destructive: true, confirm: true, run: { clearShaders(includeLearned: true) }),
                         enabled: shaderLearned + shaderCompiled > 0),
            SettingsItem(id: "gfx.packs", title: loc("Graphic packs"), detail: loc("Fixes and extras for each game."),
                         control: .link("", .graphicPacks)),
        ]

        return [
            SettingsGroup(id: "picture", title: loc("Picture"), items: picture),
            SettingsGroup(id: "shaders", title: loc("Shaders"), items: shaders),
        ]
    }

    private var graphicsAdvanced: [SettingsItem] {
        let defaults = UserDefaults.standard
        let overrideGamma = defaults.object(forKey: "espressu.render.overrideAppGamma") as? Bool ?? false
        let filters = ScaleFilter.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }
        let gammaLabel: (Double) -> String = { String(format: "%.1f", $0) }

        var items: [SettingsItem] = [
            SettingsItem(id: "gfx.upscale", title: loc("Filter when enlarging"), detail: loc("Used when the picture is drawn larger."),
                         control: .choice(filters, values.intChoice(UpscaleFilterSetting.storageKey, UpscaleFilterSetting.defaultValue.rawValue))),
            SettingsItem(id: "gfx.downscale", title: loc("Filter when shrinking"), detail: loc("Used when the picture is drawn smaller."),
                         control: .choice(filters, values.intChoice(DownscaleFilterSetting.storageKey, DownscaleFilterSetting.defaultValue.rawValue))),
            SettingsItem(id: "gfx.flip", title: loc("Flip screen upside down"), detail: loc("Only for screens that show the picture inverted."),
                         control: .toggle(values.bool("espressu.render.upsideDown", false) { cemu_bridge_set_render_upside_down($0) })),
        ]
        items.append(SettingsItem(id: "gfx.fetch", title: loc("Fast blending"), detail: loc("A quicker way to blend on Metal. Applies to the next game."),
                                  control: .toggle(values.bool("espressu.render.framebufferFetch", true) { cemu_bridge_set_framebuffer_fetch($0) })))
        items += [
            SettingsItem(id: "gfx.gamma", title: loc("Display gamma"), detail: loc("2.2 is standard. Lower is brighter in the mid-tones."),
                         control: .slider(values.double(DisplayGammaSetting.storageKey, DisplayGammaSetting.defaultValue) { cemu_bridge_set_display_gamma(Float($0)) },
                                          DisplayGammaSetting.minValue...DisplayGammaSetting.maxValue, step: 0.1, label: gammaLabel)),
            SettingsItem(id: "gfx.overrideGamma", title: loc("Override the game's gamma"), detail: loc("Replaces the gamma a game asks for."),
                         control: .toggle(values.bool("espressu.render.overrideAppGamma", false) { cemu_bridge_set_override_app_gamma($0) })),
        ]
        if overrideGamma {
            items.append(SettingsItem(id: "gfx.overrideGammaValue", title: loc("Override gamma"), detail: nil,
                                      control: .slider(values.double(OverrideGammaSetting.storageKey, OverrideGammaSetting.defaultValue) { cemu_bridge_set_override_gamma_value(Float($0)) },
                                                       OverrideGammaSetting.minValue...OverrideGammaSetting.maxValue, step: 0.1, label: gammaLabel)))
        }
        return items
    }

    private func clearShaders(includeLearned: Bool) {
        let freed = cemu_bridge_clear_shader_cache(0, includeLearned)
        let id = includeLearned ? "shader.clearAll" : "shader.clearCompiled"
        notes[id] = freed < 0 ? loc("Close the game first, then clear.") : locf("Freed %@.", ShaderCacheInfo.formatBytes(freed))
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
        let defaults = UserDefaults.standard
        let motionOn = defaults.object(forKey: MotionSettings.enabledKey) as? Bool ?? MotionSettings.defaultEnabled
        let motionSource = defaults.object(forKey: MotionSettings.sourceKey) as? Int ?? MotionSettings.defaultSource
        let motionChoice = !motionOn ? "off"
            : (motionSource == MotionSettings.sourceController ? "controller" : (motionSource == MotionSettings.sourceDevice ? "device" : "auto"))
        var motion: [SettingsItem] = [
            SettingsItem(id: "motion.source", title: loc("Motion from"), detail: loc("What moves the GamePad's gyro. Automatic uses a connected controller, otherwise this device."),
                         control: .choice([SettingsOption(id: "auto", title: loc("Automatic")),
                                           SettingsOption(id: "device", title: loc("This device")),
                                           SettingsOption(id: "controller", title: loc("Controller")),
                                           SettingsOption(id: "off", title: loc("Off"))],
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
                SettingsItem(id: "motion.sensitivity", title: loc("Sensitivity"), detail: loc("How far the aim moves when you tilt."),
                             control: .slider(values.double(MotionSettings.sensitivityKey, MotionSettings.defaultSensitivity) { _ in
                                 MotionSettings.applyToBridge()
                             }, MotionSettings.minSensitivity...MotionSettings.maxSensitivity, step: 0.25,
                                              label: { String(format: "%.2f×", $0) })),
            ]
        }

        let names = input.controllers.map { $0.vendorName ?? loc("Controller") }
        let controllers: [SettingsItem] = [
            SettingsItem(id: "pad.connected", title: loc("Connected controllers"),
                         detail: names.isEmpty ? loc("Pair one in the iOS Bluetooth settings.")
                             : names.enumerated().map { locf("Player %@: %@", $0.offset + 1, $0.element) }.joined(separator: "\n"),
                         control: .info(names.isEmpty ? loc("None") : "\(names.count)",
                                        names.isEmpty ? nil : EspressUTheme.accentText)),
            SettingsItem(id: "pad.players", title: loc("More controllers are more players"),
                         detail: loc("The first controller is Player 1 on the GamePad. The next ones play as Player 2 to 4. Applies the next time you start a game."),
                         control: .toggle(values.bool("espressu.input.extraPads", true))),
            SettingsItem(id: "pad.playerType", title: loc("Players 2 to 4 use"),
                         detail: loc("Most games take a Pro Controller; some older ones want the Classic Controller."),
                         control: .choice([SettingsOption(id: "0", title: loc("Pro Controller")),
                                           SettingsOption(id: "1", title: loc("Classic Controller"))],
                                          values.intChoice("espressu.input.extraPadType", 0)),
                         enabled: UserDefaults.standard.object(forKey: "espressu.input.extraPads") as? Bool ?? true),
            SettingsItem(id: "pad.mapping", title: loc("Controller buttons"),
                         detail: loc("Choose which button on your controller does what."),
                         control: .link("", .controllerMapping)),
            SettingsItem(id: "pad.figures", title: loc("Figure portals"),
                         detail: loc("Skylanders, Disney Infinity and LEGO Dimensions."),
                         control: .link("", .figures)),
        ]

        return [
            SettingsGroup(id: "motion", title: loc("Motion"), items: motion),
            SettingsGroup(id: "controllers", title: loc("Controllers"), items: controllers),
        ]
    }

    private var screenSoundGroups: [SettingsGroup] {
        let defaults = UserDefaults.standard
        let layout = ScreenLayout(rawValue: defaults.string(forKey: LocalScreenLayoutSettings.layoutKey) ?? "") ?? .singleScreen

        var screen: [SettingsItem] = [
            SettingsItem(id: "screen.layout", title: loc("Screen layout"), detail: layout.description,
                         control: .choice([SettingsOption(id: ScreenLayout.singleScreen.rawValue, title: loc("One screen")),
                                           SettingsOption(id: ScreenLayout.bothScreens.rawValue, title: loc("Both screens")),
                                           SettingsOption(id: ScreenLayout.smallGamePadTopRight.rawValue, title: loc("TV with small GamePad"))],
                                          values.string(LocalScreenLayoutSettings.layoutKey, ScreenLayout.singleScreen.rawValue))),
        ]
        if layout == .singleScreen {
            screen.append(SettingsItem(id: "screen.swap", title: loc("Show swap button"),
                                       detail: loc("A small button to switch between TV and GamePad."),
                                       control: .toggle(values.bool(LocalScreenLayoutSettings.showSwapButtonKey,
                                                                    LocalScreenLayoutSettings.defaultShowSwapButton))))
        }
        screen += [
            SettingsItem(id: "screen.boost", title: loc("Brightness boost"),
                         detail: loc("Lightens the picture beyond the screen's maximum."),
                         control: .slider(values.double(BrightnessBoost.key, BrightnessBoost.defaultValue),
                                          0...BrightnessBoost.maxValue, step: 0.1, label: { BrightnessBoost.label(for: $0) })),
            SettingsItem(id: "screen.maxBrightness", title: loc("Max brightness while playing"),
                         detail: loc("Full brightness during a game, back afterwards."),
                         control: .toggle(values.bool(ScreenBrightness.maxWhilePlayingKey, ScreenBrightness.defaultMaxWhilePlaying))),
            SettingsItem(id: "screen.padLowRes", title: loc("Lower-resolution GamePad screen"),
                         detail: loc("Draws the GamePad at its own 854 pixels instead of the display's sharpness. Saves GPU time; applies the next time the GamePad screen is laid out."),
                         control: .toggle(values.bool(PerformanceTuning.padLowResolutionKey, PerformanceTuning.padLowResolutionDefault) { _ in
                             PerformanceTuning.applyToBridge()
                         })),
        ]
        screen.append(SettingsItem(id: "screen.overlay", title: loc("Overlay and notifications"),
                                   detail: loc("Frame rate counter and messages over the game."),
                                   control: .link("", .overlay)))

        let tvOn = defaults.object(forKey: AudioSettings.tvEnabledKey) as? Bool ?? AudioSettings.defaultTvEnabled
        let padOn = defaults.object(forKey: AudioSettings.padEnabledKey) as? Bool ?? AudioSettings.defaultPadEnabled
        let micOn = defaults.object(forKey: AudioSettings.microphoneEnabledKey) as? Bool ?? AudioSettings.defaultMicrophoneEnabled
        let volume: (Double) -> String = { "\(Int($0.rounded()))%" }
        var sound: [SettingsItem] = [
            SettingsItem(id: "audio.tv", title: loc("TV sound"), detail: loc("The main game sound."),
                         control: .toggle(values.bool(AudioSettings.tvEnabledKey, AudioSettings.defaultTvEnabled) { cemu_bridge_set_tv_audio_enabled($0) })),
        ]
        if tvOn {
            sound.append(SettingsItem(id: "audio.tvVolume", title: loc("TV volume"), detail: nil,
                                      control: .slider(values.intSlider(AudioSettings.tvVolumeKey, AudioSettings.defaultTvVolume) { cemu_bridge_set_tv_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound.append(SettingsItem(id: "audio.pad", title: loc("GamePad sound"), detail: loc("What the game sends to the GamePad speaker."),
                                  control: .toggle(values.bool(AudioSettings.padEnabledKey, AudioSettings.defaultPadEnabled) { cemu_bridge_set_pad_audio_enabled($0) })))
        if padOn {
            sound.append(SettingsItem(id: "audio.padVolume", title: loc("GamePad volume"), detail: nil,
                                      control: .slider(values.intSlider(AudioSettings.padVolumeKey, AudioSettings.defaultPadVolume) { cemu_bridge_set_pad_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound.append(SettingsItem(id: "audio.mic", title: loc("Use the microphone"), detail: loc("Only for games that listen to the GamePad mic."),
                                  control: .toggle(values.custom(get: { micOn }, set: { on in setMicrophone(on) }))))
        if micOn {
            sound.append(SettingsItem(id: "audio.micVolume", title: loc("Microphone volume"), detail: loc("50% is normal."),
                                      control: .slider(values.intSlider(AudioSettings.inputVolumeKey, AudioSettings.defaultInputVolume) { cemu_bridge_set_input_volume(Int32($0)) },
                                                       0...100, step: 5, label: volume)))
        }
        sound += [
            SettingsItem(id: "audio.silent", title: loc("Respect silent mode"), detail: loc("No game sound while the device is on silent."),
                         control: .toggle(values.bool(MenuMusic.respectSilentModeKey, false) { _ in MenuMusic.shared.restart() })),
        ]

        return [
            SettingsGroup(id: "screen", title: loc("Screen"), items: screen),
            SettingsGroup(id: "sound", title: loc("Sound"), items: sound),
        ]
    }

    private var screenSoundAdvanced: [SettingsItem] {
        let channels = AudioChannelSetting.allCases.map { SettingsOption(id: String($0.rawValue), title: $0.title) }
        return [
            SettingsItem(id: "audio.tvChannels", title: loc("TV channels"), detail: loc("Applies to the next game."),
                         control: .choice(channels, values.intChoice(AudioSettings.tvChannelsKey, AudioSettings.defaultTvChannels) {
                             cemu_bridge_set_tv_channels(Int32($0))
                         })),
            SettingsItem(id: "audio.padChannels", title: loc("GamePad channels"), detail: loc("Applies to the next game."),
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
                    notes["audio.mic"] = loc("Allow the microphone for EspressU in iOS Settings first.")
                }
            }
        }
    }

    private var gamesDataGroups: [SettingsGroup] {
        [
            SettingsGroup(id: "library", title: loc("Library"), items: [
                SettingsItem(id: "lib.sort", title: loc("Sort by"), detail: loc("The order of the games on the home screen."),
                             control: .choice(LibrarySortOrder.allCases.map { SettingsOption(id: $0.rawValue, title: $0.title) },
                                              values.string("espressu.library.sortOrder", LibrarySortOrder.title.rawValue))),
                SettingsItem(id: "lib.linked", title: loc("Linked folders"), detail: loc("Game folders that stay where they are."),
                             control: .link("", .linkedFolders)),
            ]),
            SettingsGroup(id: "files", title: loc("Your files"), items: [
                SettingsItem(id: "data.keys", title: loc("Wii U keys"), detail: loc("Needed for encrypted dumps (.wud, .wux)."),
                             control: .link("", .keys)),
                SettingsItem(id: "data.files", title: loc("Files and storage"), detail: loc("Where EspressU keeps games, saves and more."),
                             control: .link("", .files)),
                SettingsItem(id: "data.backup", title: loc("Back up and restore"), detail: loc("Save every setting to a file, or load one."),
                             control: .link("", .backup)),
            ]),
            SettingsGroup(id: "online", title: loc("Online"), items: [
                SettingsItem(id: "data.account", title: loc("Account"), detail: loc("Your Wii U account for saves and online play."),
                             control: .link("", .account)),
            ]),
        ]
    }

    private var systemGroups: [SettingsGroup] {
        let look: [SettingsItem] = [
            SettingsItem(id: "sys.appearance", title: loc("Appearance"), detail: loc("Follows the device, or always light or dark."),
                         control: .choice([SettingsOption(id: "system", title: loc("Device")),
                                           SettingsOption(id: "light", title: loc("Light")),
                                           SettingsOption(id: "dark", title: loc("Dark"))],
                                          values.string(EspressUAppearance.storageKey, "system") { _ in
                                              EspressUAppearance.applyToWindows()
                                          })),
        ]

        let language: [SettingsItem] = [
            SettingsItem(id: "sys.gameLanguage", title: loc("Game language"),
                         detail: loc("The Wii U system language games see. Automatic follows your device language. Games that don't have it fall back to their own default. Applies the next time you start a game."),
                         control: .choice(GameLanguage.allCases.map { SettingsOption(id: $0.rawValue, title: $0.nativeName, verbatim: $0 != .automatic) },
                                          values.string(GameLanguage.storageKey, GameLanguage.defaultValue.rawValue) { _ in
                                              GameLanguage.applyStoredToBridge()
                                          })),
            SettingsItem(id: "sys.appLanguage", title: loc("App language"),
                         detail: loc("Menus follow the language set for EspressU in the iOS Settings app."),
                         control: .action(loc("Open"), destructive: false, confirm: false, run: {
                             if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
                         })),
        ]

        let help: [SettingsItem] = [
            SettingsItem(id: "sys.guide", title: loc("Show the welcome guide again"),
                         detail: loc("Walks through games, JIT and controllers."),
                         control: .action(loc("Open"), destructive: false, confirm: false, run: {
                             OnboardingState.reset()
                             close()
                             NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
                         })),
            SettingsItem(id: "sys.report", title: loc("Device report and logs"),
                         detail: loc("For bug reports: device, settings and the last log lines."),
                         control: .link("", .diagnostics)),
        ]

        let about: [SettingsItem] = [
            SettingsItem(id: "sys.version", title: loc("Version"), detail: loc("EspressU builds on Cemu."),
                         control: .info("\(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0") (\(Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "0"))", nil)),
            SettingsItem(id: "sys.about", title: loc("About and licences"), detail: loc("Credits and open-source licences."),
                         control: .link("", .about)),
            SettingsItem(id: "sys.reset", title: loc("Reset all settings"),
                         detail: loc("Back to how EspressU came, Built-in JIT and its pairing file included. Games, saves and keys stay. The welcome guide opens again."),
                         control: .action(loc("Reset"), destructive: true, confirm: true, run: {
                             SettingsDefaults.reset(includingPerGameOverrides: false)
                             UserDefaults.standard.set(SettingsMode.advanced.rawValue, forKey: SettingsMode.storageKey)
                             values.refresh()
                             close()
                             NotificationCenter.default.post(name: .espressuReopenOnboarding, object: nil)
                         })),
        ]

        return [
            SettingsGroup(id: "look", title: loc("Look"), items: look),
            SettingsGroup(id: "language", title: loc("Language"), items: language),
            SettingsGroup(id: "help", title: loc("Help"), items: help),
            SettingsGroup(id: "about", title: loc("About"), items: about),
            SettingsGroup(id: "community", title: loc("Community"), items: [
                SettingsItem(id: "sys.discord", title: loc("Join the Discord"),
                             detail: loc("Opens the EspressU community server in your browser."),
                             control: .action(loc("Open"), destructive: false, confirm: false, run: {
                                 if let url = URL(string: "https://discord.gg/espressu") { UIApplication.shared.open(url) }
                             })),
            ]),
        ]
    }
}
