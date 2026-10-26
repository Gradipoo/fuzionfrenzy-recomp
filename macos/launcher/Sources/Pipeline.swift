import Foundation

struct Job: Sendable {
    var discFolder: String
    var discISO: String?
    var support: String
    var portSource: String
    var version: String
    var settingsTOML: String
    var mute: Bool
    var autostart: Bool
    var perf: Bool
    var forceRebuild: Bool
}

enum JobEvent: Sendable {
    case log(String)
    case stage(String)
    case progress(Double?)
    case failed(String)
}

enum Pipeline {
    static func run(_ job: Job, emit: @escaping @Sendable (JobEvent) -> Void) -> String? {
        let fm = FileManager.default
        let support = URL(fileURLWithPath: job.support, isDirectory: true)
        let src = support.appendingPathComponent("src", isDirectory: true)
        let binary = src.appendingPathComponent("out/build/macos-release/RingHost")
        do {
            try fm.createDirectory(at: support, withIntermediateDirectories: true)
            let blockers = PrereqScan.blockers(PrereqScan.scan())
            if !blockers.isEmpty {
                let lines = blockers.map { "\($0.title): \($0.detail)" }.joined(separator: "\n")
                emit(.failed("Missing tools.\n" + lines + "\n\n" + ArenaInstall))
                return "Missing tools"
            }

            let stampURL = src.appendingPathComponent(".port-version")
            let have = (try? String(contentsOf: stampURL, encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            let sourcesReady = have == job.version && fm.fileExists(atPath: src.appendingPathComponent("build.sh").path)
            if job.forceRebuild || !sourcesReady {
                emit(.stage("Copying the port sources"))
                emit(.progress(nil))
                try copyPort(from: job.portSource, to: src, emit: emit)
                try job.version.write(to: stampURL, atomically: true, encoding: .utf8)
            }

            var folder = job.discFolder
            if let iso = job.discISO {
                emit(.stage("Extracting the disc"))
                emit(.progress(nil))
                let dest = support.appendingPathComponent("disc", isDirectory: true)
                try fm.createDirectory(at: dest, withIntermediateDirectories: true)
                let script = src.appendingPathComponent("tools/xiso_extract.py").path
                let code = shell(
                    "/usr/bin/env",
                    ["python3", script, "extract", iso, dest.path],
                    cwd: src,
                    emit: emit
                )
                if code != 0 {
                    emit(.failed("Could not read that disc image. It needs to be an Xbox XDVDFS .iso."))
                    return "extract failed"
                }
                let verdict = DiscCheck.inspectFolder(dest)
                guard verdict.ok, verdict.confirmed else {
                    emit(.failed(verdict.title + "\n" + verdict.detail))
                    return verdict.title
                }
                emit(.log(verdict.title))
                emit(.log(verdict.detail))
                folder = dest.path
            } else {
                let verdict = DiscCheck.inspectFolder(URL(fileURLWithPath: folder))
                guard verdict.ok, verdict.confirmed else {
                    emit(.failed(verdict.title + "\n" + verdict.detail))
                    return verdict.title
                }
            }

            let assets = src.appendingPathComponent("assets")
            try? fm.removeItem(at: assets)
            try fm.createSymbolicLink(at: assets, withDestinationURL: URL(fileURLWithPath: folder, isDirectory: true))

            let builtStamp = support.appendingPathComponent("built-version")
            let built = (try? String(contentsOf: builtStamp, encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            let needBuild = job.forceRebuild || built != job.version || !fm.fileExists(atPath: binary.path)
            if needBuild {
                if job.forceRebuild || built != job.version {
                    try? fm.removeItem(at: src.appendingPathComponent("generated"))
                    try? fm.removeItem(at: src.appendingPathComponent("out"))
                }
                emit(.stage("Translating and compiling"))
                emit(.log("First build downloads SDL and compiles the game from your disc. It takes a while."))
                emit(.progress(nil))
                let code = shell(src.appendingPathComponent("build.sh").path, ["macos-release"], cwd: src, emit: emit)
                if code != 0 || !fm.fileExists(atPath: binary.path) {
                    emit(.failed("The compile stopped. The last lines above are the reason. A missing tool, or a failed download of SDL, is the usual cause."))
                    return "build failed"
                }
                try job.version.write(to: builtStamp, atomically: true, encoding: .utf8)
            } else {
                emit(.log("Using the build already on this Mac."))
            }

            emit(.stage("Writing settings"))
            let gameDir = binary.deletingLastPathComponent()
            try fm.createDirectory(at: gameDir, withIntermediateDirectories: true)
            try job.settingsTOML.write(to: gameDir.appendingPathComponent("settings.toml"), atomically: true, encoding: .utf8)

            emit(.stage("Launching"))
            let pid = try launch(binary: binary, job: job, support: support)
            emit(.log("Game pid \(pid)"))
            emit(.stage("Running"))
            emit(.progress(1))
            return nil
        } catch {
            emit(.failed(error.localizedDescription))
            return error.localizedDescription
        }
    }

    private static func copyPort(from source: String, to dest: URL, emit: @escaping (JobEvent) -> Void) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: dest, withIntermediateDirectories: true)
        let code = shell("/usr/bin/rsync", ["-a", "--delete", "--exclude", ".git", source + "/", dest.path + "/"], cwd: dest, emit: emit)
        if code != 0 { throw PipeError("Could not copy the port sources out of the app.") }
        for name in ["build.sh", "tools/apply-macos-sdk.sh"] {
            try? fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: dest.appendingPathComponent(name).path)
        }
    }

    private static func launch(binary: URL, job: Job, support: URL) throws -> Int32 {
        let process = Process()
        process.executableURL = binary
        process.currentDirectoryURL = binary.deletingLastPathComponent()
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = PrereqScan.buildPath
        if job.mute { env["XBE_MUTE"] = "1" } else { env.removeValue(forKey: "XBE_MUTE") }
        if job.autostart { env["FUZION_AUTOSTART"] = "1" } else { env.removeValue(forKey: "FUZION_AUTOSTART") }
        if job.perf { env["XBE_PERF"] = "1" }
        // Same hints input.cpp sets before SDL_Init. A wired Xbox One pad is
        // opened by HIDAPI through libusb. MFI is left alone so Bluetooth
        // stays on GameController.
        env["SDL_JOYSTICK_HIDAPI"] = "1"
        env["SDL_JOYSTICK_HIDAPI_XBOX"] = "1"
        env["SDL_JOYSTICK_HIDAPI_XBOX_ONE"] = "1"
        env["SDL_HIDAPI_LIBUSB"] = "1"
        process.environment = env
        let logURL = support.appendingPathComponent("launch.log")
        FileManager.default.createFile(atPath: logURL.path, contents: nil)
        let handle = try FileHandle(forWritingTo: logURL)
        process.standardOutput = handle
        process.standardError = handle
        try process.run()
        // Keep both alive. Releasing the handle closes the game's stdout, and
        // releasing Process on some OS versions signals the child.
        Held.processes.append(process)
        Held.handles.append(handle)
        try "\(process.processIdentifier)\n".write(to: support.appendingPathComponent("game.pid"), atomically: true, encoding: .utf8)
        return process.processIdentifier
    }

    @discardableResult
    private static func shell(_ launch: String, _ args: [String], cwd: URL, emit: @escaping (JobEvent) -> Void) -> Int32 {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: launch)
        process.arguments = args
        process.currentDirectoryURL = cwd
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = PrereqScan.buildPath
        env["GIT_TERMINAL_PROMPT"] = "0"
        process.environment = env
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        let handle = pipe.fileHandleForReading
        let lock = NSLock()
        var buffer = Data()
        handle.readabilityHandler = { reader in
            let chunk = reader.availableData
            guard !chunk.isEmpty else { return }
            var lines: [String] = []
            lock.lock()
            buffer.append(chunk)
            while let range = buffer.rangeOfNewline() {
                let piece = buffer.subdata(in: buffer.startIndex..<range.lowerBound)
                buffer.removeSubrange(buffer.startIndex..<range.upperBound)
                if let text = String(data: piece, encoding: .utf8) {
                    lines.append(text)
                }
            }
            lock.unlock()
            for line in lines { note(line, emit: emit) }
        }
        do { try process.run() } catch {
            emit(.log(error.localizedDescription))
            return 1
        }
        process.waitUntilExit()
        handle.readabilityHandler = nil
        let rest = handle.readDataToEndOfFile()
        if let text = String(data: rest, encoding: .utf8) {
            for line in text.split(whereSeparator: { $0 == "\n" || $0 == "\r" }) {
                note(String(line), emit: emit)
            }
        }
        return process.terminationStatus
    }

