import SwiftUI

enum NotificationSettings {
    static let positionKey = "espressu.notification.position"
    static let defaultPosition = ScreenPosition.topLeft

    static let textColorKey = "espressu.notification.textColor"
    static let defaultTextColor: Int = 0xFFFFFFFF

    static let textScaleKey = "espressu.notification.textScale"
    static let defaultTextScale = 100

    static let controllerProfilesKey = "espressu.notification.controllerProfiles"
    static let defaultControllerProfiles = true

    static let controllerBatteryKey = "espressu.notification.controllerBattery"
    static let defaultControllerBattery = false

    static let shaderCompilingKey = "espressu.notification.shaderCompiling"
    static let defaultShaderCompiling = true

    static let friendsKey = "espressu.notification.friends"
    static let defaultFriends = true
}
