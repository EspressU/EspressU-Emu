import Foundation
#if os(iOS)
import UIKit
#endif

enum PlatformCapabilities {

    enum Running {
        static let version: OperatingSystemVersion = ProcessInfo.processInfo.operatingSystemVersion

        static func atLeast(_ major: Int, _ minor: Int = 0, _ patch: Int = 0) -> Bool {
            ProcessInfo.processInfo.isOperatingSystemAtLeast(
                OperatingSystemVersion(majorVersion: major, minorVersion: minor, patchVersion: patch))
        }

        static var isIOS16OrLater: Bool { atLeast(16) }
        static var isIOS26OrLater: Bool { atLeast(26) }
        static var isIOS27OrLater: Bool { atLeast(27) }

        static var displayString: String {
            "\(version.majorVersion).\(version.minorVersion)"
                + (version.patchVersion > 0 ? ".\(version.patchVersion)" : "")
        }
    }

    enum SDK {
        #if compiler(>=6.4)
        static let hasIOS27 = true
        #else
        static let hasIOS27 = false
        #endif
    }

    static var expectsContinuousIPadResize: Bool {
        #if os(iOS)
        return Running.isIOS27OrLater && UIDevice.current.userInterfaceIdiom == .pad
        #else
        return false
        #endif
    }

    static var summary: String {
        var parts = ["iOS \(Running.displayString)"]
        parts.append(Running.isIOS27OrLater ? "27+ detected" : "pre-27")
        parts.append(SDK.hasIOS27 ? "built with 27 SDK" : "built with pre-27 SDK")
        if Running.isIOS27OrLater && !SDK.hasIOS27 {
            parts.append("new APIs unavailable to this build")
        }
        if expectsContinuousIPadResize {
            parts.append("continuous iPad resize")
        }
        return parts.joined(separator: " · ")
    }
}
