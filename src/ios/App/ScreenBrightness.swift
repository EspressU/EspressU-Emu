// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit

enum BrightnessBoost {
    static let key = "espressu.display.brightnessBoost"
    static let defaultValue = 0.0
    static let maxValue = 1.0
    static let maxOpacity: Float = 0.5

    static var opacity: Float {
        let v = UserDefaults.standard.object(forKey: key) as? Double ?? defaultValue
        return Float(min(max(v, 0), maxValue)) * maxOpacity
    }

    static func label(for value: Double) -> String {
        if value <= 0.001 { return "Off" }
        switch value {
        case ..<0.34: return "Low"
        case ..<0.67: return "Medium"
        default: return "Strong"
        }
    }
}

final class BrightnessBoostLayer: CALayer {
    override init() {
        super.init()
        setup()
    }

    override init(layer: Any) {
        super.init(layer: layer)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }

    private func setup() {
        backgroundColor = UIColor.white.cgColor
        compositingFilter = "screenBlendMode"
        zPosition = 100_000
        isOpaque = false
        actions = ["opacity": NSNull(), "bounds": NSNull(), "position": NSNull(), "hidden": NSNull()]
        refresh()
    }

    func refresh() {
        let o = BrightnessBoost.opacity
        opacity = o
        isHidden = o <= 0
    }
}

final class BrightnessBoostHost {
    private let layer = BrightnessBoostLayer()
    private var observer: NSObjectProtocol?

    init(view: UIView) {
        view.layer.addSublayer(layer)
        layer.frame = view.bounds
        observer = NotificationCenter.default.addObserver(
            forName: UserDefaults.didChangeNotification, object: nil, queue: .main
        ) { [weak self] _ in self?.layer.refresh() }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
        layer.removeFromSuperlayer()
    }

    func layout(in view: UIView) {
        if layer.superlayer !== view.layer { view.layer.addSublayer(layer) }
        layer.frame = view.bounds
        layer.refresh()
    }
}

final class ScreenBrightness {
    static let shared = ScreenBrightness()
    static let maxWhilePlayingKey = "espressu.display.maxBrightnessWhilePlaying"
    static let defaultMaxWhilePlaying = false

    private var gameRunning = false
    private var appActive = true
    private var saved: CGFloat?
    private var started = false

    private var wanted: Bool {
        let on = UserDefaults.standard.object(forKey: Self.maxWhilePlayingKey) as? Bool ?? Self.defaultMaxWhilePlaying
        return on && gameRunning && appActive
    }

    func setGameRunning(_ running: Bool) {
        startIfNeeded()
        gameRunning = running
        apply()
    }

    private func startIfNeeded() {
        guard !started else { return }
        started = true
        appActive = UIApplication.shared.applicationState == .active
        let nc = NotificationCenter.default
        nc.addObserver(forName: UIApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            self?.appActive = true
            self?.apply()
        }
        for name in [UIApplication.willResignActiveNotification, UIApplication.didEnterBackgroundNotification] {
            nc.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.appActive = false
                self?.apply()
            }
        }
        nc.addObserver(forName: UserDefaults.didChangeNotification, object: nil, queue: .main) { [weak self] _ in
            self?.apply()
        }
    }

    private func apply() {
        if wanted {
            if saved == nil { saved = UIScreen.main.brightness }
            if UIScreen.main.brightness < 1.0 { UIScreen.main.brightness = 1.0 }
        } else if let previous = saved {
            saved = nil
            UIScreen.main.brightness = previous
        }
    }
}
