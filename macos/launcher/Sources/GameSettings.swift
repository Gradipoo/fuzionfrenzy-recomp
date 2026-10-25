import Foundation

/// The keys `sdk/runtime/app/settings.cpp` reads from `settings.toml` beside the
/// game executable, plus the two switches that are environment variables rather
/// than file keys: sound (`XBE_MUTE`) and the scripted pad (`FUZION_AUTOSTART`).
struct GameSettings: Codable, Equatable {
    var displayMode: String = "borderless"
    var width: Int = 0
    var height: Int = 0
    var renderPercent: Int = 100
    var fpsCap: Int = 60
    var vsync: Bool = true
    var aniso: Int = 0
    var aspect: String = "auto"
    var ultrawide: Bool = true
    var joinIP: String = ""
    var sound: Bool = true
    var scriptedPad: Bool = false

    static let modes = ["borderless", "fullscreen", "windowed"]
    static let modeNames = ["Borderless", "Fullscreen", "Windowed"]
    static let aspects = ["auto", "4:3", "16:9", "16:10", "21:9", "32:9"]
    static let anisoValues = [0, 2, 4, 8, 16]
    static let renderSteps = [50, 75, 100, 125, 150, 200]
    static let resolutions: [(Int, Int, String)] = [
        (0, 0, "Desktop"),
        (1280, 720, "1280 × 720"),
        (1280, 800, "1280 × 800"),
        (1600, 900, "1600 × 900"),
        (1920, 1080, "1920 × 1080"),
        (1920, 1200, "1920 × 1200"),
        (2560, 1440, "2560 × 1440"),
        (2560, 1080, "2560 × 1080"),
        (3440, 1440, "3440 × 1440"),
        (3840, 2160, "3840 × 2160"),
    ]
    static let caps: [(Int, String)] = [
        (30, "30 fps · low power"),
        (60, "60 fps"),
        (0, "Unlimited"),
        (72, "72 fps"),
        (120, "120 fps"),
        (144, "144 fps"),
        (165, "165 fps"),
        (240, "240 fps"),
    ]

    var resolutionLabel: String {
        GameSettings.resolutions.first { $0.0 == width && $0.1 == height }?.2
            ?? (width == 0 || height == 0 ? "Desktop" : "\(width) × \(height)")
    }

    func toml() -> String {
        let ip = joinIP.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        return """
        display_mode = "\(displayMode)"
        width = \(width)
        height = \(height)
        render_percent = \(renderPercent)
        fps_cap = \(fpsCap)
        vsync = \(vsync ? "true" : "false")
        anisotropic_filtering = \(aniso)
        aspect_ratio = "\(aspect)"
        ultrawide = \(ultrawide ? "true" : "false")
        join_ip = "\(ip)"
        hdr = "off"
        hdr_paper_white = 200
        hdr_peak = 0

        """
    }

    mutating func apply(toml text: String) {
        for raw in text.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix("#") { continue }
            let parts = line.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
            guard parts.count == 2 else { continue }
            let key = parts[0]
            var value = parts[1]
            if value.hasPrefix("\""), value.hasSuffix("\""), value.count >= 2 {
                value = String(value.dropFirst().dropLast())
            }
            switch key {
            case "display_mode":
                if GameSettings.modes.contains(value) { displayMode = value }
            case "width": width = Int(value) ?? width
            case "height": height = Int(value) ?? height
            case "render_percent": renderPercent = Int(value) ?? renderPercent
            case "fps_cap": fpsCap = Int(value) ?? fpsCap
            case "vsync": vsync = value == "true"
            case "anisotropic_filtering": aniso = Int(value) ?? aniso
            case "aspect_ratio":
                if GameSettings.aspects.contains(value) { aspect = value }
            case "ultrawide": ultrawide = value == "true"
            case "join_ip": joinIP = value
            default: break
            }
        }
    }
}

struct SavedLauncher: Codable {
    var discPath: String?
    var settings: GameSettings
    var builtVersion: String?
}

struct CLI: Equatable {
    var disc: String?
    var fps: Int?
    var support: String?
    var play = false
    var mute = false
    var perf = false
    var windowed = false
    var rebuild = false
    var autostart = false

    static func parse() -> CLI {
        var cli = CLI()
        var args = Array(CommandLine.arguments.dropFirst())
        func take() -> String? {
            guard !args.isEmpty else { return nil }
            return args.removeFirst()
        }
        while !args.isEmpty {
            switch args.removeFirst() {
            case "--disc": cli.disc = take()
            case "--fps": cli.fps = Int(take() ?? "")
            case "--support": cli.support = take()
            case "--play": cli.play = true
            case "--mute": cli.mute = true
            case "--perf": cli.perf = true
            case "--windowed": cli.windowed = true
            case "--rebuild": cli.rebuild = true
            case "--autostart": cli.autostart = true
            default: break
            }
        }
        return cli
    }
}
