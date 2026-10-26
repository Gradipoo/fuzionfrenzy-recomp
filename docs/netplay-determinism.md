# Netplay determinism harness

Lockstep input sync (Mario Party Netplay, and Halo CE's original system link) needs both machines to run the same simulation from the same inputs. This title has no System Link code, so the protocol is new. Streaming a picture and sending pads back is the fallback, and it is not built here. Nothing in this harness opens a socket or links Steam.

With every flag off, clocks, pads, the mixer, vblank, and frame pacing are the paths the game already uses.

Recorded pad files and state logs are produced by a run. They stay in `det-logs/` (gitignored). Do not commit them.

## Flags

The command line wins over the environment variable. `--name value` and `--name=value` both work.

| Flag | Environment | Effect |
| --- | --- | --- |
| `--fixed-step` | `FUZION_FIXED_STEP` | Time, vblank, timers, and the mixer advance once per present. `0` leaves the flag off. |
| `--record-input PATH` | `FUZION_RECORD_INPUT` | Append every port's latched pad for each presented frame. |
| `--replay-input PATH` | `FUZION_REPLAY_INPUT` | Feed that file. Real pads, the keyboard, and `FUZION_AUTOSTART` are ignored. Past the last frame the last sample is held. |
| `--seed N` | `FUZION_SEED` | Write `N` (`strtoul` base 0) into the rand global once, at image load. |
| `--state-log PATH` | `FUZION_STATE_LOG` | Per-present checksum log. |
| `--frames N` | `FUZION_FRAMES` | Exit 0 after N presents. |
| `--stall-ms N` | `FUZION_STALL_MS` | Sleep N milliseconds at the start of each present, before the hash. |
| `--minimized` | `FUZION_MINIMIZED` | Show the window, then minimize it. |
| `--state-dump DIR` | `FUZION_STATE_DUMP` | With the frame below, write `frame_N_region.bin`. |
| `--state-dump-frame N` | `FUZION_STATE_DUMP_FRAME` | Which present to dump. |
| `--state-dump-region NAME` | `FUZION_STATE_DUMP_REGION` | `phys_03`, `.data`, `system`, `user_0`, or a section name. Empty dumps the writable sections and the 64 physical slices. |

`--instance N` is the existing launcher flag. Instance 0 and 1 share `hdd/` next to the executable. Instance 2 and up use `hddN`. The matrix uses 2, 3, and 4 and deletes only those directories.

## What a frame is

A frame is one `D3DDevice_Present` (this XDK has no `Swap` that the title calls). The guest body of `Present` is replaced, and the only caller of `BlockUntilVerticalBlank` sat inside that body, so the title does not wait on vblank itself.

Present advances the sim tick by one, after the checksum. A guest read of QPC, RDTSC, or interrupt/system time that would return the same value as the previous read of that clock advances one tick first and then returns the new value. `KeDelayExecutionThread` and `KeStallExecutionProcessor` consume their guest duration in whole ticks and return, without sleeping.

That repeat rule is what gets past boot. `sub_0004C510` samples RDTSC, then spins on `KeQueryPerformanceCounter` until the delta reaches the performance frequency divided by two (half a second, 1,687,500 counts). A counter that only moved on present never left that spin: the first run hashed frame 0 and then sat in the wait at 100% CPU. Thirty repeated reads are exactly half a second (`30 * 56250`), so the wait exits and the TSC delta beside it stays in step with the QPC delta. Two reads that are not a wait also see one tick between them. The qpc column in the state log is how you see whether a frame is polling the counter far more than once.

| Source | Fixed-step value |
| --- | --- |
| `KeQueryPerformanceCounter` | `frame * 56250`. Frequency stays 3375000 Hz (56250 is 3375000/60). |
| `KeQueryInterruptTime`, and `KeInterruptTime` | `frame * 166666` in 100 ns units (10000000/60). |
| `KeQuerySystemTime`, `KeSystemTime`, `NtSetSystemTime`'s read | epoch + interrupt time. Epoch is 2026-01-01 00:00:00 UTC as 100 ns since 1601 (`134116992000000000`). |
| `KeTickCount` | `(frame * 1000) / 60` milliseconds. The step alternates 16 and 17 and lands on 1000 at frame 60. |
| `RDTSC` (`x86_rdtsc`) | `frame * 12222222` (733333333/60). `.text`, DSOUND, XGRPH, and WMADEC all come through this. |
| `KeDelayExecutionThread`, `KeStallExecutionProcessor` | On the thread that presents: consume whole ticks of the guest duration, then yield. A delay shorter than one tick does not move time by itself. The yield runs every other guest thread that is already waiting until each of them is blocked again, so a `Sleep(0)` poll observes the same APC every run. If that delay was non-zero and no other thread ran, one sim tick is added so a timed poll cannot spin. On any other thread: drop the turn, block until sim time has covered the delay (at least one tick), then wait for the turn again. No wall-clock sleep. |
| Timer DPCs | Deadline is guest 100 ns. The timer thread does not pop. Present fires due timers inline, in deadline order, on the present thread, then rearms a period by `period_ms * 10000` (100 ns). |
| Vblank | The free-running thread does not increment. Present bumps the count once. `BlockUntilVerticalBlank` returns at once. |
| `app::Pace` | Skipped, so the harness is not fps-capped. A Vulkan present can still wait on vsync. |

`Wait` timeouts are still wall-clock. A title that waits on an object with a timeout can diverge. The stall row is the check for that.

Guest threads take turns while fixed-step is on. `CallGuest` takes the turn, and a kernel wait gives it up. The title thread is the presenter from the moment it starts, because the boot `Sleep(0)` loop runs before the first present that used to mark it. That loop is `sub_0001C836`: it calls `sub_0004CF90(1)`, which queues an APC (`NtQueueApcThread`, thunk `0x34269`) onto the background loader, and spins on `Sleep(0)` until the loader's APC sets the dword at `0x2D02D0`. Each extra spin queues another APC, and each APC decrements the scheduler count at `0x2D02D4`. The spin used to be as long as the host took to schedule the loader, so two runs diverged at frame 3 in `.data` with different delay counts and that dword equal to `-(delay count + 1)`. The yield makes the loader run the APC inside the first `Sleep(0)`. The delay count at frame 3 is 1. `NtQueueApcThread` notifies under the dispatcher lock, so the alertable wait cannot miss the APC.

`PsTerminateSystemThread` ends in `pthread_exit`, which does not run C++ destructors. A thread that exits that way used to leave the turn taken: both surviving guest threads then sat in `UnblockGuest` and presents stopped (idle replay, frame 107, `g_busy` still 1, `g_waiting` 2, no thread left inside `CallGuest`). Exit releases the turn before `pthread_exit`.

There is no `GetTickCount` import.

## Input

Ports in the file are 1–4. The runtime's ports are 0–3. The file is text:

```
# fuzion-input 1
# frame port connected buttons lx ly rx ry a b x y black white lt rt
0 1 1 0000 0 0 0 0 00 00 00 00 00 00 00 00
```

Buttons and the eight analog bytes are hex. Sticks are signed. Disconnected ports are how this game fills seats with AI. The usual case is humans in ports 1–2 and ports 3–4 empty.

The first poll of a port in a frame becomes the latch. Later polls update a pending sample and still return the latch, so a recording and a replay see one sample per present. At the end of the present the latch becomes the pending sample. Record and replay together: replay wins.

The 250 ms XInput port thread does not write while fixed-step, record, or replay is on. Present samples the four ports once, on the game thread.

## RNG

One MSVC CRT global, guest `0x285630` in `.data`. The inlined sequence is `imul eax, 343FDh` / `add eax, 269EC3h` / `shr 16` / `and 7FFFh`. Hundreds of sites use it, including AI. There is no imported `srand`. The frame log prints the live dword whenever it changes, which is how a later `srand` of a clock shows up. Fixed time makes `srand` of `KeTickCount` stable even without `--seed`. `--seed` writes the dword once, before the entry point. It is not written again each frame.

## Checksum

Each present appends to the state log, then the frame counter steps.

Regions, in the order the diff tool prefers:

- `D` — each writable XBE section (`.data` virtual size includes BSS).
- `P` — 64 one-megabyte slices of physical RAM at `0x80000000`.
- system — committed pages in `0xB0000000`–`0xC0000000`, one hash.
- `U` — committed user pages from `0x00010000` through `0x7FFE0000`, in 4 MB slices. Slice 0 is the image and any heap placed just after it. This overlaps `.data` on purpose.

`F` is the mix of those four, plus the current rand dword and the counts of QPC, RDTSC, and delay calls so far.

Excluded, and zeroed before the hash:

- GPU scratch push buffer. The host writes command words there on its own timeline.
- Color and depth surface payloads (the size comes from the surface size dword). The GPU writes pixels when it retires work. A matching guest command stream can still disagree if those bytes were hashed. Surface headers stay in the hash.
- Stacks at `0xD0000000`. They hold spills and return addresses, and the host schedules the threads.

Included on purpose: audio sample bytes in physical RAM, and the packet status words the mixer writes. A mixer divergence shows up as a physical slice. The fixed-step SDL callback writes silence and does not move a voice cursor. Present mixes exactly 800 frames (48000/60) on the present thread, then runs the completion callbacks there, before the hash.

## Diff

```
python3 scripts/det-diff.py det-logs/a1.detlog det-logs/a2.detlog
python3 scripts/det-diff.py --self-check
```

Exit 0 prints `PASS N frames match`. A mismatch prints `FAIL frame N region NAME`, a cause, and the dump variables for that frame and region. Causes:

- seed field differs — RNG
- `qpc` count differs — something called `KeQueryPerformanceCounter` a different number of times
- `rdtsc` count differs — RDTSC
- `delay` count differs — `KeDelayExecutionThread`
- only system differs — a kernel object or a host thread
- a writable section differs and the seed matches — a game global
- a physical slice differs — heap, kernel data, or the mixer
- a user slice differs — a user allocation

If every frame's total hash is the same, the tool warns that time is probably not advancing the sim.

## Matrix

`scripts/det-matrix.sh` from the repo root. It is muted (`XBE_MUTE=1`), minimized, fixed-step, seed 1, and it unsets `FUZION_AUTOSTART`. Default 240 frames, 180 s per run. It generates `det-logs/idle.pads` (four ports disconnected, not from a play session) and wipes `hdd2`, `hdd3`, and `hdd4` only.

| Row | What |
| --- | --- |
| a sequential | Instance 2, twice, fresh `hdd2` each time. Same idle replay. |
| b parallel | Instance 2 and instance 3 at the same time. |
| c stall | Instance 4 with `FUZION_STALL_MS=30` against the zero-stall log from (a). |
| d gameplay | `det-logs/gameplay.pads` if it exists, capped at 3600 frames. Otherwise SKIP. |

`FUZION_DET_FRAMES`, `FUZION_DET_TIMEOUT`, and `FUZION_DET_BUILD` override the defaults.

## Results

`scripts/det-matrix.sh` on this Mac, 25 Oct 2026 9:34 PM PT, 240 frames, muted, minimized, seed 1, idle replay (four ports disconnected). Build `out/build/macos-release/RingHost`.

| Row | Result |
| --- | --- |
| a sequential | PASS. Two runs of instance 2, fresh `hdd2` each time. 240 frames match. Frame 0 total `adaa8469791e9640`. Frame 3 delay count is 1 (`qpc=87`, `rdtsc=15`). Frame 239 total `23c7bcee96e7f23a`, delay count 3, seed still `00000001`. |
| b parallel | PASS. Instances 2 and 3 at the same time. 240 frames match, same totals as (a). |
| c stall | PASS. Instance 4 with a 30 ms stall after each present matches the zero-stall log from (a), including frame 0. |
| d gameplay | SKIP. `det-logs/gameplay.pads` is not recorded yet. |

One guest thread calls `PsTerminateSystemThread` between frame 60 and frame 120 (the log line is "a guest thread exited while it held the turn"). Before that release, both surviving threads blocked in `UnblockGuest` and presents stopped at frame 107. With the release, all three idle rows finish.

Nothing in these idle rows diverged. They do not cover menus, character select, or a minigame. That is row (d).

## Manual check

A normal launch, with none of the variables above set, should look and pace the way it does today. Sound stays on unless `XBE_MUTE` is set.

To record the gameplay row, quit any Ring Host already open, plug controllers into ports 1 and 2, leave ports 3 and 4 empty, and run:

```
cd out/build/macos-release
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_RECORD_INPUT=../../../det-logs/gameplay.pads \
  FUZION_STATE_LOG=../../../det-logs/gameplay.detlog \
  ./RingHost
```

Start a two-human game. Go through the board menus and character select, then play Twisted System, Sumo Balls, and Jetstream. A few seconds of each is enough; finishing a round is better. Quit with Cmd-Q. Do not commit `det-logs/`. Say when `gameplay.pads` is there and the matrix can replay it.

Replay check, pads unplugged, same directory:

```
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_REPLAY_INPUT=../../../det-logs/gameplay.pads \
  ./RingHost
```

The menus and the minigames should follow the recording with the controllers ignored.

## Still wall-clock, even when the idle rows match

- `Wait` timeouts.
- A `srand` after image load replaces `--seed`. The seed column shows it. Fixed time keeps a clock-seeded `srand` stable across runs.
- A host thread that writes guest RAM without taking the turn (an IO status block, a GPU fence). The 240-frame stall row matched, so that did not show up on the idle replay. An earlier 8-frame stall differed once at frame 0 in `user_0` only, and the raw dumps were identical; it did not repeat.
- Stacks are not hashed, so a value that only ever lived in a spill is invisible.
- GPU pixels are not hashed, so a render-only divergence does not fail a row. That is deliberate.

## Cross-platform

A lockstep session can put a Mac arm64 client in the same game as a Windows or Linux x86-64 client. The per-frame hash has to match across those builds, not only across two runs of one binary.

Upstream already builds those hosts. `build.bat` is the native Windows build. `cmake/windows-cross.cmake` is clang-cl plus lld-link against an xwin sysroot, via the `windows-cross` preset, and it runs on a Linux machine. `linux-release` is the native Linux build. The Apple patch (`patches/sdk-macos-arm64.patch`) is applied only when `APPLE` is set. The lockstep patch is applied on every host, after the Apple patch when there is one:

- `patches/sdk-lockstep.patch` — bundled math, codegen, CRT, mixer, CPU vertex math, and the host rounding mode. Marker: `XBE_LOCKSTEP_FP`.
- `patches/sdk-lockstep-memory.patch` — zero a freshly committed page on Linux and Windows.
- `patches/sdk-lockstep-memory-apple.patch` — the same zeroing for the Darwin commit path.

`tools/apply-lockstep.cmake` picks the memory patch from whether `HostProtect` is already in the tree, and it does nothing if the marker is already present. Do not fold these hunks back into the Apple patch. A full `git -C sdk diff` would.

### Floating point

The recompiled guest is compiled with contraction off and without fast-math. GCC 16 at `-O2` emits `fmadd` unless `-ffp-contract=off` is passed, and `#pragma STDC FP_CONTRACT OFF` does not stop it. Clang contracts by default and honors both the flag and `#pragma clang fp contract(off)`. MSVC `/fp:precise` does not contract. clang-cl still does, so the Windows build also passes `/clang:-ffp-contract=off`. On x86-64, GCC and Clang get `-mfpmath=sse -msse2`, so a `float` or `double` expression is not evaluated in 80-bit x87 registers.

x87 data registers in this recomp stay IEEE binary64. The title's control word at thread start is `0x27F`, which is 53-bit precision. An 80-bit memory operand is an explicit 64-bit significand plus a 15-bit exponent. Load and store convert that encoding with integer arithmetic (`xbe_load80` / `xbe_store80`). The result does not follow host `long double`, which is 64-bit on arm64 and on MSVC and 80-bit on x64 Linux GCC.

The host rounding mode is forced to round-to-nearest, with flush-to-zero and denormals-are-zero off (`xbe_arm_host_fp`). That runs on the main thread, at each new host thread, on every indirect guest call, and again after a kernel export returns. The guest MXCSR is stored and is not copied onto the host. A title that sets flush-to-zero would disagree with a real Xbox and would still agree with every other recompiled client.

`sqrt` stays on the hardware. IEEE-754 requires it to be correctly rounded, and so do `nearbyint`, `trunc`, `floor`, and `ceil` once the rounding mode is the one above. Every other guest-visible transcendental goes through `sdk/runtime/cpu/softmath.cpp`: sin, cos, tan, asin, acos, atan, atan2, log, log2, log10, exp, exp2, pow, fmod, remainder, ldexp, frexp, and the float wrappers the mixer and the vertex interpreter use. The recompiler emits those names, so a generated guest partition does not call the host libm. Polynomial coefficients are hex floats, so compilers do not re-round a decimal literal differently.

Left on the host libm, on purpose:

- The HDR display tone map in `d3d12.cpp` (`Pq`). It runs after the frame, for the monitor, and it is not guest state.
- `src/common/game_info.cpp` stick motion. That file runs only for `FUZION_AUTOSTART`, which the harness unsets.
- `sdk/harness/reference_renderer.cpp`. It is not linked into the game.

### Other host leaks

A page that is committed for the first time is zeroed in `Commit`, including a recommit after decommit. Windows `VirtualAlloc` already zeros a fresh commit. Linux `mprotect` does not, and Darwin `MADV_DONTNEED` does not promise zeros the next time the page is touched. Pages that were already committed are not wiped.

The state log and the pad file are text (`# det 1`, `# fuzion-input 1`, hashes and buttons in hex). Nothing in either format is a host pointer, a `size_t` width, or a struct dumped with the host's padding. `scripts/det-diff.py` compares the text.

`std::map` is used where the runtime walks a table into guest-visible work: DirectSound voices, reservations, device paths. `std::unordered_map` iteration is not used to order a guest-visible sequence, except the case-insensitive object-name fallback. That scan keeps the exact-key hit, and when several names fold to the same string it returns the lexicographically lowest key. GPU unordered maps are caches or a debug log. Fixed-step scheduling is the guest turn lock, not the host's thread scheduler.

Struct layout of Xbox objects and host pointers written into guest RAM were not audited line by line. The rule for both is the same: guest RAM holds guest integers and guest floats, not a host address and not a host compiler's padding.

### What was run

`scripts/fp-cross.sh` on this Mac, 25 Oct 2026. Clang arm64, Clang x86_64 under Rosetta (`arch -x86_64`, `uname -m` reports `x86_64`), and Homebrew GCC 16.2.0 arm64, each with `-ffp-contract=off -fno-fast-math`. All three printed the same digest and `PASS`:

```
f95fc86463222bef PASS
```

The digest covers the bundled transcendentals, an 80-bit store/load round trip (including a significand that has to round), `ldexp`/`frexp`, a float mul-add chain, and a subnormal multiply. It does not boot the game.

The rebuilt Mac arm64 `RingHost` then replayed the idle pads twice, 25 Oct 2026 10:01 PM PT. Instance 2, fresh `hdd2` each time, muted, minimized, fixed-step, seed 1, 240 frames. `scripts/det-diff.py` printed `PASS 240 frames match`. Frame 0 total `adaa8469791e9640`. Frame 239 total `23c7bcee96e7f23a`, delay count 3, seed `00000001`. Those are the same totals as the 9:34 PM PT matrix, so this idle path did not move. The generated guest code calls `xbe_sin` (and the other bundled names) and no longer calls `std::sin`.

This machine has no Docker, no Podman, no QEMU, no `clang-cl`, and no xwin sysroot. A full RingHost cannot be linked x86_64 here: the Homebrew Vulkan loader is arm64. So there is no Linux x86-64 replay and no Windows x64 replay yet. Rosetta covered the math digest only. The comparison that is still required, once a Linux or Windows build exists, is:

```
# on each machine, same idle.pads, same seed, fresh hdd for that instance
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_REPLAY_INPUT=det-logs/idle.pads \
  FUZION_STATE_LOG=det-logs/cross.detlog \
  FUZION_FRAMES=240 FUZION_MINIMIZED=1 \
  ./FuzionFrenzy
python3 scripts/det-diff.py mac.detlog linux.detlog
```

The Mac binary is `RingHost`. Linux and Windows keep the name `FuzionFrenzy`. The frame-0 total from the 9:34 PM PT idle matrix will not match a build that contains this math. Two runs of the new binary still have to match each other. That re-run is recorded below when it has been done.

Windows, from a Linux machine that has the xwin sysroot from the top of `cmake/windows-cross.cmake`:

```
cmake --preset host-tools && cmake --build out/build/host-tools
cmake --preset windows-cross && cmake --build out/build/windows-cross
```

The Windows executable has to be run on Windows. This Mac cannot produce it.
