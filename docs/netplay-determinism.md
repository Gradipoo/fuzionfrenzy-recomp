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

Present advances the sim tick by one, after the checksum. A repeated `KeQueryPerformanceCounter` read advances one tick first and then returns the new value. That is the boot wait: thirty reads and no present. RDTSC and interrupt/system time stay on the present's tick when a frame samples them, so the in-game timer moves one step per present. A tight spin, forty reads of the same clock with the tick unmoved, still advances, or a load that waits on the counter would not finish. `KeDelayExecutionThread` and `KeStallExecutionProcessor` consume their guest duration in whole ticks and return, without sleeping. The picture waits out the fps cap (60) on every present, fixed step included, so a full-speed run matches the wall clock. Guest time stays the sim tick.

That repeat rule is what gets past boot. `sub_0004C510` samples RDTSC, then spins on `KeQueryPerformanceCounter` until the delta reaches the performance frequency divided by two (half a second, 1,687,500 counts). A counter that only moved on present never left that spin: the first run hashed frame 0 and then sat in the wait at 100% CPU. Thirty repeated QPC reads are exactly half a second (`30 * 56250`), so the wait exits and the TSC delta beside it stays in step with the QPC delta, because those QPC reads move the shared tick. Two QPC reads that are not a wait also see one tick between them. An RDTSC read does not. The qpc column in the state log is how you see whether a frame is polling the counter far more than once.

| Source | Fixed-step value |
| --- | --- |
| `KeQueryPerformanceCounter` | `frame * 56250`. Frequency stays 3375000 Hz (56250 is 3375000/60). |
| `KeQueryInterruptTime`, and `KeInterruptTime` | `frame * 166666` in 100 ns units (10000000/60). |
| `KeQuerySystemTime`, `KeSystemTime`, `NtSetSystemTime`'s read | epoch + interrupt time. Epoch is 2026-01-01 00:00:00 UTC as 100 ns since 1601 (`134116992000000000`). |
| `KeTickCount` | `(frame * 1000) / 60` milliseconds. The step alternates 16 and 17 and lands on 1000 at frame 60. |
| `RDTSC` (`x86_rdtsc`) | `frame * 12222222` (733333333/60). `.text`, DSOUND, XGRPH, and WMADEC all come through this. |
| `KeDelayExecutionThread`, `KeStallExecutionProcessor` | On the thread that presents: consume whole ticks of the guest duration, then yield. A delay shorter than one tick does not move time by itself. The yield runs every other guest thread that is already waiting until each of them is blocked again, so a `Sleep(0)` poll observes the same APC every run. If that delay was non-zero and no other thread ran, one sim tick is added so a timed poll cannot spin. On any other thread: arm the deadline while that thread still holds the turn, drop the turn, block until sim time has covered the delay (at least one tick), then wait for the turn again. No wall-clock sleep. The present yield does not resume the title while that deadline is already due or the thread is between the delay and its next block. A delay that still has ticks left stays blocked, and the yield returns. If the presenter is blocked in a kernel wait, that delay advances virtual time itself so the loader can finish and signal. A present yield does not grant that time. |
| Timer DPCs | Deadline is guest 100 ns. The timer thread does not pop. Present fires due timers inline, in deadline order, on the present thread, then rearms a period by `period_ms * 10000` (100 ns). |
| Vblank | The free-running thread does not increment. Present bumps the count once. `BlockUntilVerticalBlank` returns at once. |
| `app::Pace` | Still applied, fixed step included. The harness is capped at `fps_cap` (60). A Vulkan present can also wait on vsync. |

Under fixed step, `Wait` timeouts are sim ticks. A waiting thread stays off the turn until the object is signaled, an alertable APC is queued, or that sim deadline arrives. It does not wake on the wall clock. The stall row is the check for that.

