// SPDX-License-Identifier: MPL-2.0

import SwiftUI

@MainActor
struct JITOverview {
    struct Check: Identifiable {
        enum State { case ok, missing, unknown }
        let id: String
        let title: String
        let state: State
        let missingTitle: String
        let hint: String
    }

    let enabled: Bool
    let active: Bool
    let checks: [Check]
    let preparing: Bool
    let stageText: String

    var missing: Check? { checks.first { $0.state == .missing } }

    let vpnConnected: Bool

    var isReady: Bool { vpnConnected && (active || (enabled && missing == nil)) }

    static var current: JITOverview { current(enabled: JITSettings.enabled) }

    static func current(enabled: Bool) -> JITOverview {
        let coordinator = StikJITCoordinator.shared
        let device: Check.State
        let deviceHint: String
        switch coordinator.readiness {
        case .ready:
            device = .ok
            deviceHint = ""
        case .notChecked:
            device = .unknown
            deviceHint = ""
        case .unreachable(let reason), .notReady(let reason):
            device = .missing
            deviceHint = reason
        }
        let vpn = LocalDevVPNMonitor.shared.isConnected
        let checks = [
            Check(id: "ios", title: "iOS 17.4+", state: StikJITSetup.isAvailable ? .ok : .missing,
                  missingTitle: "Built-in JIT isn't available",
                  hint: StikJITSetup.unavailableReason ?? ""),
            Check(id: "gta", title: "get-task-allow",
                  state: StikJITHelperLauncher.hostHasGetTaskAllow() ? .ok : .missing,
                  missingTitle: "get-task-allow missing",
                  hint: "Reinstall EspressU with a signing method that keeps get-task-allow."),
            Check(id: "pairing", title: "Pairing file", state: StikJITSetup.hasPairingFile ? .ok : .missing,
                  missingTitle: "Pairing file missing",
                  hint: "Pair this device or import a pairing file."),
            Check(id: "vpn", title: "LocalDevVPN connected", state: vpn ? .ok : .missing,
                  missingTitle: "LocalDevVPN is off",
                  hint: "Turn on LocalDevVPN. JIT then switches on by itself."),
            Check(id: "device", title: "Device reachable (LocalDevVPN/Wi-Fi)", state: device,
                  missingTitle: "Device not reachable", hint: deviceHint),
        ]
        return JITOverview(enabled: enabled, active: JITStatus.isActive && StikJITSetup.hasPairingFile, checks: checks,
                           preparing: (coordinator.job == .enable && !coordinator.quiet) || coordinator.job == .prepare || coordinator.job == .resetDDI,
                           stageText: coordinator.stageText, vpnConnected: vpn)
    }

    var title: String {
        if !enabled { return "JIT is off" }
        if !vpnConnected && active { return "LocalDevVPN is off" }
        if active { return "JIT is on" }
        if preparing { return "Switching on…" }
        return missing?.missingTitle ?? "Ready"
    }

    var subtitle: String {
        if !enabled { return "Games run with the slow interpreter." }
        if !vpnConnected && active { return "JIT stays on until you close EspressU. Turn LocalDevVPN back on to switch it on again later." }
        if active { return "Games run with the fast recompiler." }
        if preparing { return stageText }
        if let missing { return missing.hint }
        if checks.contains(where: { $0.state == .unknown }) {
            return "JIT switches on by itself once LocalDevVPN is connected."
        }
        return "JIT switches on when a game starts."
    }

    var tint: Color {
        if !enabled { return EspressUTheme.brownMid }
        if isReady { return Color(hex: "#2E8B57") }
        if !vpnConnected && active { return EspressUTheme.brownMid }
        if preparing { return EspressUTheme.accentText }
        return EspressUTheme.cautionText
    }
}

struct JITStatusCard: View {
    @ObservedObject private var coordinator = StikJITCoordinator.shared
    @ObservedObject private var vpn = LocalDevVPNMonitor.shared
    @ObservedObject private var pairing = DevicePairing.shared
    var compact = false
    var enabled = JITSettings.enabled
    var pairingFile = StikJITSetup.pairingFileName

