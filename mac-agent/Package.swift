// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "DeskimonAgent",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "DeskimonAgent", targets: ["DeskimonAgent"]),
        .library(name: "DeskimonCore", targets: ["DeskimonCore"]),
    ],
    targets: [
        // Foundation-only logic: intents, natural-language/date parsing, validation, wire protocol.
        .target(name: "DeskimonCore", path: "Sources/DeskimonCore"),
        .executableTarget(
            name: "DeskimonAgent",
            dependencies: ["DeskimonCore"],
            path: "Sources/DeskimonAgent",
            linkerSettings: [
                // Embed Info.plist so macOS shows Calendar / Reminders / Bluetooth / Microphone prompts
                // even when launched as a bare SwiftPM executable. scripts/build_mac_agent.sh also builds a real .app.
                .unsafeFlags(["-Xlinker", "-sectcreate", "-Xlinker", "__TEXT", "-Xlinker", "__info_plist", "-Xlinker", "Resources/Info.plist"])
            ]
        ),
        .testTarget(name: "DeskimonCoreTests", dependencies: ["DeskimonCore"], path: "Tests/DeskimonCoreTests"),
    ]
)