    private static func note(_ line: String, emit: (JobEvent) -> Void) {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return }
        emit(.log(String(trimmed.prefix(2000))))
        if trimmed.contains("Recompiling") { emit(.stage("Translating the executable")) }
        else if trimmed.hasPrefix("[1/3]") { emit(.stage("Checking the compiler")) }
        else if trimmed.hasPrefix("[2/3]") { emit(.stage("Reading the executable")) }
        else if trimmed.hasPrefix("[3/3]") { emit(.stage("Compiling")) }
        if trimmed.hasPrefix("["), let close = trimmed.firstIndex(of: "]") {
            let inner = trimmed[trimmed.index(after: trimmed.startIndex)..<close]
            let parts = inner.split(separator: "/")
            if parts.count == 2, let done = Int(parts[0]), let total = Int(parts[1]), total > 0, done <= total {
                emit(.progress(Double(done) / Double(total)))
            }
        }
    }
}

private enum Held {
    static var processes: [Process] = []
    static var handles: [FileHandle] = []
}

private let ArenaInstall = """
Install the missing pieces, then try again:

\(Arena.installCommands)
"""

private struct PipeError: LocalizedError {
    var message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}

private extension Data {
    func rangeOfNewline() -> Range<Data.Index>? {
        for index in indices {
            if self[index] == 0x0A || self[index] == 0x0D {
                return index..<self.index(after: index)
            }
        }
        return nil
    }
}
