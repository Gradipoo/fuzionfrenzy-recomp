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

## 2026-10-31. Snapshot stacks are copied at the setjmp

The copy lives in `src/det/snapshot.cpp`. The SDK side is
`patches/sdk-lockstep-snapshot.patch`. The guest-window hint is
`patches/sdk-lockstep-snapshot-memory.patch` or the apple companion,
because those two `memory.cpp` files do not share that function. The
submodule pointer stays put. Tracy stays out of the patch.

The blocked stack is copied in `Fill`, at the `sigsetjmp`, into a buffer
allocated while the snapshot arena is bypassed. The bytes are `[sp, hi)`.
Resume longjmps back to that point and takes the wait again. A copy taken
later, or one that starts 2048 bytes below that SP, includes the wait the
thread is already inside. The resumed thread then faults in `det::Park`
(signal 11). Allocating the image on the arena changes a later guest
checksum. The bypass keeps the save on the existing replay.

On 31 Oct 2026 (Pacific) the Mac `RingHost` saved frames 20, 200, 1200,
2200, 3400, and 5000 with `FUZION_STATE_EXPECT` on `gameplay2.detlog` and
exited 0 at sim tick 22585. Resume of frame 20 through frame 140 exited 0
at sim tick 1789. The five 120-frame scene resumes passed. Local
`ctest -L fast` passed five tests.

Reopen this when a resume faults in `Park`, or when a save run's expect
mismatches on a frame before the snapshot.

## 2026-10-31 — Delay lockstep over UDP

Commit `7307778` ("Add a delay lockstep session over UDP."). The present
hook is the patch `patches/sdk-lockstep-net.patch` (`XBE_LOCKSTEP_NET`).

The session lives in `src/net`. Frame N runs the samples from frame N
minus the delay. Seat 1 publishes port 1, seat 2 publishes port 2, and
ports 3 and 4 stay disconnected so the title fills them with AI. A
mismatch of the per-frame total stops both sides. A silent peer stops
them too. With the flag unset, no socket is opened.

GekkoNet (`HeatXD/GekkoNet`, BSD-2-Clause) is a rollback library. Its
update call returns save and load events, so it is not this protocol.
GameNetworkingSockets (Valve, BSD-3-Clause) is a transport. Its build
needs protobuf and a crypto library, and Steam Datagram Relay is
Steamworks-only. Neither is vendored. The Steamworks SDK stays out of
this repo.

Reopen this when a session needs rollback, or when two machines cannot
meet on plain UDP.

## 2026-11-11: Relay datagram when plain UDP cannot meet

The 2026-10-31 lockstep decision reopens here. Two machines cannot meet
on plain UDP. Both sides still run delay lockstep. The datagram is a
relay. No accounts. Not for public deployment.

The server mints a 16 byte token. One session holds four human seats.
The F1 settings panel hosts or joins, and the server assigns the seat.
The host starts the match. Whoever has joined plays. Empty seats stay
disconnected. With `--lockstep` and no `--lockstep-peer`, the title
thread keeps presenting until that start notice. Frame 0 waits for it.
A direct peer still uses two-seat UDP. The hooks are
`patches/sdk-lockstep-relay.patch` (`XBE_LOCKSTEP_RELAY`) and
`patches/sdk-lockstep-lobby.patch` (`XBE_LOCKSTEP_LOBBY`).

## 2026-11-08: Kicked-sleeper pre-registration did not settle the turn

No commit. Reverted. The working tree and patches are back at `0be2bb7`.

`YieldGuest` was changed to wake the time waiters while it still held the
turn, register every Park sleeper that could run as waiting before `g_busy`
dropped, and acknowledge a wake in `AcquireTurn` instead of in `Park` or
`WaitTicks`. `AbandonGuest` released a pre-registered wait. The edits were
in `patches/sdk-lockstep-load-yield.patch` and `patches/sdk-lockstep-host.patch`.

Mac arm64 passed 20 of 20 replays at 250 frames once, and the full 7766
replay matched with frame 7765 total `6a609a6637cce6ba` and sim tick 25350.
It then failed a 250-frame replay at frame 179 `.data`, so the pass was not
stable. Windows passed 4 of 5 full 7766 runs, then failed at frame 201
`phys_12`. In 20 runs at 250 frames it failed 2: frame 176 `.data` (one fewer
QPC read) and frame 222 `phys_12`.

A second variant also pre-registered a WaitTicks sleeper whose deadline the
present had just reached. Mac fell to 17 of 20, with `.data` failures at
frames 145 and 175, so that variant was not kept. The expected order is not
simply the deadline order.

