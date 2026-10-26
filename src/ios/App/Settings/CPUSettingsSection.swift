import SwiftUI

struct CPUSettingsSection: View {
    @AppStorage("espressu.cpu.recompiler") private var recompilerEnabled = true
    @AppStorage("espressu.cpu.favourAccuracy") private var favourAccuracy = false
    @AppStorage(FavourPerformance.storageKey) private var favourPerformance = FavourPerformance.defaultValue
    @AppStorage(OneCoreMode.storageKey) private var oneCoreMode = OneCoreMode.defaultValue
    @AppStorage(CoreMode.storageKey) private var coreModeRaw = CoreMode.current.rawValue
    @AppStorage(ThermalMonitor.autoThrottleKey) private var autoReduceWhenHot = ThermalMonitor.autoThrottleDefault
    @AppStorage(ThermalSettings.thresholdKey) private var coolDownThresholdRaw = ThermalSettings.defaultThreshold.rawValue
    @ObservedObject private var thermal = ThermalMonitor.shared
    @AppStorage(HeatDisplayMode.storageKey) private var heatDisplayMode = HeatDisplayMode.word.rawValue
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    var body: some View {
        Section {
            CPUModeRow()

            Toggle(isOn: $recompilerEnabled) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Use the recompiler (JIT)")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("The fast way to run games. Needs a JIT enabler; without one the slow interpreter runs instead.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)
            .onValueChange(of: recompilerEnabled) { newValue in
                cemu_bridge_set_recompiler_enabled(newValue)
            }

            if advanced {
                Toggle(isOn: $favourAccuracy) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Favour accuracy")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text(favourAccuracy
                             ? "Slower but more accurate: one CPU core and stricter GPU syncing."
                             : "Faster, with some accuracy shortcuts.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
                .onValueChange(of: favourAccuracy) { newValue in
                    cemu_bridge_set_favour_accuracy(newValue)
                    if newValue { favourPerformance = false }
                }

                Toggle(isOn: $favourPerformance) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Favour performance")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text(favourPerformance
                             ? "As fast as possible: a softer picture, rougher lighting in some games, and shaders that may pop in."
                             : "Off: the normal balance of speed and quality.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
                .onValueChange(of: favourPerformance) { newValue in
                    cemu_bridge_set_favour_performance(newValue)
                    if newValue { favourAccuracy = false }
                }

                Toggle(isOn: $oneCoreMode) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("One-core mode")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Text(oneCoreMode
                             ? "One CPU core, whatever CPU cores is set to below."
                             : "Follows the CPU cores setting below.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                .tint(EspressUTheme.accentText)
                .onValueChange(of: oneCoreMode) { newValue in
                    cemu_bridge_set_low_power_mode(newValue)
                }

                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("CPU cores")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                        Spacer()
                        Picker("CPU cores", selection: $coreModeRaw) {
                            ForEach(CoreMode.allCases) { mode in
                                Text(mode.title).tag(mode.rawValue)
                            }
                        }
                        .pickerStyle(.menu)
                        .labelsHidden()
                        .tint(EspressUTheme.accentText)
                        .disabled(!DeviceCapabilities.current.multicoreViable)
                    }
                    Text(DeviceCapabilities.current.multicoreViable
                         ? (CoreMode(rawValue: coreModeRaw) ?? CoreMode.defaultValue).summary
                         : DeviceCapabilities.oneCoreOnlyText)
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .onValueChange(of: coreModeRaw) { newValue in
                    cemu_bridge_set_cpu_core_mode((CoreMode(rawValue: newValue) ?? CoreMode.defaultValue).bridgeValue)
                }
            }

            Toggle(isOn: $autoReduceWhenHot) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Cool down automatically")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text("When iOS reports the device is overheating, lowers resolution and CPU load until it cools.")
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                }
            }
            .tint(EspressUTheme.accentText)

            if advanced && autoReduceWhenHot {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Cool down starts at")
                        .font(.system(size: 13, weight: .semibold, design: .rounded))
                    Picker("Cool down starts at", selection: $coolDownThresholdRaw) {
                        ForEach(ThermalSettings.Threshold.allCases) { threshold in
                            Text(threshold.title).tag(threshold.rawValue)
                        }
                    }
                    .pickerStyle(.segmented)
                    .onValueChange(of: coolDownThresholdRaw) { _ in
                        thermal.thresholdChanged()
                    }
                    Text((ThermalSettings.Threshold(rawValue: coolDownThresholdRaw) ?? ThermalSettings.defaultThreshold).summary)
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Image(systemName: "memorychip")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(EspressUTheme.brownMid)
                    .frame(width: 20)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Memory")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                    Text(String(cString: cemu_bridge_memory_headroom_summary()))
                        .font(.system(size: 12))
                        .foregroundColor(EspressUTheme.secondaryText)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            HStack(spacing: 10) {
                Image(systemName: "thermometer.medium")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(EspressUTheme.brownMid)
                    .frame(width: 20)
                Text("Device heat")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                Spacer(minLength: 12)
                HeatStatusBadge()
            }
            .frame(minHeight: 30)

            if HeatStatus.hasRealTemperature {
                Picker("Show as", selection: $heatDisplayMode) {
                    ForEach(HeatDisplayMode.allCases) { mode in
                        Text(mode.title).tag(mode.rawValue)
                    }
                }
                .pickerStyle(.segmented)
            }
        } header: {
            SettingsSectionHeader("CPU", icon: "cpu", accent: .core)
        } footer: {
            InfoButton.footer(
                "Changes to the CPU switches apply the next time you start a game. Cool down automatically only acts when the device overheats; One-core mode is the always-on version.",
                title: "CPU",
                text: "The recompiler needs a JIT enabler (StikJIT, SideStore or LiveContainer). Without one the interpreter runs instead, which is much slower; the CPU line above shows which you got.\n\nFavour accuracy is slower but can fix a game that glitches, desyncs or crashes. It also builds every shader before it is needed, whatever Compile shaders in the background is set to.\n\nFavour performance is the opposite trade: everything runs as fast as EspressU can make it, and some quality goes. The picture is drawn at Balanced at most with linear scaling, so it's softer. Shaders skip the Wii U's exact multiply rule, which is faster but can make lighting or shadows look wrong in some games. Shaders always compile in the background, so things can pop in for a moment instead of the game pausing. Crash reports carry less detail. Favour accuracy and Favour performance turn each other off, and a game set to favour accuracy in its own options still does.\n\nCool down automatically acts when iOS reports the device is overheating, and Device heat shows that same state. In Advanced mode, Cool down starts at picks the point: Serious (the default) acts as soon as iOS starts throttling, Critical waits until iOS is throttling hard.\n\nThe recompiler, Favour accuracy, Favour performance, One-core mode and CPU cores apply the next time you start a game.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

private struct CPUModeRow: View {
    private let mode = cemu_bridge_cpu_mode()
    private let detail = String(cString: cemu_bridge_cpu_mode_detail())

    private var title: String {
        switch mode {
        case 2:  return "Recompiler (JIT)"
        case 1:  return "Interpreter"
        default: return "Not decided yet"
        }
    }

    private var tint: Color {
        switch mode {
        case 2:  return EspressUTheme.accentText
        case 1:  return EspressUTheme.cautionText
        default: return EspressUTheme.brownMid
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("CPU")
                Spacer()
                Text(title)
                    .foregroundColor(tint)
            }
            Text(detail)
                .font(.footnote)
                .foregroundColor(EspressUTheme.brownMid)
                .fixedSize(horizontal: false, vertical: true)

        }
    }
}
