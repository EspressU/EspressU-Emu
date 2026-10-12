// SPDX-License-Identifier: MPL-2.0

import UIKit
import SwiftUI

@MainActor
final class ControllerScreenCursor {
    static let shared = ControllerScreenCursor()

    private var focused: NSObject?
    private weak var screen: UIViewController?
    private let ring: UIView = {
        let view = UIView()
        view.isUserInteractionEnabled = false
        view.layer.borderWidth = 3
        view.layer.cornerRadius = 12
        view.layer.cornerCurve = .continuous
        view.backgroundColor = .clear
        return view
    }()

    static var topPresented: UIViewController? {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
        let window = windows.first { $0.isKeyWindow } ?? windows.first
        var top = window?.rootViewController?.presentedViewController
        while let next = top?.presentedViewController, !next.isBeingDismissed { top = next }
        return top
    }

    static var topIsOpaque: Bool {
        guard let top = topPresented else { return false }
        return top is UIDocumentPickerViewController || String(describing: type(of: top)).contains("DocumentPicker")
    }

    func handle(_ command: ControllerMenuCommand) {
        guard let top = Self.topPresented, !Self.topIsOpaque else { return }
        if top !== screen {
            screen = top
            focused = nil
        }
        let elements = interactiveElements(in: top.view)
        if let current = focused, !elements.contains(where: { $0 === current }) { focused = nil }

        switch command {
        case .up, .down, .left, .right:
            if let current = focused, (command == .left || command == .right), isAdjustable(current) {
                command == .right ? current.accessibilityIncrement() : current.accessibilityDecrement()
                return
            }
            guard let current = focused else {
                focus(firstVisible(elements, in: top.view), in: top)
                return
            }
            if let next = neighbour(of: current, in: elements, toward: command) {
                focus(next, in: top)
            } else if command == .up || command == .down {
                scrollPage(in: top.view, down: command == .down)
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { [weak self] in
                    guard let self, self.screen === top else { return }
                    let fresh = self.interactiveElements(in: top.view)
                    if let next = self.neighbour(of: current, in: fresh, toward: command) { self.focus(next, in: top) }
                    else { self.placeRing() }
                }
            }
        case .confirm:
            guard let current = focused else {
                focus(firstVisible(elements, in: top.view), in: top)
                return
            }
            activate(current)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { [weak self] in self?.revalidate() }
        case .back, .menu:
            if let field = typingField, field.isFirstResponder {
                field.resignFirstResponder()
                typingField = nil
                return
            }
            goBack(from: top, elements: elements)
            focused = nil
            ring.removeFromSuperview()
        case .previousPage, .nextPage:
            scrollPage(in: top.view, down: command == .nextPage)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
                guard let self, self.screen === top else { return }
                self.focus(self.firstVisible(self.interactiveElements(in: top.view), in: top.view), in: top)
            }
        case .alternate, .select:
            break
        }
    }

    func validate() {
        guard ring.superview != nil else { return }
        if screen == nil || Self.topPresented !== screen { reset() }
    }

    func reset() {
        focused = nil
        screen = nil
        ring.removeFromSuperview()
    }

    private func interactiveElements(in view: UIView) -> [NSObject] {
        var found: [NSObject] = []
        collect(from: view, into: &found, depth: 0)
        return found.filter { element in
            let frame = element.accessibilityFrame
            return frame.width > 2 && frame.height > 2
        }
    }

    private func collect(from object: NSObject, into found: inout [NSObject], depth: Int) {
        guard depth < 60 else { return }
        if let view = object as? UIView, view.isHidden || view.alpha < 0.02 { return }
        if object.isAccessibilityElement {
            if isInteractive(object) { found.append(object) }
            return
        }
        if let custom = object.accessibilityElements as? [NSObject], !custom.isEmpty {
            custom.forEach { collect(from: $0, into: &found, depth: depth + 1) }
            return
        }
        let count = object.accessibilityElementCount()
        if count != NSNotFound, count > 0 {
            for index in 0..<count {
                if let element = object.accessibilityElement(at: index) as? NSObject {
                    collect(from: element, into: &found, depth: depth + 1)
                }
            }
            return
        }
        if let view = object as? UIView {
            view.subviews.forEach { collect(from: $0, into: &found, depth: depth + 1) }
        }
    }

    private func isInteractive(_ element: NSObject) -> Bool {
        let traits = element.accessibilityTraits
        if traits.contains(.notEnabled) { return false }
        if element is UITextField || element is UITextView { return true }
        if let control = element as? UIControl { return control.isEnabled }
        return traits.contains(.button) || traits.contains(.link) || traits.contains(.adjustable)
            || traits.contains(.searchField) || traits.contains(.keyboardKey)
    }

    private func isAdjustable(_ element: NSObject) -> Bool {
        element.accessibilityTraits.contains(.adjustable)
    }

    private func visibleRect(of view: UIView) -> CGRect {
        UIAccessibility.convertToScreenCoordinates(view.bounds, in: view)
    }

    private func firstVisible(_ elements: [NSObject], in view: UIView) -> NSObject? {
        let visible = visibleRect(of: view)
        let onScreen = elements.filter { visible.intersects($0.accessibilityFrame) }
        return (onScreen.isEmpty ? elements : onScreen).min { a, b in
            let fa = a.accessibilityFrame, fb = b.accessibilityFrame
            return abs(fa.minY - fb.minY) > 8 ? fa.minY < fb.minY : fa.minX < fb.minX
        }
    }

    private func neighbour(of current: NSObject, in elements: [NSObject], toward command: ControllerMenuCommand) -> NSObject? {
        let from = current.accessibilityFrame
        var best: (NSObject, CGFloat)?
        for element in elements where element !== current {
            let to = element.accessibilityFrame
            let along: CGFloat
            let across: CGFloat
            switch command {
            case .down: along = to.midY - from.midY; across = abs(to.midX - from.midX)
            case .up: along = from.midY - to.midY; across = abs(to.midX - from.midX)
            case .right: along = to.midX - from.midX; across = abs(to.midY - from.midY)
            default: along = from.midX - to.midX; across = abs(to.midY - from.midY)
            }
            guard along > 6 else { continue }
            if (command == .left || command == .right) && across > max(from.height, to.height) { continue }
            let score = along + across * (command == .up || command == .down ? 0.35 : 2)
            if best == nil || score < best!.1 { best = (element, score) }
        }
        return best?.0
    }

    private func focus(_ element: NSObject?, in top: UIViewController) {
        guard let element else { return }
        focused = element
        if let scroll = enclosingScrollView(of: element) {
            let rect = Self.fromScreen(element.accessibilityFrame, in: scroll)
            scroll.scrollRectToVisible(rect.insetBy(dx: 0, dy: -48), animated: true)
        }
        if UIAccessibility.isVoiceOverRunning {
            UIAccessibility.post(notification: .layoutChanged, argument: element)
        }
        placeRing()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.32) { [weak self] in self?.placeRing() }
    }

    private static func fromScreen(_ rect: CGRect, in view: UIView) -> CGRect {
        guard let screen = (view as? UIWindow)?.screen ?? view.window?.screen else { return rect }
        return view.convert(rect, from: screen.coordinateSpace)
    }

    private func placeRing() {
        guard let element = focused, let window = screen?.view.window else {
            ring.removeFromSuperview()
            return
        }
        ring.layer.borderColor = UIColor(EspressUTheme.pixelBlue).cgColor
        let frame = Self.fromScreen(element.accessibilityFrame, in: window)
        if ring.superview !== window { window.addSubview(ring) }
        window.bringSubviewToFront(ring)
        UIView.animate(withDuration: 0.15) { self.ring.frame = frame.insetBy(dx: -4, dy: -4) }
    }

    private weak var typingField: UIResponder?

    private func activate(_ element: NSObject) {
        if let field = element as? UIResponder, element is UITextField || element is UITextView {
            typingField = field
            field.becomeFirstResponder()
            return
        }
        if element.accessibilityTraits.contains(.searchField) || element.accessibilityTraits.contains(.keyboardKey),
           let view = element as? UIView {
            typingField = view
            view.becomeFirstResponder()
            return
        }
        if let toggle = element as? UISwitch {
            toggle.setOn(!toggle.isOn, animated: true)
            toggle.sendActions(for: .valueChanged)
            return
        }
        if element.accessibilityActivate() { return }
        if let control = element as? UIControl {
            control.sendActions(for: .primaryActionTriggered)
            control.sendActions(for: .touchUpInside)
        }
    }

    private func revalidate() {
        guard let top = Self.topPresented, !Self.topIsOpaque else { reset(); return }
        if top !== screen { screen = top; focused = nil; ring.removeFromSuperview(); return }
        let elements = interactiveElements(in: top.view)
        if let current = focused, elements.contains(where: { $0 === current }) { placeRing() }
        else { focused = nil; ring.removeFromSuperview() }
    }

    private func goBack(from top: UIViewController, elements: [NSObject]) {
        if let navigation = navigationController(in: top), navigation.viewControllers.count > 1 {
            navigation.popViewController(animated: true)
            return
        }
        let closeWords = ["done", "close", "cancel", "fertig", "schließen", "abbrechen", "ok"]
        if let close = elements.first(where: { closeWords.contains(($0.accessibilityLabel ?? "").lowercased()) }) {
            activate(close)
            return
        }
        top.dismiss(animated: true)
    }

    private func navigationController(in controller: UIViewController) -> UINavigationController? {
        if let navigation = controller as? UINavigationController { return navigation }
        for child in controller.children {
            if let found = navigationController(in: child) { return found }
        }
        return nil
    }

    private func enclosingScrollView(of element: NSObject) -> UIScrollView? {
        var object: Any? = element
        while let current = object {
            if let view = current as? UIView {
                var candidate: UIView? = view
                while let v = candidate {
                    if let scroll = v as? UIScrollView, scroll.isScrollEnabled,
                       scroll.contentSize.height > scroll.bounds.height + 1 { return scroll }
                    candidate = v.superview
                }
                return nil
            }
            object = (current as? UIAccessibilityElement)?.accessibilityContainer
        }
        return nil
    }

    private func scrollPage(in view: UIView, down: Bool) {
        var best: UIScrollView?
        func search(_ v: UIView) {
            if let scroll = v as? UIScrollView, !v.isHidden, scroll.isScrollEnabled,
               scroll.contentSize.height > scroll.bounds.height + 1,
               scroll.bounds.height * scroll.bounds.width > (best.map { $0.bounds.height * $0.bounds.width } ?? 0) {
                best = scroll
            }
            v.subviews.forEach(search)
        }
        search(view)
        guard let scroll = best else { return }
        let inset = scroll.adjustedContentInset
        let maxY = max(-inset.top, scroll.contentSize.height - scroll.bounds.height + inset.bottom)
        let step = scroll.bounds.height * 0.6 * (down ? 1 : -1)
        let y = min(max(scroll.contentOffset.y + step, -inset.top), maxY)
        scroll.setContentOffset(CGPoint(x: scroll.contentOffset.x, y: y), animated: true)
    }
}
