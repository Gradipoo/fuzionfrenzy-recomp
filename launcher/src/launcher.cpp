#include "launcher.h"

#include "kernel/crypto.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace rh {

namespace {

std::string Trim(std::string text) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::vector<std::string> Split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, sep)) out.push_back(part);
    return out;
}

std::string Lower(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

#ifdef _WIN32
std::wstring Wide(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end())).wstring();
}

std::wstring Quote(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

void SetEnv(const char* name, const char* value) {
    SetEnvironmentVariableW(Wide(name).c_str(), value ? Wide(value).c_str() : nullptr);
}
#endif

fs::path WhichOnPath(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    const char sep = ';';
    std::vector<std::string> names = {name};
    if (name.find('.') == std::string::npos) {
        names.push_back(name + ".exe");
        names.push_back(name + ".cmd");
        names.push_back(name + ".bat");
    }
#else
    const char sep = ':';
    std::vector<std::string> names = {name};
#endif
    for (const std::string& dir : Split(path, sep)) {
        if (dir.empty()) continue;
        for (const std::string& candidate : names) {
            fs::path full = fs::path(dir) / candidate;
            std::error_code ec;
            if (fs::is_regular_file(full, ec)) return full;
        }
    }
    return {};
}

}  // namespace

std::string Utf8(const fs::path& path) {
    auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path FromUtf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

fs::path ExeDir() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (n == 0) return fs::current_path();
        if (n < buffer.size() - 1) break;
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer.data()).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return fs::current_path();
    std::error_code ec;
    fs::path resolved = fs::canonical(buffer.data(), ec);
    return (ec ? fs::path(buffer.data()) : resolved).parent_path();
#else
    std::vector<char> buffer(4096);
    for (;;) {
        ssize_t n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (n < 0) return fs::current_path();
        if (size_t(n) < buffer.size() - 1) {
            buffer[size_t(n)] = '\0';
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer.data()).parent_path();
#endif
}

fs::path DefaultSupportDir() {
#ifdef _WIN32
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        fs::path base(local);
        CoTaskMemFree(local);
        return base / "RingHost";
    }
    return fs::temp_directory_path() / "RingHost";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / "Library/Application Support/RingHost";
#else
    const char* data = std::getenv("XDG_DATA_HOME");
    if (data && *data) return fs::path(data) / "ringhost";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".local/share/ringhost";
#endif
}

std::string ExpectedSha1() { return kExpectedSha1; }

std::string Sha1Hex(const void* data, size_t len) {
    std::array<uint8_t, 92> ctx{};
    crypto::ShaInit(ctx.data());
    crypto::ShaUpdate(ctx.data(), static_cast<const uint8_t*>(data), len);
    uint8_t digest[20];
    crypto::ShaFinal(ctx.data(), digest);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (uint8_t byte : digest) {
        out.push_back(hex[byte >> 4]);
        out.push_back(hex[byte & 15]);
    }
    return out;
}

bool Sha1File(const fs::path& path, std::string& hex) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::array<uint8_t, 92> ctx{};
    crypto::ShaInit(ctx.data());
    std::array<char, 1 << 16> buffer{};
    uint64_t total = 0;
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        std::streamsize n = in.gcount();
        if (n > 0) {
            crypto::ShaUpdate(ctx.data(), reinterpret_cast<const uint8_t*>(buffer.data()), size_t(n));
            total += uint64_t(n);
        }
    }
    if (total < 4) return false;
    uint8_t digest[20];
    crypto::ShaFinal(ctx.data(), digest);
    static const char* digits = "0123456789abcdef";
    hex.clear();
    hex.reserve(40);
    for (uint8_t byte : digest) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return true;
}

namespace {

fs::path FindXbe(const fs::path& folder) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (Lower(Utf8(entry.path().filename())) == "default.xbe") return entry.path();
    }
    return {};
}

bool HasSiblingData(const fs::path& folder, const fs::path& xbe) {
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec; it.increment(ec)) {
        const fs::path& path = it->path();
        if (path == xbe) continue;
        std::string name = Lower(Utf8(path.filename()));
        if (name == ".ds_store" || name == "settings.toml") continue;
        std::string ext = Lower(Utf8(path.extension()));
        if (ext == ".iso" || ext == ".xiso") continue;
        if (it->is_regular_file(ec)) return true;
    }
    return false;
}

}  // namespace

