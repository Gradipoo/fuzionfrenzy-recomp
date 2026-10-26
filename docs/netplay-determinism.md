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
| `--replay-input PATH` | `FUZION_REPLAY_INPUT` | Feed that file. Real pads, the keyboard, and `FUZION_AUTOSTART` are ignored. Past the last frame the last sample is held. Port 2 follows `FUZION_PORT2_MASH` instead of the file when that flag is on. |
| `--seed N` | `FUZION_SEED` | Write `N` (`strtoul` base 0) into the rand global once, at image load. |
| `--state-log PATH` | `FUZION_STATE_LOG` | Per-present checksum log. |
| `--frames N` | `FUZION_FRAMES` | Exit 0 after N presents. |
| `--stall-ms N` | `FUZION_STALL_MS` | Sleep N milliseconds at the start of each present, before the hash. |
| `--minimized` | `FUZION_MINIMIZED` | Show the window, then minimize it. |
| `--state-dump DIR` | `FUZION_STATE_DUMP` | With the frame below, write `frame_N_region.bin`. |
| `--state-dump-frame N` | `FUZION_STATE_DUMP_FRAME` | Which present to dump. |
| `--state-dump-region NAME` | `FUZION_STATE_DUMP_REGION` | `phys_03`, `.data`, `system`, `user_0`, or a section name. Empty dumps the writable sections and the 64 physical slices. |
| | `FUZION_PORT2_KEYS` | Move the keyboard map from port 1 onto port 2. Unset, empty, or `0` leaves it off. There is no command-line switch: the keyboard file does not see argv. |
| `--port2-mash N` | `FUZION_PORT2_MASH` | Drive port 2 from a seeded pattern. Unset, empty, or `0` leaves it off. `1` uses `FUZION_SEED` when that is set, otherwise seed 1. Any other number is the seed. |

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
# fuzion-input 2
# LogKey:frame|P1 c,buttons,lx,ly,rx,ry,a,b,x,y,black,white,lt,rt|P2 ...|P3 ...|P4 ...
0|1,0000,0,0,0,0,00,00,00,00,00,00,00,00|0,0000,0,0,0,0,00,00,00,00,00,00,00,00|0,0000,0,0,0,0,00,00,00,00,00,00,00,00|0,0000,0,0,0,0,00,00,00,00,00,00,00,00
```

One line per frame, four ports, pipe-separated. That is the BizHawk movie shape (`Input Log.txt`: a `LogKey` header, then one line of every player). Buttons and the eight analog bytes are hex. Sticks are signed. A version 1 file (one line per port) still loads. Disconnected ports are how this game fills seats with AI. One human in port 1 and ports 2–4 empty is a normal match: the roster count at `0x2BAE94` is initialized to 4, and `sub_001C8B70` switches a seat to AI when the controller record at `0x2BAED8 + seat*0x28` is zero (the log line is `player.c is swiching a player to AI`). Nothing in that walk requires a second connected port. `GetAttachedDevices` opens only the ports whose bit is set.

The first poll of a port in a frame becomes the latch. Later polls update a pending sample and still return the latch, so a recording and a replay see one sample per present. At the end of the present the latch becomes the pending sample. Record and replay together: replay wins, except port 2 while `FUZION_PORT2_MASH` is on. That port is the mash. If recording is also on, the written file is the replay for ports 1, 3, and 4 and the mash for port 2.

`FUZION_PORT2_KEYS` and `FUZION_PORT2_MASH` are off unless set. Off, port 2 is whatever the pad file or a plugged controller says, and the keyboard stays on port 1. On, port 2 is reported connected even with no buttons held, which is what makes the title treat it as a second human instead of AI. A mash overrides the keyboard on port 2 when both are set. The mash is a pure function of the present index and the seed (8-present holds; for the first 4500 presents, a 600-present cycle of Start, D-pad Up, and A, then a mash that rarely presses Start). The sample that reaches the game is the sample the pads file stores. A later replay of that file does not roll the mash again, and it does not read the keyboard.

The 250 ms XInput port thread does not write while fixed-step, record, or replay is on. Present samples the four ports once, on the game thread.

## RNG

The inlined MSVC step uses guest `0x285630` in `.data`: `imul` by `343FDh`, `add` `269EC3h`, then `shr 16` / `and 7FFFh`. A headless Ghidra pass counts 1470 of those multiplies. `--seed` writes that dword once, before the entry point, and the frame log prints it when it changes. There is no imported `srand`. `_rand` at `0x1E133E` is a second copy of the same step, with its own dword, and the first-time path stores the constant 1 there. `--seed` does not touch it. The seat-to-AI switch does not call `_rand`. The address map is under Reverse engineering.

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

The openlibm rebuild, 25 Oct 2026 10:20 PM PT, was two more instance-2 idle replays, not a full a/b/c matrix. They match each other. Frame 0 total `574c0f97185e0166`. Frame 239 total `41a20ce3c40f8974`, delay count 3, seed `00000001`. The 9:34 PM totals above are the previous math.

## Manual check

A normal launch, with none of the variables above set, should look and pace the way it does today. Sound stays on unless `XBE_MUTE` is set.

To record the gameplay row with one controller, quit any Ring Host already open. Plug that pad in and leave the other ports empty. Do not set `FUZION_PORT2_KEYS` or `FUZION_PORT2_MASH`. The pad takes port 1. Ports 2–4 record as disconnected, and the title fills those seats with AI. The keyboard map still merges into port 1 while the new flag is unset, so leave the keyboard alone during this recording or those keys are part of port 1. One Start/A on the pad is enough to join; the match does not wait for a second connected port.

A wired Xbox One pad on macOS is opened by SDL's HIDAPI driver through libusb. stderr and `logs/ringhost.log` beside RingHost list each pad at startup. `driver=libusb` `type=xboxone` `connection=wired` is the USB pad. A Bluetooth Xbox pad stays on GameController and shows `driver=mfi` (empty path) or `driver=iohid` (`DevSrvsID:`). The build links libusb. It does not load a kernel extension. If configure stops because libusb is missing, `brew install libusb` and rebuild. The first button or a hard stick move on that pad prints one `first report` line.

```
cd out/build/macos-release
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_RECORD_INPUT=../../../det-logs/gameplay.pads \
  FUZION_STATE_LOG=../../../det-logs/gameplay.detlog \
  ./RingHost
