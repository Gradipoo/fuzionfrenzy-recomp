import AppKit
import Foundation

@MainActor
final class LauncherModel: ObservableObject {
    static let shared = LauncherModel()

    @Published var settings = GameSettings()
    @Published var verdict: DiscCheck.Verdict?
    @Published var prereqs: [Prereq] = []
    @Published var logLines: [String] = []
    @Published var stage = "Ready"
    @Published var progress: Double?
    @Published var busy = false
    @Published var error: String?
    @Published var gameRunning = false
    @Published var page: MenuPage = .play

    let cli = CLI.parse()
    private var bootstrapped = false
    private var forceOnce = false
    private var discURL: URL?

    var toolsReady: Bool { prereqs.allSatisfy(\.ok) }
    var canPlay: Bool { verdict?.ok == true && toolsReady && !busy }

    var supportURL: URL {
        if let path = cli.support, !path.isEmpty {
            return URL(fileURLWithPath: path, isDirectory: true)
        }
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("FuzionFrenzy", isDirectory: true)
    }

    func bootstrap() {
        if bootstrapped { return }
        bootstrapped = true
        prereqs = PrereqScan.scan()
        loadSaved()
        applyCLI()
        if let disc = cli.disc {
            choose(URL(fileURLWithPath: disc))
        } else if let saved = loadDiscPath() {
            choose(URL(fileURLWithPath: saved), remember: false)
        }
        if cli.play {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                self?.play()
            }
        }
    }

    func browse() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.prompt = "Choose"
        panel.message = "Choose a disc image (.iso), or a folder with default.xbe and the rest of the disc."
        if let current = discURL?.deletingLastPathComponent() {
            panel.directoryURL = current
        }
        panel.begin { [weak self] response in
            guard response == .OK, let url = panel.url else { return }
            _ = url.startAccessingSecurityScopedResource()
            Task { @MainActor in
                self?.choose(url)
            }
        }
    }

    func choose(_ url: URL, remember: Bool = true) {
        discURL = url
        let found = DiscCheck.inspect(url)
        verdict = found
        error = found.ok ? nil : found.title + "\n" + found.detail
        if found.ok, remember { persist() }
    }

    func play() {
        guard let verdict, verdict.ok, !busy else { return }
        prereqs = PrereqScan.scan()
        if !toolsReady {
            error = "Install the missing tools, then press Play."
            return
        }
        guard let source = portSource() else {
            error = "This copy of the app is missing the port sources it needs to compile."
            return
        }
        persist()
        busy = true
        gameRunning = false
        error = nil
        logLines = []
        stage = "Starting"
        progress = nil
        let force = forceOnce || cli.rebuild
        forceOnce = false
        let job = Job(
            discFolder: verdict.folder?.path ?? "",
            discISO: verdict.iso?.path,
            support: supportURL.path,
            portSource: source.path,
            version: portVersion(),
            settingsTOML: settings.toml(),
            mute: !settings.sound,
            autostart: settings.scriptedPad,
            perf: cli.perf,
            forceRebuild: force
        )
        Task.detached {
            let failure = Pipeline.run(job) { event in
                DispatchQueue.main.async {
                    LauncherModel.shared.apply(event)
                }
            }
            DispatchQueue.main.async {
                LauncherModel.shared.finish(failure)
            }
        }
    }

    func rebuild() {
        forceOnce = true
        play()
    }

    func copyInstallCommands() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(Arena.installCommands, forType: .string)
    }

    func apply(_ event: JobEvent) {
        switch event {
        case .log(let line):
            logLines.append(line)
            if logLines.count > 400 {
                logLines.removeFirst(logLines.count - 400)
            }
            appendBuildLog(line)
        case .stage(let name):
            stage = name
            appendBuildLog("== \(name)")
        case .progress(let value):
            progress = value
        case .failed(let message):
            error = message
            appendBuildLog("FAILED: \(message)")
        }
    }

    private func appendBuildLog(_ line: String) {
        let url = supportURL.appendingPathComponent("build.log")
        try? FileManager.default.createDirectory(at: supportURL, withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }
        if let handle = try? FileHandle(forWritingTo: url) {
            handle.seekToEndOfFile()
            if let data = (line + "\n").data(using: .utf8) {
                handle.write(data)
            }
            try? handle.close()
        }
    }

    func finish(_ failure: String?) {
        busy = false
        if let failure {
            if error == nil { error = failure }
            stage = "Stopped"
        } else {
            gameRunning = true
            stage = "Running"
            error = nil
        }
    }

    func pretty(path: String) -> String {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        if path.hasPrefix(home) { return "~" + path.dropFirst(home.count) }
        return path
    }

    private func portSource() -> URL? {
        guard let resource = Bundle.main.resourceURL?.appendingPathComponent("port", isDirectory: true) else { return nil }
        let script = resource.appendingPathComponent("build.sh")
        return FileManager.default.fileExists(atPath: script.path) ? resource : nil
    }

    private func portVersion() -> String {
        guard let url = Bundle.main.resourceURL?.appendingPathComponent("port-version"),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return "dev" }
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "dev" : trimmed
    }

    private func stateURL() -> URL {
        supportURL.appendingPathComponent("launcher.json")
    }

    private func loadDiscPath() -> String? {
        guard let data = try? Data(contentsOf: stateURL()),
              let saved = try? JSONDecoder().decode(SavedLauncher.self, from: data) else { return nil }
        return saved.discPath
    }

    private func loadSaved() {
        guard let data = try? Data(contentsOf: stateURL()),
              let saved = try? JSONDecoder().decode(SavedLauncher.self, from: data) else { return }
        settings = saved.settings
        let tomlURL = supportURL.appendingPathComponent("src/out/build/macos-release/settings.toml")
        if let text = try? String(contentsOf: tomlURL, encoding: .utf8) {
            settings.apply(toml: text)
        }
    }

    private func applyCLI() {
        if let fps = cli.fps { settings.fpsCap = fps }
        if cli.windowed {
            settings.displayMode = "windowed"
            if settings.width == 0 || settings.height == 0 {
                settings.width = 1280
                settings.height = 720
            }
        }
        if cli.mute { settings.sound = false }
        if cli.autostart { settings.scriptedPad = true }
    }

    func persist() {
        let saved = SavedLauncher(discPath: discURL?.path, settings: settings, builtVersion: portVersion())
        guard let data = try? JSONEncoder().encode(saved) else { return }
        try? FileManager.default.createDirectory(at: supportURL, withIntermediateDirectories: true)
        try? data.write(to: stateURL(), options: .atomic)
    }
}
