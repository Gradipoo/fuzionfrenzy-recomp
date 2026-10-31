// Short fixed-step replays from local snapshots. Prints skipped and exits 0
// when the disc image or the snapshots are not on this machine.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

struct Scene {
    const char* name;
    uint64_t frame;
    uint64_t length;
};

static const Scene kScenes[] = {
    {"title", 200, 120},
    {"board", 1200, 120},
    {"twisted", 2200, 120},
    {"sumo", 3400, 120},
    {"jetstream", 5000, 120},
};

static bool Exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

static fs::path FindUp(const fs::path& rel) {
    fs::path dir = fs::current_path();
    for (int i = 0; i < 6; i++) {
        fs::path c = dir / rel;
        if (Exists(c)) return c;
        if (!dir.has_parent_path() || dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

static void Skip(const char* why) {
    std::printf("test_scenes: %s, skipped\n", why);
}

#if !defined(_WIN32)
static int RunOne(const fs::path& exe, const Scene& scene, const fs::path& snap, const fs::path& expect, const fs::path& pads,
                  const fs::path& hdd) {
    std::vector<std::string> store;
    auto add = [&](const std::string& s) { store.push_back(s); };
    for (char** e = environ; e && *e; ++e) {
        std::string s = *e;
        auto drop = s.rfind("FUZION_", 0) == 0 || s.rfind("XBE_MUTE=", 0) == 0;
        if (!drop) add(s);
    }
    add("XBE_MUTE=1");
    add("FUZION_FIXED_STEP=1");
    add("FUZION_SEED=1");
    add("FUZION_NO_PACE=1");
    add("FUZION_REPLAY_INPUT=" + pads.string());
    add("FUZION_STATE_EXPECT=" + expect.string());
    add("FUZION_LOAD_SNAPSHOT=" + snap.string());
    add("FUZION_FRAMES=" + std::to_string(scene.frame + scene.length));
    if (!hdd.empty()) add("FUZION_HDD_SNAPSHOT=" + hdd.string());
    std::vector<char*> envp;
    for (auto& s : store) envp.push_back(s.data());
    envp.push_back(nullptr);
    std::string file = exe.string();
    char* argv[] = {file.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
#if defined(__APPLE__)
    posix_spawn_file_actions_addchdir(&actions, exe.parent_path().c_str());
#else
    posix_spawn_file_actions_addchdir_np(&actions, exe.parent_path().c_str());
#endif
    pid_t pid = 0;
    auto t0 = std::chrono::steady_clock::now();
    int rc = posix_spawn(&pid, file.c_str(), &actions, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        std::fprintf(stderr, "test_scenes: spawn %s failed (%d)\n", scene.name, rc);
        return 1;
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 1;
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        std::fprintf(stderr, "test_scenes: %s exit %d after %.2fs\n", scene.name, st, sec);
        return 1;
    }
    std::printf("test_scenes: %s frames %llu+%llu PASS %.2fs\n", scene.name, (unsigned long long)scene.frame,
                (unsigned long long)scene.length, sec);
    return 0;
}
#endif

int main(int argc, char** argv) {
    fs::path xbe = std::getenv("FUZION_XBE") ? fs::path(std::getenv("FUZION_XBE")) : FindUp("assets/default.xbe");
    if (!Exists(xbe)) {
        Skip("game files are absent");
        return 0;
    }
    fs::path root = xbe.parent_path().parent_path();
    fs::path snaps = std::getenv("FUZION_SNAPSHOT_DIR") ? fs::path(std::getenv("FUZION_SNAPSHOT_DIR")) : root / "det-snaps";
    fs::path expect = FindUp("det-logs/gameplay2.detlog");
    fs::path pads = FindUp("det-logs/gameplay2.pads");
    bool missing = !Exists(expect) || !Exists(pads);
    for (const Scene& s : kScenes) {
        char name[64];
        std::snprintf(name, sizeof name, "frame_%llu.ffsnap", (unsigned long long)s.frame);
        if (!Exists(snaps / name)) missing = true;
    }
    if (missing) {
        Skip("snapshots or the gameplay2 log are absent");
        return 0;
    }
    fs::path exe;
    if (argc > 1 && argv[1] && argv[1][0]) exe = argv[1];
    else if (const char* e = std::getenv("FUZION_SCENE_BIN")) exe = e;
    if (exe.empty() || !Exists(exe)) {
        Skip("game executable was not passed");
        return 0;
    }
    // posix_spawn resolves the path after addchdir, so a relative path misses.
    exe = fs::absolute(exe);
#if defined(_WIN32)
    Skip("scene resume is built for macOS and Linux");
    return 0;
#else
    fs::path hdd = std::getenv("FUZION_HDD_SNAPSHOT") ? fs::path(std::getenv("FUZION_HDD_SNAPSHOT")) : fs::path();
    int fails = 0;
    for (const Scene& s : kScenes) {
        char name[64];
        std::snprintf(name, sizeof name, "frame_%llu.ffsnap", (unsigned long long)s.frame);
        fails += RunOne(exe, s, snaps / name, expect, pads, hdd);
    }
    if (fails) {
        std::fprintf(stderr, "test_scenes: %d failed\n", fails);
        return 1;
    }
    std::printf("test_scenes: PASS\n");
    return 0;
#endif
}
