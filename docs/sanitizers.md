# Sanitizer builds

`macos-asan` is a Darwin arm64 preset. It instruments the runtime, the
vendored SDL and imgui, openlibm, and the recompiled game with AddressSanitizer
and UndefinedBehaviorSanitizer. It is off unless that preset (or
`-DXBE_SANITIZE=ON`) is chosen. A normal `./build.sh` is unchanged.

```
./build.sh macos-asan
tools/asan/autostart.sh
```

The preset is RelWithDebInfo at `-O1 -g -fno-omit-frame-pointer`, without
`NDEBUG`, so info lines stay in the log. Guest translation units append `-O1`
as well. `-O0` does not reach a minigame in a useful time. The autostart
script is muted (`XBE_MUTE=1`), minimized, and uses `--instance 5`, so it
writes `out/build/macos-asan/hdd5` and leaves `hdd/`, `hdd2`, `hdd3`, and
`hdd4` alone. `FUZION_AUTOSTART` stays unset for every other build.

`ASAN_OPTIONS` halts on the first error. `detect_leaks=0` because LeakSanitizer
is not part of Apple's ASan runtime. `UBSAN_OPTIONS` halts and prints a stack,
and reads `tools/asan/ubsan.supp`.

Frame dumps from the script (`XBE_VK_CAPTURE_EVERY`, default 300 presents) land
in `out/build/macos-asan/asan-frames/`. That directory is under `out/` and is
not committed. Neither are the logs.

## What is instrumented, and what is not a bug

The flags are directory options set before the SDK is included, so SDL, imgui,
the runtime, and openlibm are all instrumented. The game executable is linked
with the same sanitizers.

Recompiled guest sources (`generated/game/x86_recomp.*.cpp` and
`x86_func_mapping.cpp`) add three flags:

| Flag | Why |
| --- | --- |
| `-fwrapv` | Xbox signed overflow wraps. With this flag the overflow is defined, so UBSan does not report it and the compiler cannot treat it as unreachable. Guest-visible wrap stays wrap. |
| `-fno-sanitize=alignment` | `X86_MEM*` loads through `aligned(1)` types. `X86_PTR` casts to a host type the guest did not align. The runtime is still alignment-checked. |
| `-O1` | Overrides the recompiler's `-O2` on those files (the last `-O` wins) so the sanitizer build can finish a session. |

`tools/asan/ubsan.supp` starts with no rules. A rule is added only for a
report that is not a guest-visible bug, with a comment naming the function.

## Uninitialized reads

ASan reports heap and stack use-after-free, heap buffer overflow, and stack
buffer overflow. It does not report a read of an uninitialized local or an
uninitialized heap word. MemorySanitizer would, and it cannot be linked into
this game on macOS: every library the process loads, including the system libc,
has to be built with MSan. There is no such libc here. A macOS MSan preset
would false-fail or fail to link. It is not offered.

An uninitialized-use check that does not need the XBE belongs on Linux, against
the math digest, and is part of CI rather than this preset.

## Running

`tools/asan/autostart.sh [seconds]` defaults to 210. The menu script is wall
clock (`FUZION_AUTOSTART_PLAY_MS`, default 75000). After that it holds the
stick and A and does not press Start again. The log is
`out/build/macos-asan/logs/asan-autostart.log`, and the game's own log is
`out/build/macos-asan/logs/ringhost_*.log` because this preset leaves `NDEBUG`
unset. `perf:` lines are in both once `XBE_PERF=1` (the script sets it).

A non-zero exit means the process died on its own: sanitizer, assert, or a
crash. A zero exit after the duration means it was still running and was
stopped. Look in the log for `ERROR: AddressSanitizer` or `runtime error:`
before treating that as a clean session.
