import SwiftUI
import GameController

enum ControllerPresence {
    static func isConnected(excluding departing: GCController? = nil) -> Bool {
        GCController.controllers().contains { $0 !== departing && $0.extendedGamepad != nil }
    }
}

/// Reports whether a controller is connected; the screen decides from that and the setting what to hide.
struct ControllerAutoHideModifier: ViewModifier {
    let apply: (_ connected: Bool, _ atStart: Bool) -> Void

    func body(content: Content) -> some View {
        content
            .onAppear {
                evaluate(departing: nil, atStart: true)
                // A controller that was already paired is often not listed yet when the game opens and sends no
                // connect event, so look again a few times while the game starts.
                for delay in [0.5, 1.5, 3.0] {
                    DispatchQueue.main.asyncAfter(deadline: .now() + delay) { evaluate(departing: nil, atStart: true) }
                }
            }
            .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidConnect)) { _ in
                evaluate(departing: nil, atStart: false)
            }
            .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidDisconnect)) { note in
                evaluate(departing: note.object as? GCController, atStart: false)
            }
    }

    private func evaluate(departing: GCController?, atStart: Bool) {
        apply(ControllerPresence.isConnected(excluding: departing), atStart)
    }
}
