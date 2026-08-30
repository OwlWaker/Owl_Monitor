// swift-tools-version:6.2
import PackageDescription

let package = Package(
    name: "OwlMonitorApp",
    platforms: [
        .macOS(.v26)
    ],
    targets: [
        .executableTarget(
            name: "OwlMonitorApp",
            path: "Sources/OwlMonitorApp"
        )
    ]
)