DiscVerdict InspectDisc(const fs::path& path) {
    DiscVerdict v;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        v.title = "Nothing at that path";
        v.detail = Utf8(path);
        return v;
    }
    if (fs::is_directory(path, ec)) {
        fs::path xbe = FindXbe(path);
        if (xbe.empty()) {
            v.title = "No default.xbe in that folder";
            v.detail = "Extract the whole disc. The executable sits beside the rest of the files, not on its own.";
            return v;
        }
        std::string hex;
        if (!Sha1File(xbe, hex)) {
            v.title = "Could not read default.xbe";
            v.detail = Utf8(xbe);
            return v;
        }
        if (hex != kExpectedSha1) {
            v.title = "Different release";
            v.detail = "This port only builds the USA release (SHA-1 " + std::string(kExpectedSha1) + "). This file is " + hex + ".";
            return v;
        }
        if (!HasSiblingData(path, xbe)) {
            v.title = "Only the executable is here";
            v.detail = "The build can hash default.xbe, but the game has nothing else to load. Point at the extracted disc, not the .xbe alone.";
            return v;
        }
        v.ok = true;
        v.confirmed = true;
        v.folder = path;
        v.title = "USA release confirmed";
        v.detail = "SHA-1 " + hex + ". The rest of the disc is in this folder.";
        return v;
    }
    std::string ext = Lower(Utf8(path.extension()));
    if (ext == ".iso" || ext == ".xiso") {
        v.ok = true;
        v.image = true;
        v.file = path;
        v.title = "Disc image selected";
        v.detail = "It will be extracted next, then checked against the USA release this port supports.";
        return v;
    }
    if (Lower(Utf8(path.filename())) == "default.xbe") return InspectDisc(path.parent_path());
    v.title = "That is not a disc";
    v.detail = "Choose an .iso, or a folder that contains default.xbe and the rest of the extracted disc.";
    return v;
}

std::string Settings::Toml() const {
    std::string ip;
    for (char c : join_ip) {
        if (c == '\\' || c == '"') ip.push_back('\\');
        ip.push_back(c);
    }
    std::ostringstream out;
    out << "display_mode = \"" << display_mode << "\"\n"
        << "width = " << width << "\n"
        << "height = " << height << "\n"
        << "render_percent = " << render_percent << "\n"
        << "fps_cap = " << fps_cap << "\n"
        << "vsync = " << (vsync ? "true" : "false") << "\n"
        << "anisotropic_filtering = " << aniso << "\n"
        << "aspect_ratio = \"" << aspect << "\"\n"
        << "ultrawide = " << (ultrawide ? "true" : "false") << "\n"
        << "join_ip = \"" << ip << "\"\n"
        << "hdr = \"off\"\n"
        << "hdr_paper_white = 200\n"
        << "hdr_peak = 0\n"
        << "sound = " << (sound ? "true" : "false") << "\n"
        << "scripted_pad = " << (scripted_pad ? "true" : "false") << "\n";
    return out.str();
}

namespace {

std::string ValueOf(const std::string& value) {
    std::string text = Trim(value);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < text.size(); ++i) {
            if (text[i] == '\\' && i + 2 < text.size()) {
                ++i;
                out.push_back(text[i]);
            } else {
                out.push_back(text[i]);
            }
        }
        return out;
    }
    return text;
}

bool BoolOf(const std::string& value, bool fallback) {
    std::string text = Lower(ValueOf(value));
    if (text == "true" || text == "1" || text == "yes") return true;
    if (text == "false" || text == "0" || text == "no") return false;
    return fallback;
}

