#pragma once

namespace play {

// Replaces this process. spawn is the platform start. A null spawn cannot restart.
// exe is the binary. argv is the current command, argc entries, not necessarily null-terminated.
using SpawnFn = bool (*)(const char* exe, char* const* argv, void* user);

bool RestartOnline(const char* code, const char* relay, int argc, char** argv, const char* exe, SpawnFn spawn,
                   void* user);

}  // namespace play
