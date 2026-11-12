#include "common/play_online.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace play {

bool RestartOnline(const char* code, const char* relay, int argc, char** argv, const char* exe, SpawnFn spawn,
                   void* user) {
    // Same environment as the Mac host: the child inherits it from this process.
#if defined(_WIN32)
    SetEnvironmentVariableA("FUZION_RELAY_CODE", code && code[0] ? code : nullptr);
    if (relay && relay[0]) SetEnvironmentVariableA("FUZION_RELAY", relay);
#else
    if (code && code[0]) setenv("FUZION_RELAY_CODE", code, 1);
    else unsetenv("FUZION_RELAY_CODE");
    if (relay && relay[0]) setenv("FUZION_RELAY", relay, 1);
#endif
    if (!spawn || !exe || !exe[0] || !argv) return false;

    static char flag[] = "--lockstep";
    std::vector<char*> args;
    args.reserve(static_cast<size_t>(argc) + 2);
    bool has = false;
    for (int i = 0; i < argc; i++) {
        if (argv[i] && std::strcmp(argv[i], "--lockstep") == 0) has = true;
        args.push_back(argv[i]);
    }
    if (!has) args.push_back(flag);
    args.push_back(nullptr);
    return spawn(exe, args.data(), user);
}

}  // namespace play