Guest threads take turns while fixed-step is on. `CallGuest` takes the turn, and a kernel wait gives it up. The thread that blocked earliest runs next. A thread created while another holds the turn is given its place before it starts, so the order does not follow which host thread wakes first. Each present yields after the clock steps, so a background load runs until it blocks even when that frame never calls `KeDelayExecutionThread`. Without that yield the title thread keeps presenting and the load never finishes. The yield stays there until a loader delay that has already come due runs to its next block, so that load does not depend on which host thread the scheduler runs first. A thread sleeping on a sim deadline records the kick it has acknowledged. The yield does not resume the title while any sleeper is still asleep on an older kick. A kick that is not that delay's deadline does not end the wait. The title thread is the presenter from the moment it starts, because the boot `Sleep(0)` loop runs before the first present that used to mark it. That loop is `sub_0001C836`: it calls `sub_0004CF90(1)`, which queues an APC (`NtQueueApcThread`, thunk `0x34269`) onto the background loader, and spins on `Sleep(0)` until the loader's APC sets the dword at `0x2D02D0`. Each extra spin queues another APC, and each APC decrements the scheduler count at `0x2D02D4`. The spin used to be as long as the host took to schedule the loader, so two runs diverged at frame 3 in `.data` with different delay counts and that dword equal to `-(delay count + 1)`. The yield makes the loader run the APC inside the first `Sleep(0)`. The delay count at frame 3 is 1. `NtQueueApcThread` notifies under the dispatcher lock, so the alertable wait cannot miss the APC.

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

Under fixed step, draws stay batched. The render thread is caught up when the guest reads a result back or inserts a fence. Present keeps one frame of overlap with the swapchain, the same shape as a normal frame, so a heavy scene is not stuck behind vsync. A wait after every draw dropped that scene under 60 Hz.

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

On 26 Oct 2026, after the present yield and fixed-step GPU completion, `FUZION_DET_FRAMES=240 ./scripts/det-matrix.sh` on this Mac printed `MATRIX PASS`. Rows a, b, and c match, including the 30 ms stall and instances 2 and 3 at once. Frame 0 is still `574c0f97185e0166`. Frame 60 is `baaf36e0af926229`. Frame 120 is `5d0383df0cdc90d9`. Frame 180 is `fe93328f826a54bb` on a1, a2, both parallel instances, and the stall run.

Later that morning the same recording was replayed with the present paced to 60 Hz and with RDTSC left on the present's tick. Idle frame 0 is still `574c0f97185e0166`. Two idle replays match each other. Frame 60 is `c54a1b7dd2ac5949`. Frame 120 is `b136e69e7660e89f`. Frame 180 is `9ceb396f12b8bcb3`. From present 600 on, the sim tick advances once per present (present 600 is tick 4199, present 4200 is tick 7806, present 5999 is tick 9637). A 4200-frame replay of `gameplay.pads` finished in 70 s, 10.0 s per 600 presents from present 600 through the Sumo stretch. The present yield was 0.02 ms. The last stretch of Jetstream is still the heavy one: about 40 presents per second with the window minimized. The sim tick still advances once per present there, so the timer stays with the picture. The pre-pace binary averaged about 22 fps over the whole file, because every draw waited for the render thread. The yield is not that cost. A normal (fixed-step off) 120-frame replay of the same pads still finishes in about 4.7 s.

Two replays of all 5999 frames of `gameplay.pads` on this paced build match (`PASS 5999 frames match`). Two replays on the pre-pace binary match as well. Against the live log from the recording session, `.data`, physical RAM, and the system region match on every frame. Only user slice 0 differs. That session was `./RingHost` with no `--instance`. The pre-pace replays used `--instance 2` and a fresh `hdd2`. The slice does not move the simulation.

## Manual check

A normal launch, with none of the variables above set, should look and pace the way it does today. Sound stays on unless `XBE_MUTE` is set.

To record the gameplay row with one controller, quit any Ring Host already open. Plug that pad in and leave the other ports empty. Do not set `FUZION_PORT2_KEYS` or `FUZION_PORT2_MASH`. The pad takes port 1. Ports 2–4 record as disconnected, and the title fills those seats with AI. The keyboard map still merges into port 1 while the new flag is unset, so leave the keyboard alone during this recording or those keys are part of port 1. One Start/A on the pad is enough to join; the match does not wait for a second connected port.

