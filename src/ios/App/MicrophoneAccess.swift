// SPDX-License-Identifier: MPL-2.0

import AVFoundation
import UIKit

enum MicrophoneAccess {
    static var status: AVAudioSession.RecordPermission {
        AVAudioSession.sharedInstance().recordPermission
    }

    static func request(_ completion: @escaping (Bool) -> Void) {
        switch status {
        case .granted:
            DispatchQueue.main.async { completion(true) }
        case .denied:
            DispatchQueue.main.async { completion(false) }
        default:
            AVAudioSession.sharedInstance().requestRecordPermission { granted in
                DispatchQueue.main.async { completion(granted) }
            }
        }
    }

    static func openSystemSettings() {
        if let url = URL(string: UIApplication.openSettingsURLString) {
            UIApplication.shared.open(url)
        }
    }
}
