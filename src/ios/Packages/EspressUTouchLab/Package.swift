// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "EspressUTouchLab",
    platforms: [.iOS(.v15), .macOS(.v13)],
    products: [
        .library(name: "TouchLabCore", targets: ["TouchLabCore"]),
        .library(name: "TouchLabUI", targets: ["TouchLabUI"]),
    ],
    targets: [
        .target(name: "TouchLabCore"),
        .target(name: "TouchLabUI", dependencies: ["TouchLabCore"]),
        .executableTarget(name: "touchlab-check", dependencies: ["TouchLabCore"]),
        .executableTarget(name: "touchlab-render", dependencies: ["TouchLabCore"]),
    ]
)