    var body: some View {
        let overview = JITOverview.current(enabled: enabled)
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 14) {
                Image(systemName: overview.isReady ? "bolt.fill" : (overview.enabled ? "exclamationmark.triangle.fill" : "bolt.slash.fill"))
                    .font(.system(size: 24, weight: .bold))
                    .foregroundColor(overview.tint)
                    .frame(width: 52, height: 52)
                    .background(overview.tint.opacity(0.15), in: RoundedRectangle(cornerRadius: 15, style: .continuous))
                VStack(alignment: .leading, spacing: 3) {
                    Text(overview.title)
                        .font(.system(size: 21, weight: .heavy, design: .rounded))
                        .foregroundColor(overview.tint)
                    if !overview.subtitle.isEmpty {
                        Text(overview.subtitle)
                            .font(.system(size: 14, weight: .medium, design: .rounded))
                            .foregroundColor(EspressUTheme.brownMid)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                Spacer(minLength: 0)
            }
            .accessibilityElement(children: .combine)
            if overview.enabled && !compact {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(overview.checks) { check in
                        HStack(spacing: 10) {
                            Image(systemName: symbol(check.state))
                                .font(.system(size: 17, weight: .semibold))
                                .foregroundColor(color(check.state))
                                .frame(width: 22)
                            Text(check.title)
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                                .foregroundColor(EspressUTheme.brownDarkest)
                            if check.state == .unknown {
                                Text("not checked yet")
                                    .font(.system(size: 13, design: .rounded))
                                    .foregroundColor(EspressUTheme.brownMid)
                            }
                        }
                        .accessibilityElement(children: .combine)
                    }
                }
                .padding(.leading, 4)
                Text("JIT switches on by itself while LocalDevVPN is connected, on Wi-Fi if you can.")
                    .font(.system(size: 13, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private func symbol(_ state: JITOverview.Check.State) -> String {
        switch state {
        case .ok: return "checkmark.circle.fill"
        case .missing: return "xmark.circle.fill"
        case .unknown: return "circle.dashed"
        }
    }

    private func color(_ state: JITOverview.Check.State) -> Color {
        switch state {
        case .ok: return Color(hex: "#2E8B57")
        case .missing: return EspressUTheme.cautionText
        case .unknown: return EspressUTheme.brownMid
        }
    }
}

struct JITPairingCard: View {
    @ObservedObject private var pairing = DevicePairing.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            switch pairing.state {
            case .pin(let pin):
                Text("Enter this PIN in Settings")
                    .font(.system(size: 14, weight: .semibold, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                Text(pin)
                    .font(.system(size: 40, weight: .bold, design: .monospaced))
                    .foregroundColor(EspressUTheme.brownDarkest)
                    .textSelection(.enabled)
            default:
                HStack(spacing: 10) {
                    ProgressView()
                    Text("Waiting for Settings…")
                        .font(.system(size: 16, weight: .bold, design: .rounded))
                        .foregroundColor(EspressUTheme.brownDarkest)
                }
                Text("Open Settings › Privacy & Security › Developer Mode and tap \"Pair with \(DevicePairing.hostName)\".")
                    .font(.system(size: 14, design: .rounded))
                    .foregroundColor(EspressUTheme.brownMid)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

struct JITModeSwitch: View {
    let isOn: Bool
    let set: (Bool) -> Void
    @Namespace private var knob

    var body: some View {
        HStack(spacing: 4) {
            segment(on: true, title: "JIT", detail: "Fast recompiler", symbol: "bolt.fill")
            segment(on: false, title: "No JIT", detail: "Interpreter", symbol: "tortoise.fill")
        }
        .padding(4)
        .background(EspressUTheme.wrapper, in: Capsule())
        .animation(.spring(response: 0.3, dampingFraction: 0.8), value: isOn)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("JIT")
    }

    private func segment(on: Bool, title: String, detail: String, symbol: String) -> some View {
        let selected = isOn == on
        return Button { if !selected { set(on) } } label: {
            HStack(spacing: 10) {
                Image(systemName: symbol)
                    .font(.system(size: 18, weight: .bold))
                VStack(alignment: .leading, spacing: 1) {
                    Text(title)
                        .font(.system(size: 17, weight: .heavy, design: .rounded))
                    Text(detail)
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .opacity(0.85)
                }
            }
            .foregroundColor(selected ? .white : EspressUTheme.brownMid)
            .frame(maxWidth: .infinity, minHeight: 56)
            .background {
                if selected {
                    Capsule()
                        .fill(on ? Color(hex: "#2E8B57") : EspressUTheme.brownMid)
                        .shadow(color: Color.black.opacity(0.2), radius: 3, y: 1)
                        .matchedGeometryEffect(id: "knob", in: knob)
                }
            }
            .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}
