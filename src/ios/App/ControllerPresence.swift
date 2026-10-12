import SwiftUI
import GameController

enum ControllerPresence {
    static func isConnected(excluding departing: GCController? = nil) -> Bool {
        GCController.controllers().contains { $0 !== departing && $0.extendedGamepad != nil }
    }
}

struct ControllerAutoHideModifier: ViewModifier {
    @AppStorage(ControllerLayoutSettings.autoHideWithControllerKey)
    private var autoHide = ControllerLayoutSettings.defaultAutoHideWithController
    let apply: (_ hide: Bool, _ atStart: Bool) -> Void

    func body(content: Content) -> some View {
        content
            .onAppear { evaluate(departing: nil, atStart: true) }
            .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidConnect)) { _ in
                evaluate(departing: nil, atStart: false)
            }
            .onReceive(NotificationCenter.default.publisher(for: .GCControllerDidDisconnect)) { note in
                evaluate(departing: note.object as? GCController, atStart: false)
            }
            .onChange(of: autoHide) { _ in evaluate(departing: nil, atStart: false) }
    }

    private func evaluate(departing: GCController?, atStart: Bool) {
        apply(autoHide && ControllerPresence.isConnected(excluding: departing), atStart)
    }
}