A wired Xbox Series pad on macOS is opened through GameController. Apple's XboxGamepad driver owns USB id 045e:0b12, and SDL reports that pad as `driver=mfi` with an empty path. The product id in the log is `0b13`, SDL's stand-in for a Series pad with a Share button, and `connection=unknown`. The port line follows the hint line by about 150 ms; until then the log can say `no gamepads yet`. A pad Apple's driver does not claim still uses HIDAPI. `driver=libusb` is that path. `driver=iohid` (`DevSrvsID:`) is an IOHID pad. The build links libusb and does not load a kernel extension. If configure stops because libusb is missing, `brew install libusb` and rebuild. The first button or a hard stick move on that pad prints one `first report` line.

The parent directory of those two paths is created when the run starts. `../../../det-logs` is the repo's `det-logs/` only when the shell is already in `out/build/macos-release`. Rebuild `RingHost` before recording. A binary from before the present yield plays the intro and then never runs the background loader, so the menus stay empty. Leave `FUZION_AUTOSTART` unset. Those presses would be written into the pads file. Fixed step stays on so the file can be replayed.

```
cd out/build/macos-release
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_RECORD_INPUT=../../../det-logs/gameplay.pads \
  FUZION_STATE_LOG=../../../det-logs/gameplay.detlog \
  ./RingHost
```

Go through the board menus and character select, then play Twisted System, Sumo Balls, and Jetstream. A few seconds of each is enough; finishing a round is better. Quit with Cmd-Q. Do not commit `det-logs/`.

`tools/shots/scenes.txt` on the screenshot branch names these presents in the 26 Oct 2026 recording, replayed with fixed step, seed 1, and the 60 Hz pace: title 200, board 1200, character select 1400, Twisted System countdown 1800, Twisted System round 2200, Sumo Balls 3400, Jetstream 5000.

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

- A `srand` after image load replaces `--seed`. The seed column shows it. Fixed time keeps a clock-seeded `srand` stable across runs.
- A host thread that writes guest RAM without taking the turn (an IO status block). A fixed-step read-back or fence waits until that batch is recorded, so it is not sampled on the host clock. Present itself overlaps one frame. An earlier 8-frame stall differed once at frame 0 in `user_0` only, and the raw dumps were identical; it did not repeat.
- Stacks are not hashed, so a value that only ever lived in a spill is invisible.
- GPU pixels are not hashed, so a render-only divergence does not fail a row. That is deliberate.

## Cross-platform

A lockstep session can put a Mac arm64 client in the same game as a Windows or Linux x86-64 client. The per-frame hash has to match across those builds, not only across two runs of one binary.

Upstream already builds those hosts. `build.bat` is the native Windows build. `cmake/windows-cross.cmake` is clang-cl plus lld-link against an xwin sysroot, via the `windows-cross` preset, and it runs on a Linux machine. `linux-release` is the native Linux build. The Apple patch (`patches/sdk-macos-arm64.patch`) is applied only when `APPLE` is set. The lockstep patch is applied on every host, after the Apple patch when there is one:

- `patches/sdk-lockstep.patch` — bundled math, codegen, CRT, mixer, CPU vertex math, and the host rounding mode. Marker: `XBE_LOCKSTEP_FP`.
- `patches/sdk-lockstep-memory.patch` — zero a freshly committed page on Linux and Windows.
- `patches/sdk-lockstep-memory-apple.patch` — the same zeroing for the Darwin commit path.
- `patches/sdk-lockstep-load-yield.patch` — each present yields the guest turn, object-wait timeouts follow sim ticks, and a fixed-step GPU submit finishes before the guest continues. Marker: `XBE_LOAD_YIELD`. `tools/apply-lockstep.cmake` applies it after `patches/sdk-lockstep-harness.patch` when the Apple patch has already added `determinism.cpp`.
- `patches/sdk-lockstep-host.patch` — the same runtime and call sites, against the pinned sdk, for Linux and Windows. Applied only when `determinism.cpp` is absent. It already contains `XBE_LOCKSTEP_HARNESS` and `XBE_LOAD_YIELD`, so the two patches above are skipped.

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

