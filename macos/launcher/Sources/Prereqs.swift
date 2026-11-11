import Foundation

struct Prereq: Identifiable, Equatable {
    var id: String
    var title: String
    var ok: Bool
    var detail: String
}

enum PrereqScan {
    /// Bundled player tools, before the port is copied out of the app.
    static var bundleToolchain: URL? {
        guard let root = Bundle.main.resourceURL?.appendingPathComponent("port/toolchain", isDirectory: true) else {
            return nil
        }
        var isDir: ObjCBool = false
        if FileManager.default.fileExists(atPath: root.path, isDirectory: &isDir), isDir.boolValue {
            return root
        }
        return nil
    }

    static var toolchainBin: String? {
        guard let bin = bundleToolchain?.appendingPathComponent("bin", isDirectory: true) else { return nil }
        var isDir: ObjCBool = false
        if FileManager.default.fileExists(atPath: bin.path, isDirectory: &isDir), isDir.boolValue {
            return bin.path
        }
        return nil
    }

    static var buildPath: String {
        let prefix = "/opt/homebrew/bin:/usr/local/bin"
        let current = ProcessInfo.processInfo.environment["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin"
        let base = prefix + ":" + current
        if let bin = toolchainBin {
            return bin + ":" + base
        }
        return base
    }

    static func scan() -> [Prereq] {
        var items: [Prereq] = []
        let machine = hardwareMachine()
        items.append(Prereq(
            id: "arch",
            title: "Apple Silicon",
            ok: machine == "arm64",
            detail: machine == "arm64" ? "arm64" : "This build is arm64. This machine reports \(machine)."
        ))

        let clang = which("clang")
        items.append(Prereq(
            id: "clt",
            title: "Xcode Command Line Tools",
            ok: clang != nil,
            detail: clang ?? "Run xcode-select --install, then open this again."
        ))

        if toolchainBin != nil {
            let git = which("git")
            items.append(Prereq(id: "git", title: "git", ok: true, detail: git ?? "Included with this copy."))
        } else {
            items.append(tool("git", title: "git", hint: "Comes with the Command Line Tools. The port patch and the SDL download both use it."))
        }
        items.append(cmake())
        items.append(tool("ninja", title: "ninja", hint: toolchainBin != nil ? "This copy is missing ninja." : "brew install ninja"))
        items.append(tool(
            "python3",
            title: "python3",
            hint: toolchainBin != nil
                ? "Used to read an Xbox disc image. It comes with the Command Line Tools."
                : "Used to read an Xbox disc image. macOS or Homebrew both work."
        ))
        items.append(vulkan())
        items.append(glslang())
        items.append(libusb())
        return items
    }

    static func blockers(_ items: [Prereq]) -> [Prereq] {
        items.filter { !$0.ok }
    }

    private static func tool(_ name: String, title: String, hint: String) -> Prereq {
        if let path = which(name) {
            return Prereq(id: name, title: title, ok: true, detail: path)
        }
        return Prereq(id: name, title: title, ok: false, detail: hint)
    }

    private static func cmake() -> Prereq {
        guard let path = which("cmake") else {
            let hint = toolchainBin != nil ? "This copy is missing cmake." : "brew install cmake"
            return Prereq(id: "cmake", title: "cmake 3.25 or newer", ok: false, detail: hint)
        }
        let text = run(path, ["--version"])
        let version = text.split(separator: "\n").first.flatMap { line -> String? in
            let parts = line.split(separator: " ")
            return parts.last.map(String.init)
        } ?? ""
        let ok = versionAtLeast(version, major: 3, minor: 25)
        return Prereq(
            id: "cmake",
            title: "cmake 3.25 or newer",
            ok: ok,
            detail: ok ? "\(path) (\(version))" : "Found \(version.isEmpty ? "an unknown version" : version). Need 3.25 or newer."
        )
    }

    private static func vulkan() -> Prereq {
        var candidates: [String] = []
        if let root = bundleToolchain?.path {
            candidates.append(root + "/prefix/etc/vulkan/icd.d/MoltenVK_icd.json")
        }
        candidates.append("/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json")
        candidates.append("/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json")
        if let sdk = ProcessInfo.processInfo.environment["VULKAN_SDK"], !sdk.isEmpty {
            candidates.append(sdk + "/share/vulkan/icd.d/MoltenVK_icd.json")
            candidates.append(sdk + "/etc/vulkan/icd.d/MoltenVK_icd.json")
        }
        if let found = candidates.first(where: { FileManager.default.fileExists(atPath: $0) }) {
            return Prereq(id: "vulkan", title: "MoltenVK", ok: true, detail: found)
        }
        return Prereq(
            id: "vulkan",
            title: "MoltenVK",
            ok: false,
            detail: toolchainBin != nil
                ? "This copy is missing MoltenVK."
                : "brew install molten-vk vulkan-loader vulkan-headers. A LunarG Vulkan SDK works too, with VULKAN_SDK set."
        )
    }

    private static func libusb() -> Prereq {
        var candidates: [String] = []
        if let root = bundleToolchain?.path {
            candidates.append(root + "/prefix/lib/libusb-1.0.a")
        }
        candidates.append("/opt/homebrew/lib/libusb-1.0.dylib")
        candidates.append("/usr/local/lib/libusb-1.0.dylib")
        if let found = candidates.first(where: { FileManager.default.fileExists(atPath: $0) }) {
            return Prereq(id: "libusb", title: "libusb", ok: true, detail: found)
        }
        return Prereq(
            id: "libusb",
            title: "libusb",
            ok: false,
            detail: toolchainBin != nil
                ? "This copy is missing libusb."
                : "brew install libusb. A wired Xbox pad is opened through it. No kernel extension."
        )
    }

    private static func glslangLibs(at prefix: String) -> Bool {
        let header = prefix + "/include/glslang/Public/ShaderLang.h"
        guard FileManager.default.fileExists(atPath: header) else { return false }
        let names = ["libglslang", "libSPIRV", "libglslang-default-resource-limits"]
        return names.allSatisfy { name in
            let lib = prefix + "/lib/" + name
            return FileManager.default.fileExists(atPath: lib + ".a")
                || FileManager.default.fileExists(atPath: lib + ".dylib")
        }
    }

    private static func glslang() -> Prereq {
        if let prefix = bundleToolchain?.appendingPathComponent("prefix").path, glslangLibs(at: prefix) {
            return Prereq(id: "glslang", title: "glslang", ok: true, detail: prefix)
        }
        for prefix in ["/opt/homebrew", "/usr/local"] where glslangLibs(at: prefix) {
            return Prereq(id: "glslang", title: "glslang", ok: true, detail: prefix)
        }
        return Prereq(
            id: "glslang",
            title: "glslang",
            ok: false,
            detail: toolchainBin != nil
                ? "This copy is missing glslang."
                : "brew install glslang. Shaders compile with it. DXC from a Vulkan SDK is optional."
        )
    }

    static func which(_ name: String) -> String? {
        let text = run("/usr/bin/which", [name]).trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text.contains("not found") { return nil }
        return text.split(separator: "\n").first.map(String.init)
    }

    static func run(_ launch: String, _ args: [String]) -> String {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: launch)
        process.arguments = args
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = buildPath
        process.environment = env
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = Pipe()
        do { try process.run() } catch { return "" }
        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8) ?? ""
    }

    private static func versionAtLeast(_ version: String, major: Int, minor: Int) -> Bool {
        let parts = version.split(separator: ".").compactMap { Int($0) }
        guard parts.count >= 2 else { return false }
        if parts[0] != major { return parts[0] > major }
        return parts[1] >= minor
    }

    private static func hardwareMachine() -> String {
        var sys = utsname()
        uname(&sys)
        return withUnsafePointer(to: &sys.machine) {
            $0.withMemoryRebound(to: CChar.self, capacity: 1) { String(cString: $0) }
        }
    }
}
