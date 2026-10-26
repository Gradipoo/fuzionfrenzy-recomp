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

Clocks stay constant during the frame and step after the checksum, so a once-per-frame performance-counter delta is exactly 1/60 s. Two reads inside one frame return the same value. If the title stored `last = now` on a second read and then subtracted, the delta would be 0 and the sim would sit still. Identical hashes on every frame are that failure. Do not switch to per-call clocks without a log that shows it.

| Source | Fixed-step value |
| --- | --- |
| `KeQueryPerformanceCounter` | `frame * 56250`. Frequency stays 3375000 Hz (56250 is 3375000/60). |
| `KeQueryInterruptTime`, and `KeInterruptTime` | `frame * 166666` in 100 ns units (10000000/60). |
| `KeQuerySystemTime`, `KeSystemTime`, `NtSetSystemTime`'s read | epoch + interrupt time. Epoch is 2026-01-01 00:00:00 UTC as 100 ns since 1601 (`134116992000000000`). |
| `KeTickCount` | `(frame * 1000) / 60` milliseconds. The step alternates 16 and 17 and lands on 1000 at frame 60. |
| `RDTSC` (`x86_rdtsc`) | `frame * 12222222` (733333333/60). `.text`, DSOUND, XGRPH, and WMADEC all come through this. |
| `KeDelayExecutionThread`, `KeStallExecutionProcessor` | Return immediately. No sleep, no yield. |
| Timer DPCs | Deadline is guest 100 ns. The timer thread does not pop. Present fires due timers inline, in deadline order, on the present thread, then rearms a period by `period_ms * 10000` (100 ns). |
| Vblank | The free-running thread does not increment. Present bumps the count once. `BlockUntilVerticalBlank` returns at once. |
| `app::Pace` | Skipped, so the harness is not fps-capped. A Vulkan present can still wait on vsync. |

`Wait` timeouts are still wall-clock. A title that waits on an object with a timeout can diverge. The stall row is the check for that.

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

Filled in after `scripts/det-matrix.sh` on this branch. Until that run, this section is the plan above and not a measured result.

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
- Guest threads that are not the present thread, if they touch RAM the hash covers.
- Stacks are not hashed, so a value that only ever lived in a spill is invisible.
- GPU pixels are not hashed, so a render-only divergence does not fail a row. That is deliberate.
