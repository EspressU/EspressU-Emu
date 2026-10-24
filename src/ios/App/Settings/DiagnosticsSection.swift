import SwiftUI
import UIKit

struct DeviceReportSection: View {
    @State private var deviceReportCopied = false
    @MainActor
    private var deviceReport: String {
        String(cString: cemu_bridge_device_report())
            + "\n" + PlatformCapabilities.summary
            + "\nthermal: " + ThermalMonitor.shared.description
            + " · heat " + HeatStatus.band.word
            + (HeatStatus.temperatureCelsius.map { String(format: " (battery %.0f C)", $0) } ?? " (no sensor reading)")
    }

    var body: some View {
        Section {
            Text(deviceReport)
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)

            Button {
                UIPasteboard.general.string = deviceReport
                deviceReportCopied = true
            } label: {
                Label(deviceReportCopied ? "Copied" : "Copy device report",
                      systemImage: deviceReportCopied ? "checkmark" : "doc.on.doc")
            }

            Button {
                SessionReport.share()
            } label: {
                Label("Share EspressU report", systemImage: "square.and.arrow.up")
            }

            Button(role: .destructive) {
                cemu_bridge_report_clear()
            } label: {
                Label("Delete EspressU report", systemImage: "trash")
            }
        } header: {
            SettingsSectionHeader("This Device", icon: "iphone", accent: .system)
        } footer: {
            InfoButton.footer("For any bug, crash or speed comparison, share the EspressU report: device, settings, the whole log, crashes and a performance summary per game. Every session goes into the same file until you delete it here.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct GpuFaultHistorySection: View {
    @State private var history = String(cString: cemu_bridge_gpu_fault_history())

    var body: some View {
        Section {
            Text(history.isEmpty ? "Nothing recorded." : history)
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(EspressUTheme.secondaryText)
                .fixedSize(horizontal: false, vertical: true)

            if !history.isEmpty {
                Button {
                    UIPasteboard.general.string = history
                } label: {
                    Label("Copy history", systemImage: "doc.on.doc")
                }
                Button(role: .destructive) {
                    cemu_bridge_clear_gpu_fault_history()
                    history = ""
                } label: {
                    Label("Clear history", systemImage: "trash")
                }
            }
        } header: {
            SettingsSectionHeader("GPU Memory Failures", icon: "memorychip", accent: .system)
        } footer: {
            InfoButton.footer("The last graphics allocations that failed and GPU faults, newest first. Turn it on in Performance > Experimental.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct DiagnosticsSection: View {
    @AppStorage(LaunchLogSettings.showKey) private var showLaunchLog = false
    @AppStorage("espressu.showLaunchIntro") private var launchIntroEnabled = true
    @AppStorage(PadDiagnostics.enabledKey) private var padOverlayEnabled = PadDiagnostics.defaultEnabled
    @AppStorage(SettingsMode.storageKey) private var settingsModeRaw = SettingsMode.defaultValue.rawValue

    private var advanced: Bool { SettingsMode.isAdvanced(raw: settingsModeRaw) }

    var body: some View {
        Section {
            Toggle(isOn: $launchIntroEnabled) {
                Label {
                    Text("Play the launch intro")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                } icon: {
                    Image(systemName: "sparkles")
                }
            }
            .tint(EspressUTheme.accentText)

            if advanced {
                Toggle(isOn: $showLaunchLog) {
                    Label {
                        Text("Show launch log")
                            .font(.system(size: 15, weight: .semibold, design: .rounded))
                    } icon: {
                        Image(systemName: "text.alignleft")
                    }
                }
                .tint(EspressUTheme.accentText)

                Toggle(isOn: $padOverlayEnabled) {
                    Label {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Show controls diagnostic")
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            Text("A small readout over the game showing whether your button presses are reaching the game, and why not if they aren't.")
                                .font(.system(size: 12))
                                .foregroundColor(EspressUTheme.secondaryText)
                        }
                    } icon: {
                        Image(systemName: "gamecontroller.badge.exclamationmark")
                    }
                }
                .tint(EspressUTheme.accentText)
            }
        } header: {
            SettingsSectionHeader("Diagnostics", icon: "stethoscope", accent: .system)
        } footer: {
            InfoButton.footer(
                "The launch log shows boot progress and replaces the intro while it's on.",
                title: "Diagnostics",
                text: "The intro plays during the boot. If you turn on the launch log it takes the screen instead, so you can see timestamped boot steps when a game starts but stays black.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}