Do not retry pre-registration alone. The remaining `.data` miss is still the
first clock read, guest thread 40 at `0x0006A00A` against thread 44 at
`0x0006AC0C`. The ticket order assigned in `BlockGuest` may itself differ
across hosts, which pre-registration cannot repair. The `phys_12` difference
is three bytes, each one higher on the Mac, at physical `0x80CDFEFC`,
`0x80CDFF04`, and `0x80CE15B0`; the first two and the table at the third
read like one counter pair plus a queue or free-list entry.

Reopen when a scheduler change makes the first clock read deterministic by
guest identity or priority rather than by host block order, or when the
`phys_12` counter is explained.

### 2026-11-08: Guest-id order

No commit. Reverted. Ordering waiters by the guest `Thread::id` in
`LowestTicketLocked` moved Mac hashes: 18 of 20 replays at 250 frames matched,
with `.data` failures at frames 163 and 179 (one QPC read short). The expect
log follows the ticket order, so guest-id order is not a drop-in replacement.
The host scheduler does not use the guest priority field today. A fixed order
needs either a rule that reproduces the existing log or a fresh recording.

Reopen when a fixed order is derived from the guest's own scheduling state, or
when a new expect log is recorded for that order.

### 2026-11-08: The block-order flip and the id-order deadlock

No commit. Reverted. Trace only, then the id-order variant was stopped.

The earlier guest-id test was invalid. `t_sw.guest_id` was set when the thread
first registered, and `kernel::Current()` is often null at that point, so every
waiter compared as id 0 and fell back to tickets. Refreshing the id in
`AcquireTurn` and `BlockGuest` showed the real order.

Mac frame 176: waiters 40 (ticket 583), 28 (584), 44 (585). Thread 40 read
QPC first. Windows frame 178, the failing frame: waiters 44 (602), 40 (603),
28 (604). Thread 44 read QPC first and advanced the tick four times. The Mac
order is 40, 28, 44. The order flips in the later waves of frame 177. Tickets
are assigned when each thread blocks, so host wake timing changes the block
order even though the guest code is the same.

With the real id used as the first key, Mac matched 20 of 20 against a new
250-frame baseline. The new baseline had sim tick 4400, not 4396, so the old
expect log is tied to ticket order. On Windows, run 5 of 40 hung at frame 60.
Id order overrides the guest's block order and can deadlock.

Do not retry id order. The next step is to make the block order deterministic
without replacing it. The second and later waves are the lead: every waiter
shares `g_cv`, and the ticket is assigned at `BlockGuest`, so the object wait
registration order is not modeled. A per-object wait list, or a wake sequence
that orders the runnable set before it is served, is the likely fix.

Reopen when the wake order of threads signaled in the same frame is modeled
and both hosts match a fresh baseline.

### 2026-11-08: Pinned wait ticket

No commit. Reverted. `WaitForFixed` kept one ticket across every re-park
through `BeginWait` and `EndWait`. A new Mac baseline at 250 frames had sim
tick 4357, and Mac matched 20 of 20 against it. Windows matched 40 of 40 at
250 frames against the same baseline. The full Mac 7766 baseline had sim tick
25076 and frame 7765 total `8e58fe8335c7a76d`; a second Mac run matched it.
One Windows 7766 run passed.

The 40x7766 run then hung on run 2 at frame 5887. The pinned ticket can keep
a low ticket on a waiter that depends on a thread with a higher ticket, and
the presenter waits forever. The 40x250 gate passed before the hang, so a
short gate cannot clear this class. Reverted. `patches/sdk-lockstep-wait-pin.patch`
and its apply hook were withdrawn.

The next scheduler design must release or reorder a wait ticket when the wait
depends on another guest thread, or track the dependency, not just pin the
entry order. A Windows gate now needs one full 7766 run before the 40x loop.

Reopen when a wait-order policy handles dependencies and both hosts match a
fresh baseline through 7766.

### 2026-11-08: Event ready queue, phase 1

No commit. Reverted. Added a host wait record on each waited object in
`WaitForFixed`, registered in guest order, and marked satisfied waiters with a
ready sequence when `SetEvent` fired. The scheduler served ready waiters ahead
of the ticket order. Only events were covered.

Mac matched 18 of 20 at 250 frames against the existing baseline. The two
failures were frame 130 `phys_12` and frame 157 `.data`, the same shapes as
before. No Windows run was made. The event wait order alone does not explain
the flake, so phase 2 (all wait types) is not justified yet.

