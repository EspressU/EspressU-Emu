import SwiftUI
import Combine

@MainActor
final class LaunchLogStore: ObservableObject {
    @Published private(set) var lines: [String] = []
    @Published private(set) var droppedLines: UInt64 = 0

    private static let maxRenderedLines = 600

    private var cursor: UInt64 = 0
    private var timer: Timer?

    func start() {
        guard timer == nil else { return }
        drain()
        let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.drain() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    private func drain() {
        var dropped: UInt64 = 0
        let chunk = withUnsafeMutablePointer(to: &cursor) { cursorPtr in
            withUnsafeMutablePointer(to: &dropped) { droppedPtr in
                String(cString: ios_live_log_drain(cursorPtr, droppedPtr))
            }
        }
        if dropped > 0 {
            droppedLines += dropped
        }
        guard !chunk.isEmpty else { return }

        lines.append(contentsOf: chunk.split(separator: "\n", omittingEmptySubsequences: false).map(String.init))
        if lines.count > Self.maxRenderedLines {
            lines.removeFirst(lines.count - Self.maxRenderedLines)
        }
    }
}

struct LaunchLogView: View {
    @ObservedObject var store: LaunchLogStore
    var onDismiss: (() -> Void)?

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "text.alignleft")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(EspressUTheme.accentOnDark)

                Text("Launch log")
                    .font(.system(size: 12, weight: .semibold, design: .rounded))
                    .foregroundColor(.white.opacity(0.9))

                if store.droppedLines > 0 {
                    Text("\(store.droppedLines) earlier lines dropped")
                        .font(.system(size: 10, weight: .regular, design: .rounded))
                        .foregroundColor(EspressUTheme.alertOnDark)
                }

                Spacer()

                Text("\(store.lines.count) lines")
                    .font(.system(size: 10, weight: .regular, design: .monospaced))
                    .foregroundColor(.white.opacity(0.6))

                if let onDismiss {
                    Button(action: onDismiss) {
                        Image(systemName: "xmark")
                            .font(.system(size: 11, weight: .semibold))
                            .foregroundColor(.white.opacity(0.6))
                            .frame(width: 44, height: 28)
                            .contentShape(Rectangle())
                    }
                    .accessibilityLabel("Close launch log")
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(Color.black.opacity(0.55))

            ScrollViewReader { proxy in
                ScrollView(showsIndicators: true) {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(Array(store.lines.enumerated()), id: \.offset) { index, line in
                            Text(line)
                                .font(.system(size: 9, weight: .regular, design: .monospaced))
                                .foregroundColor(colour(for: line))
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                }
                .onValueChange(of: store.lines.count) { count in
                    guard count > 0 else { return }
                    withAnimation(.easeOut(duration: 0.12)) {
                        proxy.scrollTo(count - 1, anchor: .bottom)
                    }
                }
            }
        }
        .background(Color.black.opacity(0.82))
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(Color.white.opacity(0.12), lineWidth: 1)
        )
    }

    private func colour(for line: String) -> Color {
        let lower = line.lowercased()
        if lower.contains("crash") || lower.contains("failed") || lower.contains("error")
            || lower.contains("dropped a frame") || lower.contains("not present")
            || lower.contains("no key in keys.txt decrypts it") {
            return EspressUTheme.alertOnDark
        }
        if lower.contains("presented the first frame") || lower.contains("run title")
            || lower.contains("first swap request") || lower.contains("cleared the empty frame")
            || lower.contains("launching real title")
            || lower.contains("osscreenflipbuffersex") {
            return .green
        }
        if lower.contains("ios display:") || lower.contains("ios data path")
            || lower.contains("keys.txt reloaded") || lower.contains("title list initialized") {
            return EspressUTheme.accentOnDark
        }
        return .white.opacity(0.72)
    }
}
