import SwiftUI
import Combine
import UIKit

private struct NativeOverlayCard {
    let lines: [String]
    let color: Int
    let scale: Int
}

private struct NativeOverlaySnapshot: Equatable {
    var statsPosition: ScreenPosition = .disabled
    var statsColor: Int = OverlaySettings.defaultTextColor
    var statsScale: Int = OverlaySettings.defaultTextScale
    var notificationPosition: ScreenPosition = .disabled
    var notificationColor: Int = OverlaySettings.defaultTextColor
    var notificationScale: Int = OverlaySettings.defaultTextScale
    var stats: [String] = []
    var notifications: [[String]] = []

    static func read() -> NativeOverlaySnapshot {
        var snapshot = NativeOverlaySnapshot()
        snapshot.statsPosition = ScreenPosition(rawValue: Int(cemu_bridge_overlay_position())) ?? .disabled
        snapshot.statsColor = Int(cemu_bridge_overlay_text_color())
        snapshot.statsScale = Int(cemu_bridge_overlay_text_scale())
        snapshot.notificationPosition = ScreenPosition(rawValue: Int(cemu_bridge_notification_position())) ?? .disabled
        snapshot.notificationColor = Int(cemu_bridge_notification_text_color())
        snapshot.notificationScale = Int(cemu_bridge_notification_text_scale())
        guard snapshot.statsPosition != .disabled || snapshot.notificationPosition != .disabled else {
            return snapshot
        }
        snapshot.parse(String(cString: cemu_bridge_native_overlay_text()))
        return snapshot
    }

    private mutating func parse(_ raw: String) {
        let parts = raw.split(separator: "\u{1E}", omittingEmptySubsequences: false)
        guard let first = parts.first else { return }
        stats = Self.lines(of: first)
        notifications = parts.dropFirst().map { Self.lines(of: $0) }.filter { !$0.isEmpty }
    }

    private static func lines(of block: Substring) -> [String] {
        block.split(separator: "\n", omittingEmptySubsequences: true).map { String($0) }
    }
}

private extension ScreenPosition {
    var alignment: Alignment {
        switch self {
        case .disabled, .topLeft: return .topLeading
        case .topCenter: return .top
        case .topRight: return .topTrailing
        case .bottomLeft: return .bottomLeading
        case .bottomCenter: return .bottom
        case .bottomRight: return .bottomTrailing
        }
    }

    var stacksUpward: Bool {
        switch self {
        case .bottomLeft, .bottomCenter, .bottomRight: return true
        default: return false
        }
    }
}

private extension Color {
    init(packedARGB packed: Int) {
        let value = UInt32(truncatingIfNeeded: packed)
        self.init(
            .sRGB,
            red: Double((value >> 16) & 0xFF) / 255,
            green: Double((value >> 8) & 0xFF) / 255,
            blue: Double(value & 0xFF) / 255,
            opacity: Double((value >> 24) & 0xFF) / 255)
    }
}

private struct NativeOverlayCardView: View {
    let lines: [String]
    let color: Int
    let scale: Int

    private var font: Font {
        .system(size: 14 * CGFloat(scale) / 100).monospacedDigit()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(lines.indices, id: \.self) { index in
                Text(lines[index])
                    .font(font)
                    .foregroundColor(Color(packedARGB: color))
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(Color.black.opacity(0.65))
        .cornerRadius(6)
    }
}

private struct NativeOverlayStackView: View {
    let position: ScreenPosition
    let cards: [NativeOverlayCard]
    let safeArea: UIEdgeInsets
    let topInset: CGFloat

    private static let margin: CGFloat = 10

    private var edgeInsets: EdgeInsets {
        let margin = Self.margin
        return EdgeInsets(
            top: max(margin, topInset > 0 ? topInset + 16 : safeArea.top + margin),
            leading: max(margin, safeArea.left),
            bottom: max(margin, safeArea.bottom),
            trailing: max(margin, safeArea.right))
    }

    private var horizontalAlignment: HorizontalAlignment {
        switch position {
        case .topCenter, .bottomCenter: return .center
        case .topRight, .bottomRight: return .trailing
        default: return .leading
        }
    }

    private var ordered: [NativeOverlayCard] {
        position.stacksUpward ? Array(cards.reversed()) : cards
    }

    var body: some View {
        VStack(alignment: horizontalAlignment, spacing: 10) {
            ForEach(ordered.indices, id: \.self) { index in
                NativeOverlayCardView(lines: ordered[index].lines, color: ordered[index].color, scale: ordered[index].scale)
            }
        }
        .padding(edgeInsets)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: position.alignment)
    }
}

private struct NativeCoreOverlayLayer: View {
    let topInset: CGFloat

    @State private var snapshot = NativeOverlaySnapshot()

    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()

    private var statsCards: [NativeOverlayCard] {
        guard snapshot.statsPosition != .disabled, !snapshot.stats.isEmpty else { return [] }
        return [NativeOverlayCard(lines: snapshot.stats, color: snapshot.statsColor, scale: snapshot.statsScale)]
    }

    private var notificationCards: [NativeOverlayCard] {
        guard snapshot.notificationPosition != .disabled else { return [] }
        return snapshot.notifications.map {
            NativeOverlayCard(lines: $0, color: snapshot.notificationColor, scale: snapshot.notificationScale)
        }
    }

    @ViewBuilder
    private func stacks(safeArea: UIEdgeInsets) -> some View {
        ZStack {
            if snapshot.statsPosition == snapshot.notificationPosition {
                NativeOverlayStackView(position: snapshot.statsPosition, cards: statsCards + notificationCards,
                                       safeArea: safeArea, topInset: topInset)
            } else {
                NativeOverlayStackView(position: snapshot.statsPosition, cards: statsCards,
                                       safeArea: safeArea, topInset: topInset)
                NativeOverlayStackView(position: snapshot.notificationPosition, cards: notificationCards,
                                       safeArea: safeArea, topInset: topInset)
            }
        }
    }

    var body: some View {
        GeometryReader { _ in
            stacks(safeArea: WindowSafeArea.insets())
        }
        .accessibilityHidden(true)
        .onReceive(timer) { _ in
            let latest = NativeOverlaySnapshot.read()
            if latest != snapshot { snapshot = latest }
        }
    }
}

struct NativeCoreOverlayView: View {
    let active: Bool
    var topInset: CGFloat = 0

    var body: some View {
        ZStack { content }
            .allowsHitTesting(false)
            .onAppear { cemu_bridge_set_native_overlay(active) }
            .onValueChange(of: active) { newValue in
                cemu_bridge_set_native_overlay(newValue)
            }
            .onDisappear { cemu_bridge_set_native_overlay(false) }
    }

    @ViewBuilder
    private var content: some View {
        if active {
            NativeCoreOverlayLayer(topInset: topInset)
        } else {
            Color.clear.frame(width: 0, height: 0)
        }
    }
}

enum WindowSafeArea {
    static func insets() -> UIEdgeInsets {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap { $0.windows }
        return (windows.first(where: { $0.isKeyWindow }) ?? windows.first)?.safeAreaInsets ?? .zero
    }

    static func padding(minimum: CGFloat) -> EdgeInsets {
        let safe = insets()
        return EdgeInsets(top: minimum, leading: max(minimum, safe.left),
                          bottom: minimum, trailing: max(minimum, safe.right))
    }
}