int IntOf(const std::string& value, int fallback) {
    try {
        return std::stoi(ValueOf(value));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

Settings Settings::Load(const fs::path& path) {
    Settings s;
    std::ifstream in(path);
    if (!in) return s;
    std::string line;
    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;
        if (key == "display_mode") s.display_mode = ValueOf(value);
        else if (key == "width") s.width = IntOf(value, s.width);
        else if (key == "height") s.height = IntOf(value, s.height);
        else if (key == "render_percent") s.render_percent = IntOf(value, s.render_percent);
        else if (key == "fps_cap") s.fps_cap = IntOf(value, s.fps_cap);
        else if (key == "vsync") s.vsync = BoolOf(value, s.vsync);
        else if (key == "anisotropic_filtering") s.aniso = IntOf(value, s.aniso);
        else if (key == "aspect_ratio") s.aspect = ValueOf(value);
        else if (key == "ultrawide") s.ultrawide = BoolOf(value, s.ultrawide);
        else if (key == "join_ip") s.join_ip = ValueOf(value);
        else if (key == "sound") s.sound = BoolOf(value, s.sound);
        else if (key == "scripted_pad") s.scripted_pad = BoolOf(value, s.scripted_pad);
    }
    return s;
}

void Settings::Save(const fs::path& path) const {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out << Toml();
}

namespace {

std::string RunCapture(const std::vector<std::string>& argv);

#ifdef _WIN32

int RunProcess(const std::vector<std::string>& argv, const fs::path& cwd,
               const std::vector<std::pair<std::string, std::string>>& env,
               const std::function<void(const std::string&)>& on_line) {
    if (argv.empty()) return 127;
    std::wstring command;
    for (const std::string& arg : argv) {
        if (!command.empty()) command.push_back(L' ');
        command += Quote(Wide(arg));
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return 127;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    for (const auto& e : env) SetEnv(e.first.c_str(), e.second.c_str());
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, cwd.empty() ? nullptr : cwd.wstring().c_str(), &si, &pi);
    for (const auto& e : env) SetEnv(e.first.c_str(), nullptr);
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        return 127;
    }
    std::string pending;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        pending.append(buffer, got);
        size_t pos;
        while ((pos = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (on_line && !line.empty()) on_line(line);
        }
    }
    if (on_line && !pending.empty()) on_line(pending);
    CloseHandle(read_pipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 127;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
}

#else

int RunProcess(const std::vector<std::string>& argv, const fs::path& cwd,
               const std::vector<std::pair<std::string, std::string>>& env,
               const std::function<void(const std::string&)>& on_line) {
    if (argv.empty()) return 127;
    int fds[2];
    if (pipe(fds) != 0) return 127;
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        for (const auto& e : env) setenv(e.first.c_str(), e.second.c_str(), 1);
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const std::string& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return 127;
    }
    std::string pending;
    char buffer[4096];
    ssize_t got = 0;
    while ((got = read(fds[0], buffer, sizeof(buffer))) > 0) {
        pending.append(buffer, size_t(got));
        size_t pos;
        while ((pos = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (on_line && !line.empty()) on_line(line);
        }
    }
    if (on_line && !pending.empty()) on_line(pending);
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

#endif

std::string RunCapture(const std::vector<std::string>& argv) {
    std::string out;
    RunProcess(argv, {}, {}, [&](const std::string& line) {
        out += line;
        out.push_back('\n');
    });
    return out;
}

std::vector<std::string> PythonCommand() {
    if (fs::path p = WhichOnPath("python3"); !p.empty()) return {Utf8(p)};
    if (fs::path p = WhichOnPath("python"); !p.empty()) return {Utf8(p)};
#ifdef _WIN32
    if (fs::path p = WhichOnPath("py"); !p.empty()) return {Utf8(p), "-3"};
#endif
    return {};
}

std::string CmakeVersion() {
    fs::path cmake = WhichOnPath("cmake");
    if (cmake.empty()) return {};
    std::string text = RunCapture({Utf8(cmake), "--version"});
    size_t at = text.find("version");
    if (at == std::string::npos) return {};
    std::string rest = Trim(text.substr(at + 7));
    size_t end = 0;
    while (end < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[end])) || rest[end] == '.')) ++end;
    return rest.substr(0, end);
}

bool VersionAtLeast(const std::string& version, int major, int minor) {
    int have_major = 0;
    int have_minor = 0;
    char tail = 0;
    if (std::sscanf(version.c_str(), "%d.%d%c", &have_major, &have_minor, &tail) < 2) return false;
    if (have_major != major) return have_major > major;
    return have_minor >= minor;
}