Reverted. The ready-queue work in `objects.cpp`, `kernel.h`, and
`determinism.cpp` is gone.

Reopen when a trace of a failing frame shows an event waiter out of order, or
when all wait types can be modeled in one change.

### 2026-11-08: Event ready queue with pulse

No commit. Reverted. The event ready queue was retried with `PulseEvent`
also marking satisfied waiters. Mac matched 20 of 20 once, then 18 of 40.
The failure was frame 173 `.data`.

The in-memory wait-state dump at that failure showed threads 28, 36, 40, and
44 waiting on `ObjType::Event` (mask `00000001`). Thread 20 was on an I/O
completion port (mask `00000010`). `SetEvent` and `PulseEvent` both mark
event waiters in registration order, so the missing pulse hook is not the
whole cause. The signal may arrive through `SetSignal` from a timer path, or
the guest may write the dispatcher signal directly, or the registration order
is not the order the guest expects.

Reverted. The diagnostic confirmed the failing threads wait on events, but
not which signal path moves them. The next trace should tag each signal by
source (`KeSetEvent`, `KePulseEvent`, timer, direct `SetSignal`) and log the
ready sequence for the event object.

Reopen when the signal source is known and the event order can be tied to it.

### 2026-11-08: Signal-source trace

No commit. Reverted. The trace recorded the last 128 signals, tagged by source,
and the last wait state per guest thread. It also compared the guest dispatcher
signal word with the host object's signal before each block.

Mac failed at frame 172 `.data`. The wait state showed threads 28, 36, 40, and
44 on events and thread 20 on an I/O completion port. There were no direct
guest writes (`GUESTSIG` was empty). The signal ring at that frame was almost
all mutant releases and consumes; no `set` or `pulse` for the event objects
the waiting threads were on.

So the event wait order is not the order-critical path at this frame. The
mutant and semaphore waits are. The event-only ready queue could not fix it.

Reverted. The next model should cover mutants and semaphores first, then
events. The signal-source trace and wait-state dump are not kept.

Reopen when the mutant and semaphore wait order is modeled, or when a trace
shows a different signal source.

### 2026-11-08: Mutant and semaphore wait order

No commit. Reverted. The ready queue was extended to mutants and semaphores:
one mutant waiter marked on `ReleaseMutant`, up to the count on
`ReleaseSemaphore`, and events as before. A wait record kept its place across
re-parks.

Mac failed at frame 165 `.data` on run 10 of 40, one QPC read short. The same
class of failure survived the event queue, the pin, and guest-id order. Wait
order is not the remaining cause for this miss.

The clock path is. `Hold` advances the sim tick when two QPC reads see the
same tick, so which thread reads first decides which thread skips the
`0x0006A00A` sample. Wait-order changes move who reads first but do not remove
the read-count difference.

Reverted. Do not retry wait-order variants for this `.data` miss. The next
change should make the QPC tick independent of read order, or trace the exact
read that advances the tick at the first differing frame.

Reopen when a clock change is ready to test, or when a trace shows a wait-order
cause that is not the QPC count.

### 2026-11-08: Per-thread QPC clock

No commit. Reverted. `ClockQpc` kept its last value and have flag per guest
thread. A first read by a thread returned the tick; only that thread's repeat
advanced it. The snapshot TLS image carried the per-thread state and the
snapshot version went to 3.

The change moved the baseline: frame 249 total `39df123dec811dd1`, sim tick
4094, qpc 4405 at 250 frames. Against that fresh baseline Mac failed on run 4
of 40 at frame 136 `phys_12`, cause still including a QPC read-count
difference.

So the host-dependent read count is not only which thread reads first. Wait
order and clock ownership have both been tried and neither makes Mac
deterministic. Reverted; snapshot version back to 2; no per-thread QPC state.

The next design has to make the guest-visible clock and the guest run order
deterministic together, not adjust either heuristic alone.

Reopen when a scheduler and clock model can be tested as one change.

### 2026-11-08: Run queue ordering, candidate

No commit. The run queue is wired behind `FUZION_RUN_QUEUE=1`. The key is
guest priority, then guest thread id, then ready sequence. `LowestTicketLocked`
uses the queue when the flag is on; the ticket path stays the default.

Mac matched 40 of 40 at 250 frames against a fresh baseline, and the full
7766 matched a second run: sim tick 25383, frame 7765 total
`fac2e181df2256fa`.

Windows matched 40 of 40 at 250 frames against that baseline, and one full
7766 run matched it. This is the first full cross-host match.

