#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <cstdlib>
#include <cstring>

#include "app/app.h"
#include "common/input_kbm.h"
#include "common/overlay.h"
#include "core/determinism.h"
#include "core/input.h"
#include "core/log.h"
#include <cstdio>
#include <string>

namespace {

// Opt-in self-test (FUZION_AUTOSTART): a scripted pad on port 0. For the first
// FUZION_AUTOSTART_PLAY_MS (default 75 s, about how long the intro movie and the
// menus take) it repeats a 10 s cycle from ~2 s after the first poll: Start
// (wButtons 0x0010) held 0-0.5 s, D-pad Up (wButtons 0x0001) held 3-3.5 s, then A
// (analog[0] = 0xFF, the Xbox pressure byte, not a wButtons bit) held 5-5.5 s, all
// released between. A title commonly ignores presses through its intro movies and
// for a while after them, so the cycle repeats: a Start lands whenever the title is
// responsive and an A whenever a menu is.
// After that it plays: the left stick walks a slow circle and A is tapped every 2 s,
// and Start is never pressed again. Pressing Start on the cycle forever pauses and
// unpauses the minigame every 10 s, which both stops the game playing and makes a
// frame rate measured over that window the pause screen's, not the arena's.
// Only ever ORs bits/bytes in, never clears a real press. Unset: no-op, pad untouched.
// Step mode (FUZION_STEP=<path>): each new token written to that file is a burst
// of timed pulses generated here, on the game's own poll clock: NAME or NAMExN
// (START, A, B, X, Y, BACK, UP, DOWN, LEFT, RIGHT, LSUP, LSDOWN, LSLEFT,
// LSRIGHT, BLACK, WHITE, LT, RT, LTHUMB, RTHUMB; anything else = none), so a
// driver can do press -> screenshot -> decide -> press with an exact press
// count. Takes precedence over the cycle below.
bool StepPad(int port, input::Pad& pad) {
    static const char* path = std::getenv("FUZION_STEP");
    if (!path || !path[0] || port != 0) return false;
    using namespace std::chrono;
    // R10: the pad hook runs outside input::g_lock and the title may poll from more than one guest
    // thread; the token and its two timestamps change together, so every poll holds this lock
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    static std::string held;
    static int64_t last_read = 0, held_since = 0;
    const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    if (now - last_read >= 50) {
        last_read = now;
        std::string token;
        if (FILE* f = std::fopen(path, "r")) {
            char buf[32] = {};
            if (std::fgets(buf, sizeof buf, f)) token = buf;
            std::fclose(f);
        }
        while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ')) token.pop_back();
        if (token != held) {
            held = token;
            held_since = now;
            LOGI("fuzion step: holding '%s' (port 0)", held.c_str());
        }
    }
    if (held.empty() || held == "NONE") return true;
    // Pulse train for the current token: NAMExN = N pulses (default 1), each
    // 130 ms on / 130 ms off for buttons, 160/160 for stick deflections (they
    // are polled against a threshold). After the burst the input rests, however
    // long the driver leaves the token in the file.
    std::string name = held;
    int count = 1;
    if (const size_t x = held.rfind('x'); x != std::string::npos && x + 1 < held.size() &&
        held.find_first_not_of("0123456789", x + 1) == std::string::npos) {
        count = std::atoi(held.c_str() + x + 1);
        name = held.substr(0, x);
        if (count < 1) count = 1;
        if (count > 40) count = 40;
    }
    const bool stick = name.starts_with("LS");
    const int64_t half = stick ? 160 : 130;
    const int64_t phase = (now - held_since) / half;
    if (phase >= 2 * count || (phase & 1)) return true;
    pad.connected = true;
    if (name == "START") pad.buttons = uint16_t(pad.buttons | 0x0010);
    else if (name == "BACK") pad.buttons = uint16_t(pad.buttons | 0x0020);
    else if (name == "UP") pad.buttons = uint16_t(pad.buttons | 0x0001);
    else if (name == "DOWN") pad.buttons = uint16_t(pad.buttons | 0x0002);
    else if (name == "LEFT") pad.buttons = uint16_t(pad.buttons | 0x0004);
    else if (name == "RIGHT") pad.buttons = uint16_t(pad.buttons | 0x0008);
    else if (name == "A") pad.analog[0] = 0xFF;
    else if (name == "B") pad.analog[1] = 0xFF;
    else if (name == "X") pad.analog[2] = 0xFF;
    else if (name == "Y") pad.analog[3] = 0xFF;
    else if (name == "LSUP") pad.ly = 30000;
    else if (name == "LSDOWN") pad.ly = -30000;
    else if (name == "LSLEFT") pad.lx = -30000;
    else if (name == "LSRIGHT") pad.lx = 30000;
    else if (name == "BLACK") pad.analog[4] = 0xFF;
    else if (name == "WHITE") pad.analog[5] = 0xFF;
    else if (name == "LT") pad.analog[6] = 0xFF;
    else if (name == "RT") pad.analog[7] = 0xFF;
    else if (name == "LTHUMB") pad.buttons = uint16_t(pad.buttons | 0x0040);
    else if (name == "RTHUMB") pad.buttons = uint16_t(pad.buttons | 0x0080);
    return true;
}

void AutostartPad(int port, input::Pad& pad) {
    if (StepPad(port, pad)) return;
    static const bool enabled = [] {
        const char* v = std::getenv("FUZION_AUTOSTART");
        return v && v[0] && std::strcmp(v, "0") != 0;
    }();
    if (!enabled || port != 0) return;
    using namespace std::chrono;
    static std::atomic<int64_t> first_ms{0};
    static std::atomic<bool> start_down{false}, a_down{false}, up_down{false};
    const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    int64_t expected = 0;
    if (first_ms.compare_exchange_strong(expected, now)) {
        LOGI("fuzion autostart: enabled, first poll on port 0; scripted Start/A cycle from ~2 s");
        return;
    }
    const int64_t elapsed = now - first_ms.load(std::memory_order_relaxed);
    static const int64_t play_after = [] {
        const char* v = std::getenv("FUZION_AUTOSTART_PLAY_MS");
        char* e = nullptr;
        int64_t ms = v && v[0] ? std::strtoll(v, &e, 10) : 0;
        return ms > 0 && e && !*e ? ms : 75000;
    }();
    if (elapsed >= play_after) {
        // In the arena: walk and hit A, and never press Start again - that is the pause button.
        static std::atomic<bool> said{false};
        if (!said.exchange(true)) LOGI("fuzion autostart: playing (stick and A only, no more Start)");
        const double t = double(elapsed - play_after) / 1000.0;
        pad.connected = true;
        const int16_t ax = int16_t(24000 * std::cos(t * 0.7)), ay = int16_t(24000 * std::sin(t * 0.7));
        if (pad.lx == 0) pad.lx = ax;
        if (pad.ly == 0) pad.ly = ay;
        if ((elapsed - play_after) % 2000 < 250 && pad.analog[0] < 0xFF) pad.analog[0] = 0xFF;  // Xbox A
        return;
    }
    const int64_t phase = elapsed >= 2000 ? (elapsed - 2000) % 10000 : -1;
    const bool start = phase >= 0 && phase < 500;
    const bool up = phase >= 3000 && phase < 3500;
    const bool a = phase >= 5000 && phase < 5500;
    if (start) {
        pad.connected = true;
        pad.buttons = uint16_t(pad.buttons | 0x0010);  // Xbox Start
        if (!start_down.exchange(true))
            LOGI("fuzion autostart: synthetic Start pressed (port 0, wButtons=0x%04X)", pad.buttons);
    } else if (start_down.exchange(false)) {
        LOGI("fuzion autostart: synthetic Start released (port 0)");
    }
    if (up) {
        pad.connected = true;
        pad.buttons = uint16_t(pad.buttons | 0x0001);  // Xbox D-pad Up
        if (!up_down.exchange(true)) LOGI("fuzion autostart: synthetic DPadUp pressed (port 0, wButtons=0x%04X)", pad.buttons);
    } else if (up_down.exchange(false)) {
        LOGI("fuzion autostart: synthetic DPadUp released (port 0)");
    }
    if (a) {
        pad.connected = true;
        if (pad.analog[0] < 0xFF) pad.analog[0] = 0xFF;  // Xbox A, pressure byte
        if (!a_down.exchange(true)) LOGI("fuzion autostart: synthetic A pressed (port 0, analog[0]=0xFF)");
    } else if (a_down.exchange(false)) {
        LOGI("fuzion autostart: synthetic A released (port 0)");
    }
}

// every polled pad: the scripted self-test first, then the keyboard and mouse (input_kbm.cpp).
// Both only ever add to what a real controller reported.
void PadHook(int port, input::Pad& pad) {
    AutostartPad(port, pad);
    kbm::Apply(port, pad);
}

// Inlined MSVC rand: imul 343FDh, add 269EC3h, store the seed back. This .data dword is that global.
void Loaded() { det::WatchRng(0x285630); }

}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
#if defined(__APPLE__)
    .title = L"Ring Host",
    .short_title = L"Ring Host",
    .name = L"RingHost",
    .log = "ringhost",