#ifdef _WIN32

fs::path FindClang() {
    if (fs::path p = WhichOnPath("clang++"); !p.empty()) return p;
    const char* program_files = std::getenv("ProgramFiles");
    if (!program_files) program_files = "C:\\Program Files";
    const char* editions[] = {"Enterprise", "Professional", "Community", "BuildTools"};
    for (const char* edition : editions) {
        fs::path p = fs::path(program_files) / "Microsoft Visual Studio/2022" / edition / "VC/Tools/Llvm/x64/bin/clang++.exe";
        if (fs::exists(p)) return p;
    }
    fs::path standalone = fs::path(program_files) / "LLVM/bin/clang++.exe";
    if (fs::exists(standalone)) return standalone;
    return {};
}

#endif

}  // namespace

std::vector<Prereq> ScanPrereqs() {
    std::vector<Prereq> out;
    auto add = [&](std::string id, std::string title, bool ok, std::string detail, bool required = true) {
        out.push_back({std::move(id), std::move(title), std::move(detail), ok, required});
    };

#ifdef _WIN32
    fs::path clang = FindClang();
    add("clang", "Visual Studio C++ Clang tools", !clang.empty(),
        clang.empty() ? "Install Visual Studio 2022 with \"Desktop development with C++\" and the \"C++ Clang tools for Windows\" component." : Utf8(clang));
#else
    fs::path clang = WhichOnPath("clang");
    fs::path clangxx = WhichOnPath("clang++");
    add("clang", "clang and clang++", !clang.empty() && !clangxx.empty(),
        clang.empty() || clangxx.empty() ? "Install clang with your package manager." : Utf8(clangxx));
#endif
    fs::path cmake = WhichOnPath("cmake");
    std::string cmake_version = CmakeVersion();
    add("cmake", "cmake 3.25 or newer", !cmake.empty() && VersionAtLeast(cmake_version, 3, 25),
        cmake.empty() ? "Install cmake." : Utf8(cmake) + " (" + cmake_version + ")");
    fs::path ninja = WhichOnPath("ninja");
    add("ninja", "ninja", !ninja.empty(), ninja.empty() ? "Install ninja." : Utf8(ninja));
    fs::path git = WhichOnPath("git");
    add("git", "git", !git.empty(),
        git.empty() ? "Install git. The build applies the port patches with it." : Utf8(git));

#ifndef _WIN32
    bool vulkan = fs::exists("/usr/include/vulkan/vulkan.h");
    if (!vulkan) {
        if (fs::path p = WhichOnPath("pkg-config"); !p.empty()) vulkan = RunProcess({Utf8(p), "--exists", "vulkan"}, {}, {}, nullptr) == 0;
    }
    add("vulkan", "Vulkan headers and loader", vulkan,
        "Install libvulkan-dev (Debian/Ubuntu), vulkan-loader-devel (Fedora), vulkan-headers and vulkan-icd-loader (Arch).");
    bool glslang = fs::exists("/usr/include/glslang/Public/ShaderLang.h");
    if (!glslang) {
        if (fs::path p = WhichOnPath("pkg-config"); !p.empty()) glslang = RunProcess({Utf8(p), "--exists", "glslang"}, {}, {}, nullptr) == 0;
    }
    add("glslang", "glslang", glslang,
        "Install glslang-dev (Debian/Ubuntu), glslang-devel (Fedora), glslang (Arch).");
    add("zlib", "zlib headers", fs::exists("/usr/include/zlib.h"),
        "Install zlib1g-dev (Debian/Ubuntu), zlib-devel (Fedora), zlib (Arch).");
#endif

    std::vector<std::string> python = PythonCommand();
    add("python", "python3", !python.empty(),
        python.empty() ? "Needed only to read a disc image (.iso). Install python3." : python[0], false);

#ifdef _WIN32
    fs::path vswhere = fs::path(std::getenv("ProgramFiles(x86)") ? std::getenv("ProgramFiles(x86)") : "C:\\Program Files (x86)")
        / "Microsoft Visual Studio/Installer/vswhere.exe";
    add("vswhere", "Visual Studio with the C++ workload", fs::exists(vswhere),
        "Install Visual Studio 2022 with \"Desktop development with C++\".");
#endif
    return out;
}

