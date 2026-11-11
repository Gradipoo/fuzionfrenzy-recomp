#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rh {

namespace fs = std::filesystem;

// The exact USA release this port builds.
inline constexpr const char* kExpectedSha1 = "c6fb6082900532cde5fa8dbb7de8dfa202f0fdad";

struct Settings {
    std::string display_mode = "borderless";
    int width = 0;
    int height = 0;
    int render_percent = 100;
    int fps_cap = 60;
    bool vsync = true;
    int aniso = 0;
    std::string aspect = "auto";
    bool ultrawide = true;
    std::string join_ip;
    bool sound = true;
    bool scripted_pad = false;

    std::string Toml() const;
    static Settings Load(const fs::path& path);
    void Save(const fs::path& path) const;
};

struct DiscVerdict {
    bool ok = false;
    bool confirmed = false;
    bool image = false;
    fs::path folder;  // set when confirmed
    fs::path file;    // set when an image was chosen
    std::string title;
    std::string detail;
};

struct Prereq {
    std::string id;
    std::string title;
    std::string detail;
    bool ok = false;
    bool required = true;
};

struct Job {
    fs::path disc;
    bool disc_is_image = false;
    fs::path support;
    fs::path port_source;
    std::string version;
    std::string settings_toml;
    bool mute = false;
    bool autostart = false;
    bool perf = false;
    bool force_rebuild = false;
};

enum class Stage {
    Idle,
    Checking,
    Copying,
    Extracting,
    Building,
    Launching,
    Running,
    Failed,
};

struct Progress {
    Stage stage = Stage::Idle;
    std::string text = "Ready";
    double fraction = -1;  // negative: indeterminate
    std::string error;
    bool busy = false;
    bool running = false;
    std::string pid;
};

std::string Utf8(const fs::path& path);
fs::path FromUtf8(const std::string& text);
fs::path ExeDir();
fs::path DefaultSupportDir();
std::string ExpectedSha1();
std::string Sha1Hex(const void* data, size_t len);
bool Sha1File(const fs::path& path, std::string& hex);

DiscVerdict InspectDisc(const fs::path& path);
std::vector<Prereq> ScanPrereqs();
std::string InstallHint();

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool Start(const Job& job);
    bool Busy() const;
    Progress Snapshot() const;
    std::vector<std::string> DrainLog();

private:
    void Run(Job job);
    void Log(const std::string& line);
    void SetStage(Stage stage, const std::string& text);
    void SetFraction(double fraction);
    void Fail(const std::string& message);

    mutable std::mutex mu_;
    Progress progress_;
    std::vector<std::string> log_;
    std::thread worker_;
    bool busy_ = false;
};

}  // namespace rh
