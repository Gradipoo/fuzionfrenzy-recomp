// Play online must replace this process with the same binary plus --lockstep.
#include "common/play_online.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

struct Cap {
    bool called = false;
    std::string exe;
    std::vector<std::string> args;
};

static bool Capture(const char* exe, char* const* argv, void* user) {
    auto* cap = static_cast<Cap*>(user);
    cap->called = true;
    cap->exe = exe ? exe : "";
    for (int i = 0; argv && argv[i]; i++) cap->args.emplace_back(argv[i]);
    return true;
}

static bool Has(const Cap& cap, const char* text) {
    for (const std::string& a : cap.args)
        if (a == text) return true;
    return false;
}

int main() {
    char arg0[] = "RingHost.exe";
    char arg1[] = "--instance";
    char arg2[] = "2";
    char* argv[] = {arg0, arg1, arg2, nullptr};
    Cap cap;
    bool ok = play::RestartOnline("ABCD23", nullptr, 3, argv, "C:\\Game\\RingHost.exe", Capture, &cap);
    Expect(ok, "play online restarts");
    Expect(cap.called, "spawn was called");
    Expect(cap.exe == "C:\\Game\\RingHost.exe", "same binary");
    Expect(Has(cap, "RingHost.exe") && Has(cap, "--instance") && Has(cap, "2"), "keeps the current arguments");
    Expect(Has(cap, "--lockstep"), "adds --lockstep");

    cap = {};
    char flag[] = "--lockstep";
    char* with[] = {arg0, flag, nullptr};
    ok = play::RestartOnline(nullptr, "127.0.0.1:9999", 2, with, "C:\\Game\\RingHost.exe", Capture, &cap);
    int locks = 0;
    for (const std::string& a : cap.args)
        if (a == "--lockstep") locks++;
    Expect(ok, "restart with lockstep already set");
    Expect(locks == 1, "does not repeat --lockstep");

    if (g_fails) {
        std::fprintf(stderr, "FAIL play_online %d\n", g_fails);
        return 1;
    }
    std::printf("PASS play_online\n");
    return 0;
}