std::string InstallHint() {
#ifdef _WIN32
    return "Install Visual Studio 2022 with \"Desktop development with C++\", the \"C++ Clang tools for Windows\" and \"C++ CMake tools for Windows\" components, then install git. Reopen the launcher.";
#else
    std::string id;
    std::string like;
    std::istringstream in(ReadFile("/etc/os-release"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ID=", 0) == 0) id = Trim(line.substr(3));
        if (line.rfind("ID_LIKE=", 0) == 0) like = Trim(line.substr(8));
    }
    id = Lower(id + " " + like);
    if (id.find("debian") != std::string::npos || id.find("ubuntu") != std::string::npos)
        return "On Debian or Ubuntu:\n  sudo apt install clang cmake ninja-build git python3 libvulkan-dev glslang-dev zlib1g-dev\nThen open the launcher again.";
    if (id.find("fedora") != std::string::npos || id.find("rhel") != std::string::npos)
        return "On Fedora:\n  sudo dnf install clang cmake ninja-build git python3 vulkan-loader-devel glslang-devel zlib-devel\nThen open the launcher again.";
    if (id.find("arch") != std::string::npos)
        return "On Arch:\n  sudo pacman -S clang cmake ninja git python vulkan-headers vulkan-icd-loader glslang zlib\nThen open the launcher again.";
    return "Install clang, cmake (3.25 or newer), ninja, git, python3, the Vulkan headers and loader, glslang, and zlib development files, then open the launcher again.";
#endif
}

namespace {

fs::path GameBinary(const fs::path& src) {
#ifdef _WIN32
    const char* paths[] = {
        "out/build/release/FuzionFrenzy.exe",
        "out/build/release/Release/FuzionFrenzy.exe",
        "out/build/windows-cross/FuzionFrenzy.exe",
    };
    for (const char* rel : paths) {
        fs::path p = src / rel;
        if (fs::exists(p)) return p;
    }
    return src / paths[0];
#else
    return src / "out/build/linux-release/FuzionFrenzy";
#endif
}

bool CopyTree(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec; it.increment(ec)) {
        fs::path rel = fs::relative(it->path(), from, ec);
        if (ec) break;
        std::string name = Utf8(rel);
        if (name == ".git" || name.rfind(".git/", 0) == 0 || name == "out" || name.rfind("out/", 0) == 0 ||
            name == "generated" || name.rfind("generated/", 0) == 0 || name == "assets" || name.rfind("assets/", 0) == 0 ||
            name == "det-logs" || name.rfind("det-logs/", 0) == 0 || name == "re" || name.rfind("re/", 0) == 0 ||
            name == "macos/launcher/dist" || name.rfind("macos/launcher/dist/", 0) == 0)
            continue;
        fs::path dst = to / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(dst, ec);
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(dst.parent_path(), ec);
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) break;
    }
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

bool LinkAssets(const fs::path& link, const fs::path& target) {
    std::error_code ec;
    fs::remove_all(link, ec);
    fs::remove(link, ec);
#ifdef _WIN32
    std::string command = "mklink /J \"" + Utf8(link) + "\" \"" + Utf8(target) + "\"";
    return RunProcess({"cmd.exe", "/c", command}, {}, {}, nullptr) == 0;
#else
    fs::create_symlink(target, link, ec);
    return !ec;
#endif
}

bool LaunchGame(const fs::path& binary, const Job& job, std::string& pid) {
    fs::path game_dir = binary.parent_path();
    fs::path log_path = job.support / "launch.log";
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(log_path.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    SetEnv("XBE_MUTE", job.mute ? "1" : nullptr);
    SetEnv("FUZION_AUTOSTART", job.autostart ? "1" : nullptr);
    SetEnv("XBE_PERF", job.perf ? "1" : nullptr);
    std::wstring command = Quote(binary.wstring());
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(binary.wstring().c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                             nullptr, game_dir.wstring().c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    SetEnv("XBE_MUTE", nullptr);
    SetEnv("FUZION_AUTOSTART", nullptr);
    SetEnv("XBE_PERF", nullptr);
    if (!ok) return false;
    pid = std::to_string(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    pid_t child = fork();
    if (child == 0) {
        if (chdir(game_dir.c_str()) != 0) _exit(127);
        int fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        if (job.mute) setenv("XBE_MUTE", "1", 1);
        if (job.autostart) setenv("FUZION_AUTOSTART", "1", 1);
        if (job.perf) setenv("XBE_PERF", "1", 1);
        execl(binary.c_str(), binary.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    if (child < 0) return false;
    pid = std::to_string(child);
    return true;
#endif
}

}  // namespace

Pipeline::~Pipeline() {
    if (worker_.joinable()) worker_.join();
}

bool Pipeline::Start(const Job& job) {
    std::lock_guard<std::mutex> lock(mu_);
    if (busy_) return false;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    progress_ = Progress{};
    progress_.busy = true;
    progress_.stage = Stage::Checking;
    progress_.text = "Checking tools";
    log_.clear();
    worker_ = std::thread([this, job] { Run(job); });
    return true;
}

bool Pipeline::Busy() const {
    std::lock_guard<std::mutex> lock(mu_);
    return busy_;
}

Progress Pipeline::Snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return progress_;
}

std::vector<std::string> Pipeline::DrainLog() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

void Pipeline::Log(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    if (log_.size() < 4000) log_.push_back(line);
}

void Pipeline::SetStage(Stage stage, const std::string& text) {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.stage = stage;
    progress_.text = text;
    progress_.fraction = -1;
}

void Pipeline::SetFraction(double fraction) {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.fraction = fraction;
}

void Pipeline::Fail(const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    progress_.stage = Stage::Failed;
    progress_.text = "Failed";
    progress_.error = message;
    progress_.busy = false;
}

void Pipeline::Run(Job job) {
    try {
        SetStage(Stage::Checking, "Checking tools");
        std::vector<Prereq> prereqs = ScanPrereqs();
        std::string missing;
        for (const Prereq& p : prereqs) {
            if (!p.ok && p.required) missing += p.title + ": " + p.detail + "\n";
        }
        if (!missing.empty()) {
            Fail("Missing tools.\n" + missing + "\n" + InstallHint());
            std::lock_guard<std::mutex> lock(mu_);
            busy_ = false;
            return;
        }

        if (!fs::exists(job.port_source / "build.sh") && !fs::exists(job.port_source / "build.bat")) {
            Fail("This copy of the launcher is missing its port sources.");
            std::lock_guard<std::mutex> lock(mu_);
            busy_ = false;
            return;
        }

        std::error_code ec;
        fs::create_directories(job.support, ec);
        fs::path src = job.support / "src";
        fs::path stamp = src / ".port-version";
        std::string have = Trim(ReadFile(stamp));
#ifdef _WIN32
        const char* build_script = "build.bat";
#else
        const char* build_script = "build.sh";
#endif
        bool ready = have == job.version && fs::exists(src / build_script);
        if (job.force_rebuild || !ready) {
            SetStage(Stage::Copying, "Copying the port sources");
            fs::remove_all(src, ec);
            std::string error;
            if (!CopyTree(job.port_source, src, error)) {
                Fail("Could not copy the port sources: " + error);
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
#ifndef _WIN32
            fs::permissions(src / "build.sh", fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                                  fs::perms::others_read | fs::perms::others_exec,
                            fs::perm_options::replace, ec);
#endif
            std::ofstream(stamp, std::ios::trunc) << job.version << "\n";
        }

        fs::path folder;
        if (job.disc_is_image) {
            SetStage(Stage::Extracting, "Extracting the disc");
            fs::path dest = job.support / "disc";
            fs::remove_all(dest, ec);
            fs::create_directories(dest, ec);
            std::vector<std::string> python = PythonCommand();
            if (python.empty()) {
                Fail("Python 3 is needed to read a disc image. Install python3 and try again.");
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
            std::vector<std::string> args = python;
            args.insert(args.end(), {"tools/xiso_extract.py", "extract", Utf8(job.disc), Utf8(dest)});
            int code = RunProcess(args, src, {}, [this](const std::string& line) { Log(line); });
            if (code != 0) {
                Fail("Could not read that disc image. It needs to be an Xbox XDVDFS .iso.");
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
            DiscVerdict verdict = InspectDisc(dest);
            if (!verdict.ok || !verdict.confirmed) {
                Fail(verdict.title + "\n" + verdict.detail);
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
            Log(verdict.title);
            Log(verdict.detail);
            folder = dest;
        } else {
            DiscVerdict verdict = InspectDisc(job.disc);
            if (!verdict.ok || !verdict.confirmed) {
                Fail(verdict.title + "\n" + verdict.detail);
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
            folder = verdict.folder;
        }

        if (!LinkAssets(src / "assets", folder)) {
            Fail("Could not link the disc into the build folder.");
            std::lock_guard<std::mutex> lock(mu_);
            busy_ = false;
            return;
        }

        fs::path binary = GameBinary(src);
        fs::path built_stamp = job.support / "built-version";
        std::string built = Trim(ReadFile(built_stamp));
        bool need_build = job.force_rebuild || built != job.version || !fs::exists(binary);
        if (need_build) {
            if (job.force_rebuild || built != job.version) {
                fs::remove_all(src / "generated", ec);
                fs::remove_all(src / "out", ec);
            }
            SetStage(Stage::Building, "Translating and compiling");
            Log("First build downloads SDL and compiles the game from your disc. It takes a while.");
            auto on_line = [this](const std::string& line) {
                Log(line);
                if (line.find("Recompiling") != std::string::npos) SetStage(Stage::Building, "Translating the executable");
                else if (line.rfind("[1/3]", 0) == 0) SetStage(Stage::Building, "Checking the compiler");
                else if (line.rfind("[2/3]", 0) == 0) SetStage(Stage::Building, "Reading the executable");
                else if (line.rfind("[3/3]", 0) == 0) SetStage(Stage::Building, "Compiling");
                if (line.size() > 3 && line.front() == '[') {
                    int done = 0;
                    int total = 0;
                    if (std::sscanf(line.c_str(), "[%d/%d]", &done, &total) == 2 && total > 0 && done >= 0 && done <= total) {
                        SetFraction(double(done) / double(total));
                    }
                }
            };
            int code = 0;
#ifdef _WIN32
            code = RunProcess({"cmd.exe", "/c", "build.bat"}, src, {{"CI", "1"}}, on_line);
#else
#ifdef __APPLE__
            code = RunProcess({"bash", "build.sh", "macos-release"}, src, {}, on_line);
#else
            code = RunProcess({"bash", "build.sh", "linux-release"}, src, {}, on_line);
#endif
#endif
            if (code != 0 || !fs::exists(binary)) {
                Fail("The compile stopped. The last lines above are the reason. A missing tool, or a failed download of SDL, is the usual cause.");
                std::lock_guard<std::mutex> lock(mu_);
                busy_ = false;
                return;
            }
            std::ofstream(built_stamp, std::ios::trunc) << job.version << "\n";
        } else {
            Log("Using the build already on this machine.");
        }

        SetStage(Stage::Launching, "Writing settings");
        fs::create_directories(binary.parent_path(), ec);
        std::ofstream(binary.parent_path() / "settings.toml", std::ios::trunc) << job.settings_toml;
        SetStage(Stage::Launching, "Launching");
        std::string pid;
        if (!LaunchGame(binary, job, pid)) {
            Fail("Could not start the game.");
            std::lock_guard<std::mutex> lock(mu_);
            busy_ = false;
            return;
        }
        Log("Game pid " + pid);
        {
            std::lock_guard<std::mutex> lock(mu_);
            progress_.stage = Stage::Running;
            progress_.text = "Running";
            progress_.fraction = 1;
            progress_.running = true;
            progress_.pid = pid;
            progress_.busy = false;
            busy_ = false;
        }
    } catch (const std::exception& e) {
        Fail(e.what());
        std::lock_guard<std::mutex> lock(mu_);
        busy_ = false;
    }
}

}  // namespace rh