```

Go through the board menus and character select, then play Twisted System, Sumo Balls, and Jetstream. A few seconds of each is enough; finishing a round is better. Quit with Cmd-Q. Do not commit `det-logs/`. Say when `gameplay.pads` is there and the matrix can replay it.

Two humans, still with one pad. Variant A puts the keyboard on port 2 (same map as port 1: WASD, arrows, space = A, enter = Start, and the rest of the table in the README). The pad stays on port 1. Both are in the pads file. Variant B replays a port-1 recording and writes port 2 from the mash. Leave the flag you are not using unset. If both are set, the mash wins.

Keyboard, live:

```
cd out/build/macos-release
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_PORT2_KEYS=1 \
  FUZION_RECORD_INPUT=../../../det-logs/gameplay-keys.pads \
  FUZION_STATE_LOG=../../../det-logs/gameplay-keys.detlog \
  ./RingHost
```

Mash layered on a recorded port-1 session (`gameplay.pads` from the one-controller command). `FUZION_PORT2_MASH=1` uses seed 1 because `FUZION_SEED=1`. The window can stay up; nothing has to be pressed. Port 2 taps Start, D-pad Up, and A on a 10 second cycle for the first 75 seconds, then mashes. Quit with Cmd-Q, or set `FUZION_FRAMES` to stop on its own.

```
cd out/build/macos-release
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_REPLAY_INPUT=../../../det-logs/gameplay.pads \
  FUZION_PORT2_MASH=1 \
  FUZION_RECORD_INPUT=../../../det-logs/gameplay-mash.pads \
  FUZION_STATE_LOG=../../../det-logs/gameplay-mash.detlog \
  ./RingHost
