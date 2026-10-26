# Decisions

Append-only log of choices a later agent should keep. Paths in this file are
repository-relative. Machine paths, worker names, and HDD snapshots stay in
`HANDOFF.md`, which is maintainer-local.

The procedure is `docs/agent-workflow.md`. The harness manual is
`docs/netplay-determinism.md`. Each contributor records replays from their
own legally obtained disc; those files are not in the clone.

Add an entry at the bottom when a task chooses an approach. Include the date
with a timezone, the commit, the choice, and what evidence would reopen it.
Dates below are America/Los_Angeles.

## 2026-10-25 — Lockstep over streaming

Commit `36469ae` ("Add a lockstep determinism harness before any netplay.").

The protocol is lockstep input sync: both machines run the same simulation
from the same inputs, and each present's guest-RAM hash must match. This
title has no System Link code, so the protocol is new. Streaming a picture
and sending pads back is the recorded fallback, and this tree does not
implement it. The harness opens no socket and links no Steam. With every
flag off, clocks, pads, the mixer, vblank, and frame pacing stay on the
paths the game already uses.

Reopen this when a session is told to build the streaming fallback, or when
a hash match across hosts turns out to be unreachable for a mode the game
must ship. Until then, networking stays off and the next step is still a
matching hash on a host that does not have one yet.

## 2026-10-25 — openlibm and `-ffp-contract=off`

Commits `e400f23` ("Make guest floating point match across arm64 and x86-64.")
and `8cbc743` ("Route guest transcendentals through openlibm.").

Guest-visible math is compiled with contraction off and without fast-math.
GCC 16 at `-O2` emits `fmadd` unless `-ffp-contract=off` is passed, and
`#pragma STDC FP_CONTRACT OFF` does not stop it. Clang contracts by default
and honors the flag. clang-cl still contracts under `/fp:precise`, so the
Windows build passes `/clang:-ffp-contract=off`. On x86-64, GCC and Clang
get `-mfpmath=sse -msse2`.

`sqrt` (`FSQRT`) stays a hardware instruction. Every other guest-visible
transcendental goes through openlibm (`third_party/openlibm`, commit
`5fe399749f9276eaa0b8403e507470da05cbbb3f`). The game compiles the portable
C sources in `sources.txt` only. Entry points are renamed `olm_*`. x87
`FSIN` / `FCOS` / `FSINCOS` / `FPTAN` use openlibm plus the 66-bit pi
reduction. The previous Taylor series is retired.

`scripts/fp-cross.sh` (clang arm64, clang x86_64 under Rosetta, GCC 16.2.0
arm64) printed `06a5a5d542b715f9 PASS`. That hash is
`tools/fp-digest.expected`. CI checks the native digest. The idle replay
moved with the transcendental change: frame 0 total stayed
`574c0f97185e0166`, frame 239 became `41a20ce3c40f8974`. Two runs of that
binary match each other.

Reopen this when a digest or a replay shows two hosts disagree on a
guest-visible result the flags and openlibm were meant to pin.

## 2026-10-26 — Fixed-step pacing

Commit `92002fe` ("Pace fixed-step presents at 60 Hz and keep the timer on
the frame."), on top of the present yield in `36d66fb`.

One sim tick per present. `KeQueryPerformanceCounter` still advances on a
repeated read (the boot wait is thirty reads and no present). RDTSC and
interrupt time stay on the present's tick. `app::Pace()` still sleeps the
frame-cap wait, fixed step included, at `fps_cap` 60, so a full-speed run
matches the wall clock. Draws stay batched. The render thread catches up
when the guest reads a result back or inserts a fence. Present overlaps one
frame. A wait after every draw put the heavy scene under 60 Hz.

`KeDelayExecutionThread` and `KeStallExecutionProcessor` consume their guest
duration in whole ticks and return, without a wall-clock sleep. Each present
yields the guest turn so a background load runs. Lock order: never take
`g_time_mu` while holding `g_sched_mu`. Under fixed step,
`dsound::AdvanceFixed` is the only mixer-cursor advance, and the SDL
callback writes silence.

Reopen this when a replay shows the in-game timer leaving the picture, or
when a heavy scene misses the 60 Hz cap because of a wait the pace commit
removed.

## 2026-10-26 — XInput packet compares report fields

Commit `84c3a10` ("Count an XInput packet only when a report field changes.").
Marker `XBE_XINPUT_PACKET`.

`XInputGetState` is the strong HLE `sub_0024D2AE` (`SYM_XInputGetState`).
It writes the packet number into the caller's `XINPUT_STATE` at guest
`0x002CC1FC`. The old increment was `memcmp` of the whole `input::Pad`,
which includes the padding byte after `connected`. `Sample::Pad()` fills
the report fields and leaves that padding alone, and Linux's padding
differed between frame 4 (sim tick 90, value 1) and frame 5 (sim tick 98,
value 2). Mac's padding was stable, so the stored packet stayed 1.

The packet now advances when a report field changes: `connected`,
`buttons`, the eight analog bytes, and `lx` / `ly` / `rx` / `ry`. Normal
play uses the same compare. The recording already made on the stable host
stayed valid, so that pad file did not need to be recorded again. After
this change, macOS arm64 and Linux arm64 both printed `PASS 7766 frames
match` against that recording's expect log. Frame 7765 total
`6a609a6637cce6ba`. The log itself stays on the machine that recorded it.

Reopen this when a replay shows two hosts advancing the packet on a field
the game reads and this compare ignores.

## 2026-10-26 — Boot thread keeps the turn until its exit signal is stored

Commit `8641ec8` ("Keep the turn only for the boot thread's exit signal.").
Marker `XBE_THREAD_EXIT`. Supersedes the wider hold in `96e6985`.

The first x86_64 gameplay2 replay diverged at frame 0 in `system`. The only
differing dword was guest `0xB0000404`, the signal of the boot `ETHREAD`.
`MarkExited` sets it. `CallGuest` released the turn before that store, and
QEMU hashed the clear signal. A fast host usually takes the turn back first.

Only that boot return keeps the turn through `MarkExited`. Later returns
still drop the turn first, then take it again under fixed step for their
own `MarkExited`. Holding every return dropped one
`KeQueryPerformanceCounter` read and moved Mac frame 524 (`.data` diverged,
`qpc` 15136 instead of 15137). Normal play does not take the turn.

After the narrower hold, Mac (two replays), arm64 Linux, and x86_64 Linux
each printed `PASS 7766 frames match`. Exit sim tick 25350. Frame 7765
total `6a609a6637cce6ba`.

Reopen this when a host hashes a still-clear signal for a thread that
returned before the present, or when holding a later return moves a Mac
frame that already matches.