#else
    .title = L"Fuzion Frenzy",               // window and splash
    .short_title = L"Fuzion Frenzy",         // message boxes
    .name = L"FuzionFrenzy",                 // window classes, crash dumps, handover event
    .log = "fuzion",                         // log file prefix
#endif
    .settings = "settings.toml",             // settings file beside the exe
    .splash = L"splash",                     // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its Packages folder beside it.",
#if defined(__APPLE__)
    .images = {{"default.xbe", "RingHost"}},
#else
    .images = {{"default.xbe", "FuzionFrenzy"}},   // each recompiled xbe and its exe, for title launches
#endif
    .options = {},                                 // no game-specific settings yet
    .load = nullptr,                         // reads more from the settings file's toml::table
    .save = nullptr,                         // writes more into it
    .init = nullptr,                         // after the settings load, before the window and boot
    .loaded = &Loaded,                      // image in memory, before its entry point runs
    .window = nullptr,                       // the game window is up
#if defined(_WIN32)
    .message = &kbm::Message,                // window messages first; true takes one
#else
    .message = nullptr,                      // the SDL host calls no message hook; kbm polls SDL
#endif
    .ui = &overlay::Draw,                    // ImGui drawing each frame: F1 settings, F2 performance
    .pad = &PadHook,                         // keyboard and mouse as port 0, FUZION_AUTOSTART self-test
    .rumble = nullptr,                       // changes a rumble before it is set
    // Measured, not guessed: this title draws the whole frame and leaves no overscan band, so the
    // no-crop 0/480 is the right answer rather than an unexamined default.
    //   - Its own viewport transform, dumped from running draws (XBE_VK_DEBUG_DRAW): the XDK fold
    //     holds half-size c[58] = (320, -240, ...) about centre c[59] = (320, 240, ...), so the
    //     picture spans y 0 to 480. The 160/+-120 variants about (160,120), (480,120), (160,360) and
    //     (480,360) are the four-player split-screen quadrants of that same 640x480 frame.
    //   - Guest front buffers captured through a 200 s driven run (XBE_VK_CAPTURE_EVERY): 0 black
    //     rows top and bottom on all 18 gameplay and menu frames. The one letterboxed frame is a
    //     cutscene at 54/52 rows, about 11% a side - the game's own 16:9 bar, not the ~3% a TV's
    //     overscan band would leave, and not present every frame.
    // A title that did draw into a band would show it in both, as The Urbz does at 640x448 offset 16.
    .picture_top = 0,
    .picture_height = 480,
};