```

The matrix replays `det-logs/gameplay.pads` only. A two-human file is replayed by pointing `FUZION_REPLAY_INPUT` at it with both port-2 flags unset.

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

`sqrt` at an `FSQRT` stays a hardware instruction. IEEE-754 requires it to be correctly rounded. `ldexp` and `frexp` stay integer arithmetic in `softmath.cpp`, and so does the 80-bit load/store. Every other guest-visible transcendental goes through openlibm (`third_party/openlibm`, MIT/BSD, commit `5fe399749f9276eaa0b8403e507470da05cbbb3f`). The game compiles the portable C sources in `sources.txt` only: no amd64/aarch64 assembly, no long-double objects. Entry points are renamed `olm_*` so the compiler does not treat them as builtins and the link does not bind them to libsystem. `xbe_sin` and the rest call those names. The CRT uses that libm. x87 `FSIN`/`FCOS`/`FSINCOS`/`FPTAN` do not: they use openlibm plus the 66-bit pi reduction from xboxrecomp (`recomp_x87_sincos_pi66`, MIT). `|x| >= 2^63` sets C2 and leaves the stack alone. Precision control 24 rounds the significand only (`xbe_round24`), so a value outside the float range does not become inf. `FRNDINT` rounds in software from the guest control word. `CVTSS2SI` / `CVTTSS2SI` / the packed forms return `0x80000000` out of range, after a double range check, and the cast goes through `int64` so ARM does not saturate. SoftFloat extF80 is not vendored. CORE-MATH is the correctly-rounded alternative and is not what this build uses.

Left on the host libm, on purpose:

- The HDR display tone map in `d3d12.cpp` (`Pq`). It runs after the frame, for the monitor, and it is not guest state.
- `src/common/game_info.cpp` stick motion. That file runs only for `FUZION_AUTOSTART`, which the harness unsets.
- `sdk/harness/reference_renderer.cpp`. It is not linked into the game.

### Other host leaks

A page that is committed for the first time is zeroed in `Commit`, including a recommit after decommit. Windows `VirtualAlloc` already zeros a fresh commit. Linux `mprotect` does not, and Darwin `MADV_DONTNEED` does not promise zeros the next time the page is touched. Pages that were already committed are not wiped.

The state log and the pad file are text (`# det 1`, `# fuzion-input 2`, hashes and buttons in hex). Nothing in either format is a host pointer, a `size_t` width, or a struct dumped with the host's padding. Before a word is mixed, a double NaN becomes the default quiet NaN `0x7FF8000000000000` and a float NaN becomes `0x7FC00000`, sign kept. Guest RAM is not rewritten. An integer that happens to have those bits is hashed the same way. `scripts/det-diff.py` compares the text and, on the first mismatch, writes `desync_<frame>.txt` (the OpenRCT2 desync log: frame, region, both checksums, and the dump command). `FUZION_STATE_EXPECT=earlier.detlog` does that inside the run and also writes `frame_<n>_<region>.bin` under `FUZION_STATE_DUMP` or `det-desync/`, then exits. That is the snapshot-on-mismatch. There is still no socket.

`std::map` is used where the runtime walks a table into guest-visible work: DirectSound voices, reservations, device paths. `std::unordered_map` iteration is not used to order a guest-visible sequence, except the case-insensitive object-name fallback. That scan keeps the exact-key hit, and when several names fold to the same string it returns the lexicographically lowest key. GPU unordered maps are caches or a debug log. Fixed-step scheduling is the guest turn lock, not the host's thread scheduler.

Struct layout of Xbox objects and host pointers written into guest RAM were not audited line by line. The rule for both is the same: guest RAM holds guest integers and guest floats, not a host address and not a host compiler's padding.

### What was run

`scripts/fp-cross.sh` on this Mac, 25 Oct 2026. Clang arm64, Clang x86_64 under Rosetta (`arch -x86_64`, `uname -m` reports `x86_64`), and Homebrew GCC 16.2.0 arm64, each with `-ffp-contract=off -fno-fast-math`, linked to the same openlibm objects. All three printed the same digest and `PASS`:

```
06a5a5d542b715f9 PASS
```

The digest covers openlibm, the 66-bit-pi `fsin(pi)` (the value `1.2246063538223773e-16`, and it differs from openlibm `sin(pi)`), `xbe_round24` outside the float range, the out-of-range `0x80000000` convert, an 80-bit store/load round trip, `ldexp`/`frexp`, a float mul-add chain, and a subnormal multiply. It does not boot the game. The previous Taylor digest `f95fc86463222bef` is retired with that implementation.

The rebuilt Mac arm64 `RingHost` then replayed the idle pads twice, 25 Oct 2026 10:20 PM PT. Instance 2, fresh `hdd2` each time, muted, minimized, fixed-step, seed 1, 240 frames. `scripts/det-diff.py` printed `PASS 240 frames match`. Frame 0 total `574c0f97185e0166`. Frame 3 delay count is still 1 (`qpc=87`, `rdtsc=15`). Frame 239 total `41a20ce3c40f8974`, delay count 3, seed `00000001`. Those totals are not the 10:01 PM PT Taylor totals (`adaa8469791e9640` / `23c7bcee96e7f23a`). The idle path moved when the transcendental implementation changed. The two runs of this binary match each other.

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

