import SwiftUI
import UIKit

enum AudioChannelSetting: Int, CaseIterable, Identifiable {
    case mono = 0
    case stereo = 1
    case surround = 2

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .mono:     return "Mono"
        case .stereo:   return "Stereo"
        case .surround: return "Surround"
        }
    }
}

enum AudioSettings {
    static let tvEnabledKey = "espressu.audio.tvEnabled"
    static let defaultTvEnabled = true

    static let tvVolumeKey = "espressu.audio.tvVolume"
    static let defaultTvVolume = 50

    static let tvChannelsKey = "espressu.audio.tvChannels"
    static let defaultTvChannels = AudioChannelSetting.stereo.rawValue

    static let padEnabledKey = "espressu.audio.padEnabled"
    static let defaultPadEnabled = false

    static let padVolumeKey = "espressu.audio.padVolume"
    static let defaultPadVolume = 50

    static let padChannelsKey = "espressu.audio.padChannels"
    static let defaultPadChannels = AudioChannelSetting.stereo.rawValue

    static let microphoneEnabledKey = "espressu.audio.microphoneEnabled"
    static let defaultMicrophoneEnabled = false

    static let inputVolumeKey = "espressu.audio.inputVolume"
    static let defaultInputVolume = 50
}