On 26 Oct 2026 (Pacific) the paced Mac `RingHost` replayed `det-logs/gameplay2.pads` twice with `FUZION_FIXED_STEP=1`, `FUZION_SEED=1`, and `FUZION_STATE_EXPECT` pointed at `det-logs/gameplay2.detlog`. Both runs exited 0. `scripts/det-diff.py` printed `PASS 7766 frames match` for the two replays and for the first replay against that expect log. The recording was launched with no `--instance`, so the replays used the same `hdd` beside the executable, restored from a snapshot before each run.

GitHub CI does not run that comparison. The workflow builds the math digest, `det-diff.py --self-check`, the host tools, and the runtime against `tools/ci/stub_symbols.py`. It never checks out a disc image or a detlog. The real cross-check stays on this Mac.

The Homebrew Vulkan loader is arm64, so a Linux `FuzionFrenzy` cannot be linked on the host itself. The local path is a Linux VM or container whose disk never leaves the machine. Arm64 Linux first (native on this M3). x86_64 Linux second, under emulation, for correctness. Generated C, the XBE, pads, and detlogs stay on the guest disk and are not committed or pushed. On 26 Oct 2026 (Pacific) Lima 2.2.1 was installed with Homebrew and an arm64 Ubuntu 26.04 guest was started as `ff-det` (`limactl start --name ff-det --cpus 6 --memory 8 template:ubuntu`).

Install one of these, then the packages inside the guest:

- OrbStack, Lima, or Docker Desktop. A Lima VM is `brew install lima` and `limactl start`.
- In the guest: `clang`, `cmake`, `ninja`, `python3`, `pkg-config`, `zlib1g-dev`, `libvulkan-dev`, `glslang-dev`, `mesa-vulkan-drivers`, `xvfb`, and the X11 headers SDL's CMake fetch needs (`libx11-dev`, `libxext-dev`, `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`).

Lima's Ubuntu template mounts the home directory read-only. Configure writes the lockstep patch into `sdk/` and the recompiled guest into `out/`, so copy the tree onto the guest disk first. A writable mount is the other way. Either copy stays on this Mac.

```
# Lima mounts the Mac home read-only at the same path. $HOME is the guest disk.
rsync -a --copy-links --exclude out \
  /Users/gsrvm3/Documents/projects/ff-linux-det/ "$HOME/ff-src/"
cd "$HOME/ff-src"
./build.sh
# lavapipe is the software Vulkan ICD. GPU pixels are not in the hash.
# This Ubuntu 26.04 image names it lvp_icd.json. Other images use
# lvp_icd.aarch64.json or lvp_icd.x86_64.json in the same directory.
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
cd out/build/linux-release
rm -rf hdd2
xvfb-run -a env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_REPLAY_INPUT=/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/idle.pads \
  FUZION_STATE_LOG=/tmp/linux-idle.detlog \
  FUZION_FRAMES=240 FUZION_MINIMIZED=1 \
  ./FuzionFrenzy --instance 2
```

Back on the Mac, diff that log against a Mac idle log from this same paced build (instance 2, fresh `hdd2`, the same flags, `RingHost`). Do not diff it against the 9:34 PM PT Taylor log.

```
python3 scripts/det-diff.py mac-idle.detlog linux-idle.detlog
```

The same command with `det-logs/gameplay2.pads`, `FUZION_FRAMES=7766`, and `FUZION_STATE_EXPECT` pointed at `det-logs/gameplay2.detlog` is the gameplay check. The Mac binary is `RingHost`. Linux and Windows keep the name `FuzionFrenzy`.

x86_64 uses the same steps in an emulated guest (`limactl start --arch=x86_64 template:ubuntu`, or Docker `--platform linux/amd64`). Point `VK_DRIVER_FILES` at that image's lavapipe ICD. Emulation is enough to compare checksums. It is not a frame-rate test. `brew install lima-additional-guestagents` is what provides the non-native Lima guest agent.

Windows, from a Linux machine that has the xwin sysroot from the top of `cmake/windows-cross.cmake`:

```
cmake --preset host-tools && cmake --build out/build/host-tools
cmake --preset windows-cross && cmake --build out/build/windows-cross
```

The Windows executable has to be run on Windows. This Mac cannot produce it.