The Mac binary is `RingHost`. Linux and Windows keep the name `FuzionFrenzy`. Diff a Linux or Windows log against the Mac idle log from this openlibm build, not against the 9:34 PM PT Taylor log.

Windows, from a Linux machine that has the xwin sysroot from the top of `cmake/windows-cross.cmake`:

```
cmake --preset host-tools && cmake --build out/build/host-tools
cmake --preset windows-cross && cmake --build out/build/windows-cross
```

The Windows executable has to be run on Windows. This Mac cannot produce it.

## Reverse engineering

Two local tools, both aimed at the XBE on this Mac. Nothing they read out of the XBE is committed. The Ghidra project, the address map, decompiler text, symbol dumps, the Unicorn virtualenv, and the oracle binary stay under `re/`, which is gitignored, and `tools/hooks/pre-commit` refuses `re/`, `*.gpr`, `*.rep`, `*.gzf`, and `time-rng-map.txt`.

Ghidra is the Homebrew formula `ghidra` (12.2). There is no cask by that name. It wants JDK 25 (`brew install openjdk@25`). `/usr/bin/java` on this Mac is a stub, so `JAVA_HOME` is `/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home`. The XBE loader is [ghidra-xbe](https://github.com/XboxDev/ghidra-xbe), built from source against this Ghidra. The published zip is built for 12.1.4 and will not load. Headless Ghidra only indexes an extension jar whose directory contains `Module.manifest` and is passed as `-Dghidra.external.modules`. The install that works is `~/Library/ghidra/ghidra_12.2_PUBLIC/Extensions/ghidra-xbe-src/`. A copy under the Homebrew prefix is not on the classpath. `-loader` matches the class name `XbeLoader`, not the display name. `tools/re/analyze_xbe.sh` sets both and runs `analyzeHeadless` into `re/ghidra/`. The post-script `tools/re/TimeRngMap.java` writes addresses, symbol names, and one instruction per hit. It does not call the decompiler. That is the query bridge. GhidraMCP is not installed.

```
brew install ghidra openjdk@25
tools/re/analyze_xbe.sh assets/default.xbe
```

The run on 26 Oct 2026, 3:11 AM PT, imported the local XBE (loader "Xbox Executable Format (XBE)", language `x86:LE:32:default:windows`), analyzed it in about 81 seconds, and wrote `re/ghidra/time-rng-map.txt`. `xboxkrnl.exe` is not in the project. The kernel names come from the loader. XDK library names, including `_rand`, come from XbSymbolDatabase inside the extension. This title has no `.XTLID` section.

Unicorn is [unicorn](https://github.com/unicorn-engine/unicorn) (GPL-2.0). It is not vendored. Homebrew has the C library 2.1.4. The differential tester uses the Python module from a virtualenv that also stays under `re/`:

```
python3.14 -m venv re/.venv
re/.venv/bin/pip install 'unicorn==2.1.4' capstone
re/.venv/bin/python scripts/unicorn_diff.py --seed 1 --count 12 assets/default.xbe
```

`scripts/unicorn_diff.py` maps the image once, copies the original instruction bytes for each selected site, and compares them with `re/oracle/fp_oracle`. That binary calls the same `softmath` the recompiler uses (openlibm, the 66-bit pi reduction, `xbe_round24`, the `0x80000000` integer convert). Exit 0 means the integer and 24-bit multiply checks matched. `fsin` / `fcos` / `fptan` / `fsincos` print `FAIL qemu-x87` and do not fail the process. Unicorn is QEMU. Its `fsin`/`fcos` reduction is not the 66-bit pi, and `|x| >= 2^63` does not set C2. The recompiler does set C2 and leaves ST0. A 24-bit `fmul` that matches the exact product but not `xbe_round24` is reported as double rounding: x87 rounds a long product to 24 bits, and `xbe_round24` rounds the already-rounded binary64 product. The 26 Oct 2026 3:27 AM PT run, seed 1, did not hit that case.

[xemu](https://xemu.app) is the whole-game reference for a later trace comparison. It is not installed. Nothing was set up for it.

### Time sources

Direct `call dword ptr [thunk]` counts from the image, checked against the fixed-step clocks above. Ghidra's xref list matches these.

| Source | Thunk | Direct calls | What the harness does |
| --- | --- | --- | --- |
| `KeQueryPerformanceCounter` | `0x2534E4` | 1, the wrapper at `0x1E8E5F` | `ClockQpc`, and a repeated read advances a tick |
| `KeQueryPerformanceFrequency` | `0x2534E8` | 1, the wrapper at `0x1E8E74` | constant 3375000 |
| `KeQueryInterruptTime` | `0x253510` | 2 (`0x24EE22`, `0x24F14D`) | `ClockInterrupt` |
| `KeQuerySystemTime` | `0x2533B0` | 7, plus the jump stub at `0x30982` | `ClockSystem` |
| `KeTickCount` | `0x2534DC` | 0 (data import) | `(frame * 1000) / 60` |
| `KeDelayExecutionThread` | `0x2533A8` | 3 (`0x3091C`, `0x24D00B`, `0x252B31`) | consume the guest duration in ticks |
| `KeStallExecutionProcessor` | `0x253488` | 7 | same |
| `KeSetTimer` | `0x253434` | 17 | Present fires due timers |
| `KeCancelTimer` | `0x2534A4` | 20 | same |
| `KeInitializeTimerEx` | `0x25343C` | 6 | same |
| `RtlTimeToTimeFields` | `0x25346C` | 5 | pure conversion |
| `RtlTimeFieldsToTime` | `0x2534E0` | 1, plus four `call ebx` in `0x1E8A24` after it loads the thunk | pure conversion |

The QPC wrapper has 11 callers: the boot spin at `0x4C549` and `0x4C555`, `0x5A9B8` and `0x5A9FC` in `0x5A990`, and `0x1BD2F8`, `0x1BD312`, `0x1BD339`, `0x1BD398`, `0x1BD3AA`, `0x1BD415`, `0x1BD428`. Frequency is read from the boot spin at `0x4C521`, from `0x5A9AA`, and from `0x1BD308`. `0x5A990` also reads `KeTickCount` through `0x1E893C`. That reader is the only instruction that loads the `KeTickCount` slot. Its other callers are `0x49DF0` and `0x49D40`.

`KeQuerySystemTime`'s jump stub at `0x30982` is called from `0x4F32A` in `0x4F210`, which `0x41EE0` calls beside Present. The other seven calls are `0x339FE` (`0x338DE`), `0x33F93`, `0x1E8C89`, `0x1E8DEA`, `0x1FE214`, `0x208800`, and `0x208D6B`.

Ghidra's instruction list has eight `RDTSC`s, not the twenty-two `0F 31` byte hits. The extras are not `RDTSC` instructions. DSOUND and XGRPH have none. The eight are:

- `0x4C526` and `0x4C58B` in the boot function `0x4C510`, which also calls the QPC and frequency wrappers.
- `0x4C5F4` in `0x4C5F0`. That helper is the common sampler: 94 callers, including `0x1C377E`.
- `0x5B41F` in `0x5B410`, `0x5B7AC` in `0x5B4A0` (seven callers), `0x5C126` in `0x5C0F0`.
- `0x238A93` and `0x238AA0` in WMADEC function `0x238A6A`, called from `0x23837A`.

Every one of those goes through `x86_rdtsc` when fixed-step is on. The hook is not limited to these eight sites.

Not imported, so this title never reaches them: `NtSetSystemTime`, `KeSetTimerEx`, `NtCreateTimer`, `NtSetTimerEx`, `NtCancelTimer`, `NtQueryTimer`, `KeInterruptTime`, `KeSystemTime`, `KeTimeIncrement`, `GetTickCount`. `NtSetSystemTime` in the runtime still stores the host clock into the previous-time pointer, including while fixed-step is on. That does not affect this title.

`D3D_BlockOnTime` at `0x1FB020` has five callers, all in the D3D section: `0x1FB2E1`, `0x1FB30B`, `0x1FB35A`, `0x1FB37E`, `0x1FB390`. `0x1FB30B` is inside `0x1FB300`, and Present calls `0x1FB300` at `0x2021FD`. The harness replaces Present, so that call does not run. `.text` never calls `BlockOnTime` directly. `BlockUntilVerticalBlank` at `0x1F5520` has one caller, Present at `0x202212`. The vblank function `0x1FD700`–`0x1FD839` has five callers, all in D3D (`0x1FDD55`, `0x1FDDFA`, `0x1FE51B`, `0x1FE9AA`, `0x2000F8`). Nothing in `.text` calls it. The callback slot at device offset `0x24EC` is unchanged from the layout note: this image does not store a game callback there.

### RNG

1470 instructions are `imul` by `0x343FD`, and 1470 are `add` of `0x269EC3`. Sixty further copies of each immediate are not instructions. 2491 `mov` instructions contain the address `0x285630` (loads, stores, and the address used as an immediate). There is no `mov dword ptr [0x285630], imm32`. `--seed` is a host write, before the entry point. The frame log is what shows a later guest store.

`_rand` at `0x1E133E`–`0x1E135F` does not use `0x285630`. It calls `0x1E2038`, which returns the per-thread CRT block, and the step reads and writes the dword at offset `0x14` of that block. The first time the block is created, `0x1E2099` stores the constant 1 at that offset. No other caller of `0x1E2038` writes offset `0x14`. There is no `_srand` symbol. `--seed` does not change this dword. Two fixed-step runs still start from 1, so they match each other, and changing `--seed` does not move this generator.

All 31 calls of `_rand` are in `.text` between `0x1BF9F0` and `0x1DDE45`. The callers are `0x1BF9F0`, `0x1BFC40`, `0x1C4420` (at `0x1C446B`), `0x1C9C90` (at `0x1C9CB5`), `0x1DA1C0`, `0x1DAB40`, `0x1DB5A0`, `0x1DBD20`, `0x1D2DC0`, `0x1D8820`, and `0x1DCDE0`. The seat-to-AI switch `0x1C8B70` does not call `_rand` and does not contain the multiply. Its caller is `0x1C2303`.

### Unicorn, 26 Oct 2026 3:27 AM PT

`scripts/unicorn_diff.py --seed 1 --count 12`, Unicorn 2.1.4, exit 0. `SUMMARY unexpected=0 qemu-x87=33`.

| Check | Result | Site |
| --- | --- | --- |
| RNG state after one step | PASS, 6 instruction shapes, 72 seeds | `0x11730`, `0x118F0`, `0x15EC7`, `0xBB581`, `0xA2179`, `0xC40C3` |
| `fistp` to int32, including ties, overflow, NaN, inf | PASS, 11 values | `0x29C1A` |
| `cvttss2si` | PASS, 9 values | `0x1FC980` (the only one, in D3D) |
| `fldcw` of `0x027F` and `0x007F` | PASS | `0x2CF43` |
| `fmul st(1)` at 53-bit and at 24-bit | PASS, 12 pairs, no double-round | `0x122E1` (`D8 C9`) |
| `fsin` | `FAIL qemu-x87`, 8 match, 6 differ, C2 missed twice | `0x122BD` |
| `fcos` | `FAIL qemu-x87`, 10 match, 4 differ, C2 missed twice | `0x23923` |
| `fptan` | `FAIL qemu-x87`, 6 match, 8 differ, C2 missed twice | `0x43B20` |
| `fsincos` | `FAIL qemu-x87`, 7 match, 7 differ, C2 missed twice | `0x8622E` |

`fsin(pi)` on this Unicorn is `0x3ca1a62633145c07`. The 66-bit model, which is what the recompiler returns, is `0x3ca1a60000000000`. `fcos(pi/2)` is the same split: Unicorn `0x3c91a62633145c07`, model `0x3c91a60000000000`. `fptan(pi/6)` leaves 1.0 in ST0, as the instruction specifies, and ST1 is one ulp off the model (`0x3fe279a74590331b` against `0x3fe279a74590331c`). `fsincos(pi/2)` leaves the cosine in ST0 and the sine in ST1. The sine matched. The cosine is the `fcos(pi/2)` pair above. The two C2 misses on each instruction are the inputs `+2^63` and `-2^63`.

`fldcw` shows that Unicorn 2.1.4 does honor a 24-bit precision control word. The `fmul` row was loaded at 53 bits and then the control word was dropped to 24 bits, so the multiply is what rounded. Those 12 pairs matched `xbe_round24`. A pair that only matches the exact long product is still a `FAIL double-round` line, and it does not by itself make the process exit 1.