The 40x7766 loop was stopped after run 1 showed about one hour per full run,
or roughly forty hours for the gate. The 40x250 gate and the single full run
passed. The candidate is not patched or committed; the new baselines are
local `rq-250.detlog` and `rq-7766.detlog`.

Reopen when the full-run gate is reduced or scheduled, or when the queue path
shows a failure.

### 2026-11-08: Run queue accepted

Committed with this change. The queue path is behind `FUZION_RUN_QUEUE=1` and
is not the default. Five full Windows 7766 runs passed against the new
baseline, after the 40x250 gate. Mac passed 40x250 and two full 7766 runs.

The new baseline is `rq-250.detlog` and `rq-7766.detlog`: sim tick 25383,
frame 7765 total `fac2e181df2256fa`. The old `gameplay2.detlog` remains the
default-path baseline and is not retired.

Reopen if a host fails the queue path, or when the flag becomes the default.

### 2026-11-11: Release archives carry the player toolchain

No commit yet. The published launcher compiles the player's USA disc on
their machine. The archive carries CMake, Ninja, the pinned SDL, imgui,
zydis, and tomlplusplus sources, and the libraries that compile used to
find on the system. Mac still asks for Xcode Command Line Tools. Linux
still uses the machine's Vulkan driver. Windows downloads the Windows SDK
once, with xwin, into %LOCALAPPDATA%\RingHost\winsdk, because that SDK
cannot be redistributed in the zip.

Developer builds are unchanged when RINGHOST_TOOLCHAIN is unset.

Reopen this if a release must ship a prebuilt game binary, or if the
Windows SDK download is replaced.

## 2026-11-12: Short code and a list of open games

The 2026-11-11 relay stays the meeting point. Both games send outbound
UDP, so neither player forwards a port. The relay host is the reachable
address, from `FUZION_RELAY` or the empty built-in constant.

A one-byte Open stays a private unnamed session. A longer Open carries a
public flag and a name. The server derives a 6 character code from the
token. Resolve returns that token. List returns public games that have
not started and were heard from in the last 20 seconds. The host
heartbeats every 5 seconds. The F1 panel hosts, joins by code or from
the list, and Play online restarts with `--lockstep`. No accounts.
Plaintext. Steam stays out. The Windows relay client stays a stub.

Reopen when the built-in host is set, or when outbound UDP is not enough.

## 2026-11-12: Restart both games when the host starts

The lobby was already simulating, so Start compared two different RAMs.
The host's Start now sends Restart. The relay drops the seats and, when
the same number have rejoined, sends the real Start. Each game relaunches
and holds its first present until that notice. Save folders still have to
match. `--instance 2` uses `hdd2`.

Reopen if a restart leaves the two first presents on different frames.

## 2026-11-12: The host can start the relay

Host online, on macOS and Linux, spawns `relay_server` beside the game on port 0, asks the router to map that UDP port (UPnP IGD, then NAT-PMP), and copies `ff1:<public-ip>:<port>:<32 hex>`. The host's own Open uses `127.0.0.1` so the Start relaunch does not need hairpin NAT. A private or CGNAT address (`10/8`, `172.16/12`, `192.168/16`, `100.64/10`, loopback) stops the relay and the panel says internet play is not available. If the router does not open the port, the panel says so and no relay is left running. Host against a relay you started yourself, including `127.0.0.1:9999`, does not start one. There is no shared game list on this path. The other player pastes the full link. Windows stays a stub.

The process exits through `std::_Exit`, which skips `atexit`, and `execv` keeps children and the environment. `relay_guard` watches the host pid and removes the mapping when that pid is gone. The environment still carries the pid and the mapping so the relaunched image can refresh the lease.

Reopen when a public relay hostname is chosen, or when a router the host uses cannot map a UDP port and a relay elsewhere should be the fallback.

## 2026-11-12: PCP when UPnP and NAT-PMP are silent

America/Los_Angeles. Host online tries PCP (RFC 6887, version 2 on UDP 5351) only after UPnP IGD fails and NAT-PMP (version 0 on that same port) gets no reply. A PCP MAP uses the external address and mapped port the same way. A loopback, private, link-local, multicast, or CGNAT address (`10/8`, `172.16/12`, `192.168/16`, `100.64/10`) still stops the relay. The guard deletes that mapping with a MAP lifetime of 0. The one hour lease, renewed while the host is up, is the fallback if the delete is lost.

Reopen if a delete with lifetime 0 leaves the port open, or if a router that speaks PCP needs a different external address encoding than an IPv4-mapped address.
