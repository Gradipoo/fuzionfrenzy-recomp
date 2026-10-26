# Cold start (26 Oct 2026, Pacific)

A new agent can start from this file. The procedure is
`docs/agent-workflow.md` (one task per agent, what to update here, how to
verify, how to report). Settled choices are `docs/decisions.md`. The section
below the rule is the macOS GPU and launcher state. Battery numbers and the
MoltenVK flush notes there are still the open GPU work. Determinism, replays,
and the branch map are here.

USA Xbox Fuzion Frenzy, XDK 3925, title `0x4D530002`. Static recomp. SDK submodule
`sdk/` is `abilliontokens/XboxXBERecomp` @ `58757ab` (`fuzion-frenzy-port`). The XBE
never leaves this Mac. SHA-1 of `default.xbe` is `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad`.

## Latest branch

`cursor/linux-det-c2dc` is the latest line. Review base is
`cursor/fixed-step-pace-c2dc` @ `92002fe`. Parent of that is `cursor/xbox-one-usb-c2dc`
@ `36d66fb`.

The gameplay2 write-up is `fc4730d` ("Record the x86_64 gameplay2 match and
the boot-thread exit."). The cold-start handoff is `09b7843`. The agent
workflow (`docs/agent-workflow.md`), the decisions log (`docs/decisions.md`),
and `AGENTS.md` are the commit on top of that. `git log -1` on this branch
is the hash to report.

No pull request was opened. `ManagePullRequest` `create_pr` returned unauthenticated.
Compare:

https://github.com/Gradipoo/fuzionfrenzy-recomp/compare/cursor/fixed-step-pace-c2dc...cursor/linux-det-c2dc

Do not open that PR with `gh`. `gh` is logged in as Gradipoo and is for viewing CI
only (`gh run list`, `gh run view`).

Commits on this line are authored `port <port@local>` by setting `GIT_AUTHOR_NAME`,
`GIT_AUTHOR_EMAIL`, `GIT_COMMITTER_NAME`, and `GIT_COMMITTER_EMAIL` for that one
command. Leave git config alone. New branches are `cursor/<descriptive-name>-c2dc`,
lowercase. Push with `git push -u origin <branch>`.

Code CI on `8641ec8` ("Keep the turn only for the boot thread's exit signal.") was
green: GitHub Actions run `40403236620`, ubuntu and macos-arm64, about 5m21s, started
2026-10-26 14:18 PT. `fc4730d` is docs on top of that and was not rechecked. The
workflow is `.github/workflows/ci.yml` (ubuntu-latest with valgrind math digest, and
macos-14). It runs `tools/hooks/forbid-game-paths.sh`, `scripts/fp-digest-host.sh`,
`scripts/det-diff.py --self-check`, the SDK host tools, the SDK runtime
(`XBE_CI_RUNTIME`), and on macOS a `swiftc` of the launcher. There is no Windows job
and no ctest job.

Other local branches, left where they are. Do not merge the screenshot, Tracy,
Ghidra, ASan, or netplay lines into `linux-det`:

| Branch | Tip | What it is |
| --- | --- | --- |
| `cursor/macos-arm64-c2dc` | `9f3a1b5` | Preferred base for new unrelated cloud work. Its PR base is `main`. |
| `cursor/unbound-blendtexturealpha-c2dc` | `4ccef40` | Twisted System white floor marked resolved. |
| `cursor/netplay-determinism-c2dc` | `fcb9d0b` | Port-2 keyboard and seeded mash. Networking still off. |
| `cursor/xbox-one-usb-c2dc` | `36d66fb` | Present yield so fixed-step loads run. |
| `cursor/fixed-step-pace-c2dc` | `92002fe` | 60 Hz pace. Review base for this line. |
| `cursor/linux-det-c2dc` | this branch | Cross-host gameplay2 match. Latest. |
| `cursor/re-ghidra-unicorn-c2dc` | `8391a81` | Ghidra and Unicorn clock/RNG checks. |
| `cursor/macos-asan-c2dc` | `145a6c5` | openlibm UBSan. |
| `cursor/linux-ci-c2dc` | `825c8b4` | CI without a disc image. |
| `cursor/screenshot-regression-c2dc` | `af1a573` | Local screenshot harness. |
| `cursor/screenshot-scenes-c2dc` | `ffa5453` | Scene present indices. Sumo Balls is present 3400 there. |
| `cursor/tracy-profiling-c2dc` | `9a08dba` | Tracy, default off. |
| `main` | `071dbd9` | Scripted pad exercised. |

`git status` showing `m sdk` is the patched worktree. The submodule pointer stays
`58757ab`. Do not commit a new gitlink.

## Determinism status

`gameplay2` is 7766 presents, frames 0 through 7765. Seed 1. Exit sim tick 25350 on
Mac, arm64 Linux, and x86_64 Linux. Frame 0 total `eefbcee7c7938e05`. Frame 7765
total `6a609a6637cce6ba` (data `718a3318967bbdb6`, phys `c1847c5d98bd1b3d`, system
`3b3a3264539f44a3`, user `d975f073f2e01c53`, seed `75d308b5`, qpc 18174, rdtsc
108099, delay 32). `gameplay2.pads` stays the recording to replay.

| Host | Result |
| --- | --- |
| Mac arm64 `RingHost` | Two replays after the boot-thread fix, `/tmp/mac-g2-d.detlog` and `/tmp/mac-g2-e.detlog`, each `PASS 7766 frames match` against `det-logs/gameplay2.detlog` and against each other. Earlier `/tmp/mac-g2-a.detlog` matches the same expect log. Exit 0, sim tick 25350. |
| Lima `ff-det` (vz, aarch64) | `/tmp/linux-g2.detlog` (guest `~/linux-g2.detlog`) `PASS 7766` against the expect log and against the Mac replay. Idle pads, 240 frames, also match: frame 0 `574c0f97185e0166`, frame 239 `784ca3be84e7aa6c`. |
| Lima `ff-det-x86` (qemu, x86_64) | `/tmp/linux-x86-g2.detlog` (guest `~/linux-x86-g2.detlog`) `PASS 7766` against the expect log, the Mac replay, and the arm64 replay. Exit 0, sim tick 25350. Wall time under TCG was on the order of 40 minutes. |
| Windows | No comparison. The `windows-cross` preset builds on Linux with an xwin sysroot. The exe has to run on Windows. This Mac cannot produce that run. |

Two host bugs were fixed on this branch. Details live in `docs/netplay-determinism.md`.

- Frame 5 (arm64, before `84c3a10`). Guest `0x002CC1FC` is the XInput packet dword written by HLE `XInputGetState` (`sub_0024D2AE`, strong symbol from `SYM_XInputGetState`). `memcmp` of the whole `input::Pad` also compared the padding byte after `connected`. `XBE_XINPUT_PACKET` advances the packet when a report field changes (`connected`, `buttons`, eight analog bytes, `lx`/`ly`/`rx`/`ry`).
- Frame 0 (x86_64, before `8641ec8`). System dword `0xB0000404` is the boot thread's ETHREAD signal (`SetSignal` / `MarkExited`). `CallGuest` dropped the turn before that store, and QEMU hashed the clear signal. `XBE_THREAD_EXIT` keeps the turn only for that boot return. Holding every return dropped one QPC read and moved Mac frame 524. Leave later threads on the old path.

Fixed-step clocks that already match, and that a later change has to keep: one sim tick per present; QPC still advances on a repeated read; RDTSC and the interrupt use the present tick; `Pace()` runs under fixed step; draws are batched and `DrainFixed` runs on fence, busy, and readback; present overlaps one frame. Lock order: never take `g_time_mu` while holding `g_sched_mu`. `dsound::AdvanceFixed` is the only mixer-cursor advance under fixed step. The SDL callback does not move voice cursors in fixed step. Under fixed step, `QueueDpc` enqueues and `DrainFixedDpcs` runs at the start of `FireDueTimers` and inside `WaitForFixed`. Normal play still starts the host DPC thread.

A non-fixed-step Mac launch (`FUZION_FRAMES=30`, fixed step and replay unset) exited 0 at sim tick 0 before the thread-exit change. The hold is fixed-step only, so that path is the same code. It was not re-run after `8641ec8`.

## Where the files live

All of these are local. None of them belong in git.

- HDD snapshot: `/tmp/ff-hdd-sean` (dirs `C E X Y Z`, mtime 25 Oct 2026 19:12 PT). `Z/debugLog.txt` sha256 `f0311dc1984fa6fcddd3c52578acf8c8f8091af358e2b1d53e809532438c4490`. Host `/tmp` is not mounted in Lima. Each guest has its own copy at `/tmp/ff-hdd-sean`. Restore that into the build's `hdd/` before every replay. Copying the live Mac `hdd/` while a replay is writing it mixes two runs.
- Recordings: `/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.pads` and `gameplay2.detlog` (the 7766-frame expect). Also `idle.pads`, `gameplay.pads`, and older matrix logs. Gitignored.
- Mac binary and settings: `out/build/macos-release/RingHost`, `settings.toml` beside it (`display_mode = "windowed"`, 1280×720, `ultrawide = true`, `vsync = true`, `fps_cap = 60`).
- Lima guests (both Running when this was written; `limactl list` for the SSH port): `ff-det` vz aarch64, 6 CPU, 8 GiB, `~/.lima/ff-det`. `ff-det-x86` qemu x86_64, 4 CPU, 8 GiB, 100 GiB disk, `~/.lima/ff-det-x86`. Guest user `gsrvm3`, sudo -n works. Home is mounted read-only at the Mac path `/Users/gsrvm3/...`, so the pads and the expect log are readable there. Guest source tree is `/home/gsrvm3.guest/ff-src` (excludes `out/`, `.git/`, `det-logs/`). Binary `~/ff-src/out/build/linux-release/FuzionFrenzy`. `settings.toml` is already beside that binary on both guests. x86 also has `~/ff-replay/settings.toml`.
- Finished logs, for diffing, all 11293173 bytes: `/tmp/mac-g2-d.detlog`, `/tmp/mac-g2-e.detlog`, `/tmp/linux-g2.detlog`, `/tmp/linux-x86-g2.detlog`.
- `lima-additional-guestagents` 2.2.1 is installed (that brew also pulled qemu and pointed Homebrew's generic `openssl` symlink at 4.1.0). `openssl@3` remains at `/opt/homebrew/opt/openssl@3`. tmux is not installed; background long commands in the shell tool.

## Mac replay

Working directory `out/build/macos-release`. No `--instance`. Leave `FUZION_AUTOSTART`, `FUZION_MINIMIZED`, and `FUZION_WATCH_VA` unset. Expect exit 0, sim tick 25350, and `PASS 7766 frames match`.

```
cd /Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/out/build/macos-release
rsync -a --delete /tmp/ff-hdd-sean/ hdd/
find hdd \( -name '._*' -o -name '.DS_Store' \) -delete
unset FUZION_AUTOSTART FUZION_MINIMIZED FUZION_WATCH_VA
XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 FUZION_FRAMES=7766 \
  FUZION_REPLAY_INPUT=/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.pads \
  FUZION_STATE_EXPECT=/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.detlog \
  FUZION_STATE_LOG=/tmp/mac-g2-next.detlog \
  ./RingHost
python3 /Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/scripts/det-diff.py \
  /Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.detlog \
  /tmp/mac-g2-next.detlog
```

Rebuild from the repo root with `./build.sh macos-release` (or ninja in that build dir) before replaying a tree that changed. `ld` warns about a duplicate `-lpthread`; that warning was already there.

## Lima replays

`limactl shell ff-det` for arm64, `limactl shell ff-det-x86` for x86_64. Inside the guest, from `~/ff-src/out/build/linux-release`. Restore the guest's `/tmp/ff-hdd-sean`, then delete AppleDouble files. `set +e` before `xvfb-run`, then `echo EXIT:$?`. `set -e` together with `xvfb-run` swallows the game log. In zsh on the Mac, `=cmd` is equals-expansion, so a banner like `echo ===COPY===` breaks; use a different banner.

```
cd ~/ff-src/out/build/linux-release
rsync -a --delete /tmp/ff-hdd-sean/ hdd/
find hdd \( -name '._*' -o -name '.DS_Store' \) -delete
set +e
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NO_RAST=1 \
xvfb-run -a env -u FUZION_AUTOSTART -u FUZION_MINIMIZED -u FUZION_WATCH_VA \
  XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 FUZION_FRAMES=7766 \
  FUZION_REPLAY_INPUT=/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.pads \
  FUZION_STATE_EXPECT=/Users/gsrvm3/Documents/projects/fuzionfrenzy-recomp/det-logs/gameplay2.detlog \
  FUZION_STATE_LOG=$HOME/linux-g2-next.detlog \
  ./FuzionFrenzy
echo EXIT:$?
```

Same command in `ff-det-x86`. Pixels are not in the hash. ALSA/pipewire failing to open is noise and did not desync. x86 is a checksum run, not a frame-rate test.

To refresh the guest tree, copy changed sources into `~/ff-src`, then `find ~/ff-src -name '._*' -delete`. `limactl copy` keeps mtime and can drop `._*` AppleDouble files that ninja then tries to compile (UTF-8 error). `touch` a copied `.cpp` when the preserved mtime is older than the build. The x86 binary on disk was rebuilt after `8641ec8` (26 Oct 2026 14:21 PT).

Start line, if the x86 guest is gone:

```
limactl start --yes --name ff-det-x86 --arch=x86_64 --cpus 4 --memory 8 --disk 100 --timeout 45m template:ubuntu
```

`brew install lima-additional-guestagents` is what provides the non-native guest agent. This host has 24 GB of RAM. Both 8 GiB guests fit. `kernel.perf_event_paranoid` was set to 1 inside a guest for the watch (`sudo -n sysctl`). The game must keep working when that sysctl is left alone.

Idle check is the same binary with `det-logs/idle.pads`, `FUZION_FRAMES=240`, `FUZION_MINIMIZED=1`, and a fresh `hdd2` (`--instance 2`). `scripts/det-matrix.sh` from the repo root is the Mac matrix (muted, minimized, fixed-step, seed 1, `FUZION_AUTOSTART` unset, default 240 frames / 180 s). Overrides: `FUZION_DET_FRAMES`, `FUZION_DET_TIMEOUT`, `FUZION_DET_BUILD`.

## Env vars

CLI names are in `docs/netplay-determinism.md`. The harness also reads the variables directly.

| Variable | Role |
| --- | --- |
| `FUZION_FIXED_STEP` | One sim tick per present. `0` or unset leaves it off. |
| `FUZION_RECORD_INPUT` | Append each port's latched pad per present. |
| `FUZION_REPLAY_INPUT` | Feed that file. Real pads, the keyboard, and `FUZION_AUTOSTART` are ignored. Past the last frame the last sample is held. |
| `FUZION_SEED` | Written into the rand global once, at image load (`strtoul` base 0). |
| `FUZION_STATE_LOG` | Per-present checksum log. |
| `FUZION_STATE_EXPECT` | In-process compare. On mismatch, writes `det-desync/desync_N.txt` and `frame_N_region.bin`, then `Shutdown(1)`. |
| `FUZION_FRAMES` | Exit 0 after N presents. |
| `FUZION_STALL_MS` | Sleep at the start of each present, before the hash. |
| `FUZION_MINIMIZED` | Show the window, then minimize it. Leave unset for the gameplay2 match. |
| `FUZION_STATE_DUMP` | Directory for `frame_N_region.bin`. `DumpRegion` does not create the directory. |
| `FUZION_STATE_DUMP_FRAME` | Which present to dump. |
| `FUZION_STATE_DUMP_REGION` | `phys_03`, `.data`, `system`, `user_0`, or a section name. Empty dumps the writable sections and the 64 physical slices. |
| `FUZION_PORT2_KEYS` | Move the keyboard map from port 1 onto port 2. Off when unset, empty, or `0`. |
| `FUZION_PORT2_MASH` | Seeded port-2 pattern. Off when unset, empty, or `0`. |
| `FUZION_AUTOSTART` | Scripted pad. Leave unset for recordings and for gameplay2 replays. `FUZION_AUTOSTART_PLAY_MS` stays 75000. |
| `FUZION_WATCH_VA` | Guest VA in hex. The watch is off unless this is set. Leave it unset for a matching replay. |
| `FUZION_WATCH_LOG` | Optional watch log path. |
| `FUZION_WATCH_SCAN` | Set `0` to skip the scan. |
| `XBE_MUTE` | Silence. |
| `LP_NO_RAST` | Lavapipe skips raster. Set `1` on Linux replay. Pair with `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`. |
| `XBE_CAPTURE_DIR`, `XBE_VK_CAPTURE_EVERY` | Darwin present copy-out. Off unless both say to capture. |

`scripts/det-diff.py` compares two logs and names the first mismatched region. `--self-check` is what CI runs.

## Gotchas

`LP_NO_RAST=1` plus the lavapipe ICD is the Linux replay setup that matched. The hash covers guest RAM. Pixels stay out of it.

`FUZION_WATCH_VA` is a one-dword hardware watch, off unless the variable is set. One relaxed atomic in `EnterGuest`, and only when fixed step is on. Do not wrap `X86_MEM32`. Linux uses `perf_event_open` (`PERF_TYPE_BREAKPOINT`, `HW_BREAKPOINT_W`, length 4) with `sigtrap=1` and `remove_on_exec=1` (without `remove_on_exec`, errno 22). The trap is before the store: disable the data watch, plant an exec breakpoint at `pc+4`, log the new value on that trap, re-enable. `WatchRearm` runs after 8 ms in phase 1. Ubuntu 24.04 glibc (CI, `-std=gnu++20`) has no `TRAP_PERF`; the watch patch defines it as 6 when the header omits it. Ubuntu 26.04 has the macro. The Mac single-step path sticks (`watch stuck pc=...`) and disarms that thread. Use the Linux perf watch. A pre-log line is tagged `tag=pre` and includes `sia=`. Watch `0x002CC1FC` for the packet. Watch `0xB01018E8` and the hits are libc `memset` zeros, not the packet writer.

`pipe2` is absent from the macOS 14 SDK. `patches/sdk-macos-pipe.patch` (marker `XBE_MACOS_PIPE` in `boot.cpp`) supplies the call. `tools/apply-macos-sdk.sh` applies, in order, `patches/sdk-macos-arm64.patch` (marker `XBE_DARWIN_GUEST`), `patches/sdk-macos-gamepad-events.patch` (`XBE_GAMEPAD_WATCH`), then the pipe patch. CI commit `9602de9` is the one that made the macOS 14 runtime compile. Darwin-only. Linux never runs that script.

`tools/apply-lockstep.cmake` runs on every host, after the Apple script on Darwin. Idempotent, by marker. Order:

1. `patches/sdk-lockstep.patch` (`XBE_LOCKSTEP_FP`)
2. `patches/sdk-lockstep-host.patch` on Linux/Windows only, when `determinism.cpp` is missing. It already contains `XBE_LOCKSTEP_HARNESS` and `XBE_LOAD_YIELD`.
3. `patches/sdk-lockstep-harness.patch` when the host patch did not supply the marker
4. `patches/sdk-lockstep-load-yield.patch` (`XBE_LOAD_YIELD`)
5. `patches/sdk-lockstep-input-dpc.patch` (`XBE_INPUT_DPC`)
6. `patches/sdk-lockstep-watch.patch` (`XBE_WATCH_VA`)
7. `patches/sdk-lockstep-thread-exit.patch` (`XBE_THREAD_EXIT`)
8. `patches/sdk-lockstep-xinput-packet.patch` (`XBE_XINPUT_PACKET`)
9. Memory: `patches/sdk-lockstep-memory-apple.patch` when `memory.cpp` contains `HostProtect`, otherwise `patches/sdk-lockstep-memory.patch` (`XBE_LOCKSTEP_COMMIT`)

The dirty Mac `sdk/runtime/core/determinism.cpp` also has `#include "core/trace.h"` and `xbe_trace::BeginGameTick(); // XBE_TRACY_ZONES`. Those lines are the Tracy worktree from `cursor/tracy-profiling-c2dc`. The Linux tree has no `trace.h`. A patch generated from the Linux-shaped file applies on both bases when the hunk context stays off those two lines. Do not put Tracy into `patches/`.

`CheckExpect` reports the first of data, phys, system, user. The hash mixes with `(h ^ CanonWord(v)) * 0x100000001B3`. NaN payloads are canonicalized in the hash only. Guest system RAM starts at `mem::kSystemBase = 0xB0000000`. The first thread's KPCR is `0xB0000000` and its ETHREAD is `0xB0000400`. A store logged with `frame=N` happened while `g_frame` was already N, which includes `YieldGuest` of the present that hashed frame N-1.

`HLE_STDCALL` / `GUEST(name)` emits a strong `sub_<SYM_name>` and overrides the weak recompiled body. `GUEST_ORIG` is the `__imp__sub_` import. A future "the USB function did not run" chase should check `generated/game/image_symbols.h` for a matching `SYM_` before blaming the recompiler. That generated tree is local and stays uncommitted. On the x86 guest it matched the Mac tree for the shared header, `image_symbols.h`, the func mapping, and recomp units 0 and 62.

## No game content

`tools/hooks/forbid-game-paths.sh` (pre-commit, and the ubuntu CI job on the diff) refuses:

- `sdk` and `sdk/*` (no submodule content, no gitlink change)
- `assets`, `generated`, `out`, `build`, `logs`, `hdd`, `CrashDumps`, `shadercache`, `tools/bin`, `.toolchain`, `macos/launcher/dist`
- `*.xbe`, `*.iso`, `*.xiso`, `*.dmp`, `*.d3dcap`, `*.pdb`, `*.exe`, `*.dmg`
- `det-logs`, `det-desync`, `*.pads`, `*.detlog`, `desync_*.txt`, `frame_*_*.bin`
- `re/`, `*.gpr`, `*.rep`, `*.gzf`, `time-rng-map.txt`
- any path containing `x86_recomp`, `x86_func_mapping`, or `image_symbols.h`

Also leave untracked `.DS_Store` unstaged. `--self-test` checks the rules. The XBE, the HDD snapshot, pad files, state logs, and region dumps stay on this machine. Nothing game-derived is committed, pushed, or uploaded.

## Open items

- Save-state replays. Not started. Today a replay boots from the HDD snapshot and feeds `gameplay2.pads`. `FUZION_STATE_DUMP` writes one region's bin for one frame and does not resume from it.
- A ctest suite. Not started. CI is the workflow above plus `scripts/det-diff.py --self-check`. `scripts/det-matrix.sh` is the local Mac matrix.
- ccache. Not wired into the presets or the workflow.
- Windows CI. No Windows job. `cmake --preset windows-cross` is documented at the bottom of `docs/netplay-determinism.md` and still has to run on Windows before an x86_64 Windows hash exists.
- Networking later. There is still no socket. Leave it off. The gameplay2 hash now matches across Mac arm64, Linux arm64, and Linux x86_64, which was the prerequisite. `docs/netplay-determinism.md` is the design note.
- Sumo glitches. No root-cause writeup on this branch. Sumo Balls is a recorded scene: present 3400 in `tools/shots/scenes.txt` on `cursor/screenshot-scenes-c2dc` (title 200, board 1200, character select 1400, Twisted System countdown 1800, Twisted System round 2200, Jetstream 5000). White characters and the white Twisted System floor are resolved in `docs/render-defect.md`. Scene indices stay on the screenshot branch.
- Battery. Measured on battery during the 60 fps heavy scene: 11.168 V, −1231 mA, 13.7 W (`BatteryPower` −13747 mW), process CPU about 76% of one core, RSS about 195 MB. `powermetrics` needs root and `sudo -n` is unavailable, so there is no package or GPU watt split. The 4096-draw flush still waits inside a heavy frame. Do not retry a non-blocking slot rotation or a timeline submit on MoltenVK 1.4.2. The rest of that note is the next section.

# macOS GPU and launcher

Native Apple Silicon build of this static recompilation. No game files belong in git.
`assets/`, `generated/`, and `out/` stay on the machine that owns the disc.

The cold start above is the current branch, the replays, and the open determinism work.
This section is the GPU and launcher state. Battery numbers and the MoltenVK flush
notes below are still the open GPU work.

## Approach

Stay on this repository's XboxXBERecomp SDK (`sdk/`, submodule
`abilliontokens/XboxXBERecomp` @ `58757ab`, branch `fuzion-frenzy-port`). Do not switch the
game onto [DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp).

Why:

- The recompiler already emits portable C++. Guest memory is `base + offset`, not fixed host
  addresses, and x87/SSE are scalar C++ on an `X86Context`. An arm64 port is a platform layer,
  not a new lifter.
- Linux (Vulkan + SDL3, playable) and Windows (D3D12 + XAudio2) keep compiling the submodule
  exactly as it is pinned. The macOS delta is `patches/sdk-macos-arm64.patch`, applied only when
  CMake or `./build.sh` runs on Darwin.
- DirectXbox-Recomp is a different pipeline (C, not this C++ runtime) and is GPL-3.0. Moving the
  game onto it would drop the XDK 3925 symbol work, the hooks, and the Linux/Windows bring-up.
  Its `src/platform/posix_memory.c` was the reference for the Darwin memory behaviour and was
  not copied. See the README legal note.

`elpetese/black-macos-runtime` is the other xboxrecomp family (the C lifter), not this SDK.

## What the patch changes

Applied by `tools/apply-macos-sdk.sh` (also from the top of `CMakeLists.txt` on Apple, and from
`./build.sh` on Darwin). Idempotent; it looks for the `XBE_DARWIN_GUEST` marker.

- Guest window: 4 GB `PROT_NONE` reservation. Darwin will not honour the `0x100000000` hint; any
  address is fine because every access is `base + guest`.
- Physical RAM aliases (`0x80000000` and `0xF0000000`, 64 MB): one `shm_open` + immediate
  `shm_unlink`, two `MAP_FIXED` views. Darwin has no `memfd_create`.
- Host page size is 16 KB on this Mac. `mprotect` is per host page. A host page is writable if
  any 4 KB guest page on it is committed. Xbox reservations are 64 KB, so stack guards stay
  exact. A 4 KB commit can make the rest of its 16 KB host page writable. There is no fault
  handler to emulate the stricter protection (that is the part DirectXbox does and this port
  does not).
- `pthread_setname_np` is the one-argument Darwin call.
- Executable path is `_NSGetExecutablePath`, not `/proc/self/exe`.
- No `<cpuid.h>` / `<x86intrin.h>` / `<smmintrin.h>` on arm64. State compares use `memcmp`.
- `SYS_pidfd_open` is absent; relaunch still compiles. Abstract-namespace Unix sockets do not
  exist on Darwin, so image handover will not work. This title is one XBE.
- Vulkan: `VK_KHR_portability_enumeration` and `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`,
  device extension `VK_KHR_portability_subset`, and the first supported composite alpha
  (MoltenVK often lacks `OPAQUE`). Core `robustBufferAccess` is enabled when the device
  advertises it. `VK_EXT_robustness2` is image-only here (`robustBufferAccess2` and
  `nullDescriptor` are not advertised).
- MoltenVK copies of a BC mip use the image's mip extent. Rounding that copy up to a
  4-texel block makes the copy larger than the subresource, and MoltenVK reads off the
  allocation. Sampler LOD is clamped to the bound view's mip count, and the LOD bias is
  clamped to `maxSamplerLodBias`.
- The Vulkan pipeline bakes the vertex stride. The pipeline key includes each buffer's
  stride, so a later draw with a shorter stride does not reuse a pipeline that fetches
  at the longer one.
- On Apple, `Draw()` calls `Flush()` before draws 4097, 8193, and so on. One command
  buffer that records ~8500 draws faults a GPU read even when each draw is in range.
  The same draws split across submits do not. 4096 stays under the 8192-set segment
  and the upload ring (a heavy frame uploads about 2.5–3.2 MB). The flush waits.
  Handing that chunk to another command buffer without a CPU fence SIGSEGV'd in
  MoltenVK image-view encode (`MVKImageView` plane pointer `0xaa0003f3aa0103f4`,
  the same address on every run, from `vkQueueSubmit`). Metal argument buffers stay
  enabled.
- On Apple, `Pace()` sleeps the whole frame-cap wait. The non-Apple branch still
  sleeps in 1 ms steps and yield-spins the last 2 ms. `KeStallExecutionProcessor`
  sleeps the requested microseconds on Apple; the non-Apple branch still yield-spins.
  A zero stall stays a no-op. `KeDelayExecutionThread` already slept on every platform.
  `NtYieldExecution` is still a yield. Present is FIFO when vsync is on. The overlay
  frame-cap list already includes 30 fps.
- The swapchain requests `TRANSFER_SRC` when the device advertises it, so a present
  image can be copied out. The copy runs only when `XBE_CAPTURE_DIR` is set and
  `XBE_VK_CAPTURE_EVERY` is a non-zero interval. Those lines live in the Darwin patch.
- glslang's `setShiftBinding` is applied by `mapIO()`, which this path was not calling.
  Without it, HLSL registers are the bindings (`t0` and `s0` both binding 0).
  `mapIO()` puts textures at bindings 2..5 and samplers at 6..9, which is the
  descriptor set the draws already write. The SPIR-V cache key changed with that call.
  Distinct bindings are not enough for the old two-texture present shader: MoltenVK
  still emits both textures as `[[texture(0)]]` and both samplers as `[[sampler(0)]]`,
  and the Metal library does not compile. The present shader is now one texture and
  one sampler. The gamma ramp lives in the binding-0 uniform (192 `float4`s, the
  existing b0 range) and the blit constants sit after it in b1. The upload covers
  that ramp plus a full `DrawConstants`, which is the range the descriptor writes.
- Cocoa: `SDL_PumpEvents` is not called from the guest input thread. AppKit aborts if it is.
  The main thread already pumps. `SDL_PeepEvents` still drains gamepad add/remove.
- `ucontext.h` needs `_XOPEN_SOURCE` before it is included. `sysctlbyname`'s last argument is
  a `size_t`. `SOCK_CLOEXEC` and `accept4` are not available; the handover socket uses
  `accept` plus `FD_CLOEXEC`.
- `XBE_PERF` lines in the Vulkan backend are report-level, so a Release build actually prints
  them. Linux and Windows do not get this change; the patch is Darwin-only.
- Linker is Apple `ld64`. `clang -fuse-ld=lld` is rejected by the Command Line Tools clang.
  There are no strong `X86_FUNC` overrides in `src/game` yet; weak recompiled functions just
  need to link.
- Shaders: Homebrew glslang. DXC is used when `VULKAN_SDK` provides `dxc/dxcapi.h` and
  `libdxcompiler.dylib`.
- If `VK_DRIVER_FILES` is unset, `main` points the loader at Homebrew's MoltenVK ICD.
- Fixed-function stage 1 on the in-round Twisted System characters is color op 14,
  `BLENDTEXTUREALPHA` in this shader's numbering, with no texture and alpha op
  `SELECTARG1`. A missing texture is opaque white, so the blend replaced the colored
  stage-0 texel with flat white. The stage is skipped when it has no texture, the
  color op is 14, and color arg1 is that texture (no complement). An earlier guard
  required the alpha op to be disabled, so these draws never took it. Confirmed in
  play on 2026-10-25: characters stay textured through the countdown and the round,
  the floor that had been flat white is textured too, and the other scenes looked at
  in that session were unchanged. Cxbx would remap raw op 14 to `BLENDFACTORALPHA`
  on this XDK; that remap is not applied, because it would miss this skip. See
  `docs/render-defect.md`.

Presets: `macos-debug`, `macos-release`, `macos-relwithdebinfo`. `CMAKE_OSX_ARCHITECTURES=arm64`.
No `-march=x86-64-v2`.

## What works on this Mac

Recorded 2026-10-25 on this machine (Apple M3, 8 cores, 24 GB, macOS 27.0, 16 KB pages,
display 1470×956). Toolchain: Apple clang arm64, CMake, Ninja, Homebrew `sdl3` (the build
still fetches SDL 3.4.16), `molten-vk` 1.4.2, `vulkan-loader` 1.4.363, `glslang` 16.6.0,
OpenSSL 3. No LunarG SDK, so shaders go through glslang. `file` on the linked binary reports
`Mach-O 64-bit executable arm64` (not a fat binary).

- Configure, compile, and link of `macos-release` succeed. The recompiled guest sources were
  produced locally under `generated/` and stayed untracked.
- The process starts, finds `assets/default.xbe`, and creates a MoltenVK device named
  "Apple M3". The window is borderless and covers the display.
- The first seconds are one draw per frame at about 30 fps (a full-frame upload of about
  1.2 MB, consistent with the intro movie). From about 13 s the title is a rendered scene
  at about 60 fps until the old fault point.
- The one-texture present shader, with the pipeline cache removed first, logged no
  `cannot reserve` line and no `lut`. An earlier 1024-draw blocking split kept the
  device up and held the post-75 s scene at about 33 fps (about 11800 draws), because
  each flush waited. Render-target readbacks past the old fault were not blank.
  Present 0 is all zeros. Those files were deleted.
- The split is now 4096 draws, still a blocking `Flush()`. The binary that ships does
  not allocate the extra command-buffer rings tried for a pipelined submit. A run on
  2026-10-25 with `FUZION_AUTOSTART=1` and no capture stayed up through 190 s. The old
  fault frame (~53 s, about 9000 draws) was 60 fps. From 75 s through 190 s the mean
  was 58.2 fps over 125 one-second samples (median about 59), at roughly 600–12800
  draws. Host draw in that window averaged 13.9 ms. Fifteen of those seconds dipped
  under 55 fps, the lowest 47.9, when host draw reached about 20 ms. Zero skipped
  draws, zero pipeline misses, no upload overflow, no device loss, no signal 11.
- Swapchain readbacks from the same 4096-draw wait, with the scripted pad on, were
  looked at and then deleted. Present 0 was black. Later presents showed a
  character-select screen with six portraits and a start prompt, a main menu whose
  list includes a minigames entry, a character in blue standing on a platform under
  a get-ready prompt, that character mid-jump in a bright arena of floating blocks
  with a score and a timer counting, a round-over results screen with a score, a
  time bonus, and a rank, then a return toward character select. The guest front
  buffer matched the swapchain. Nothing blocked menus or a minigame once
  `FUZION_AUTOSTART` was set. Unset, the intro movie does not skip, and the pad
  hook is a no-op. `FUZION_AUTOSTART_PLAY_MS` is still 75000.
- The title opened its music package while this was running. The test set `XBE_MUTE=1`, so
  playback was not listened to.
- Guest threads run. The main thread stays in the Cocoa event loop. An earlier build aborted
  there because a guest thread called `SDL_PumpEvents`; that call is skipped on Apple now.

## What is broken / open

- Next: battery and heat. The white characters and the white Twisted System floor are
  resolved (`docs/render-defect.md`). What is already measured, on battery during the
  60 fps heavy scene: 11.168 V and −1231 mA (13.7 W; `BatteryPower` −13747 mW), process
  CPU about 76% of one core, RSS about 195 MB. `powermetrics` needs root and `sudo -n`
  is not available. The frame cap and guest stalls already sleep; the 4096-draw flush
  still waits inside a heavy frame.
- The heavy scene sits on the vsync budget most seconds and still misses a few when
  a second of ~12000 draws takes about 20 ms of host draw. The 4096-draw flush waits,
  so the title thread spends several milliseconds blocked on that submit inside a
  heavy frame. 2745 draws in one buffer stayed alive; ~8500 did not.
- Non-blocking mid-frame submits are not safe on MoltenVK 1.4.2. Rotating the three
  frame slots, and a separate command-buffer ring plus a timeline semaphore, both
  SIGSEGV'd at ~53 s in image-view encode, at the same address. The shipped path
  waits on each 4096-draw flush. Do not retry either handoff on this driver.
- `powermetrics` must be root, and `sudo -n` is not available, so there is no package
  or GPU watt sample. On battery, `ioreg` AppleSmartBattery during the 60 fps heavy
  scene read 11.168 V and −1231 mA (13.7 W; the `BatteryPower` field was −13747 mW).
  Process CPU was about 76% of one core (`ps` %cpu) and RSS about 195 MB. The earlier
  1024-draw binary, at about 33 fps in the same scene, sat around 58–71% CPU with a
  sticky gauge of about 7.0–7.4 W (11.45 V, −610 to −642 mA). The gauge is coarse and
  the machine was discharging the whole session, so the watt change is not a clean
  before/after. What the binary does for heat is sleep the frame cap, sleep guest
  stalls, and finish the frame inside the vsync interval instead of a ~30 ms
  serialized flush. A 30 fps cap is already in the overlay list.
- MoltenVK does not implement every feature the Linux backend uses (some BC layouts,
  `D24_UNORM_S8`, RGB32 float images, non-solid fill). The device only enables features it
  advertises; the depth format falls through the existing loop.
- 16 KB protection is coarser than the guest's 4 KB commits. A title that depends on a fault
  from a 4 KB hole inside a committed host page will not fault. This run did not hit that.
- No HDR. No image-to-image handover (no abstract-namespace sockets, no `pidfd`). This title
  is one XBE, so handover is not on the boot path.
- Screen recording is not permitted for this environment. The picture check copied the
  swapchain (`swap_NNNNN.bmp`, next to the existing guest readbacks) into `/tmp`, then
  those files were deleted. Do not commit them. With the capture env unset, no copy
  runs. The swapchain still requests `TRANSFER_SRC` when the device advertises it;
  that usage was on during the 60 fps run.
- A couple of shorter runs logged `Exit (0)` when the test shell was torn down: SDL
  turns that signal into a quit. A process started in its own session kept drawing.
- Linux and Windows were not rebuilt here. Their sources are the unpatched submodule.
  Inside the patch, the non-Apple `Pace()` and `KeStallExecutionProcessor` branches
  are the previous yield spins, and the 4096-draw flush is under `__APPLE__`. A Linux
  configure is the check that the patch did not leak into the submodule SHA.
- The Windows exe remains untested on Windows, as before.

## Reproduce

From a clean checkout on an Apple Silicon Mac, with your own disc extracted to `assets/`
(`default.xbe` plus the rest of the disc, not the XBE alone):

```sh
git submodule update --init sdk
# one of:
git config core.hooksPath tools/hooks
ln -sfn ../../tools/hooks/pre-commit .git/hooks/pre-commit

brew install cmake ninja openssl@3 glslang libusb molten-vk vulkan-loader vulkan-headers
# optional: LunarG Vulkan SDK, then `source` its setup script so VULKAN_SDK is set
# xdvdfs, only if you want that extractor (there is no brew formula):
#   cargo install xdvdfs-cli

./build.sh
# equivalent:
#   ./tools/apply-macos-sdk.sh
#   cmake --preset macos-release
#   cmake --build --preset macos-release

file out/build/macos-release/RingHost    # Mach-O 64-bit executable arm64
out/build/macos-release/RingHost
```

The Mac executable and window title are Ring Host. Linux and Windows still produce
`FuzionFrenzy`. Earlier notes in this file that name `FuzionFrenzy` are the binary
those runs actually launched. Logs now land in `out/build/macos-release/logs/ringhost.log`.
The logs directory is gitignored. Do not commit it,
screenshots, or anything under `generated/`.

Useful environment variables are the ones in the README (`XBE_PERF`, `XBE_MUTE`,
`FUZION_AUTOSTART`). `FUZION_AUTOSTART` has to be set or the scripted pad never runs.
`XBE_VK_CAPTURE_EVERY`, with `XBE_CAPTURE_DIR` pointed outside the repo, copies the
swapchain on those presents. Leave both unset for a normal run, and delete the files.
Leave `XBE_VK_DEBUG` unset unless you mean to; the README records that the validation
layer has frozen a machine.

## References for the GPU fault

Ideas only. Nothing from these trees was copied. DirectXbox-Recomp is GPL-3.0. xemu and
black-macos-runtime's NV2A renderer are GPL as well.

- [phobos665/DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp) and
  [split2-recomp](https://github.com/phobos665/split2-recomp). Apple Silicon via MoltenVK
  1.4.2. Their Vulkan notes (`docs/technical/vulkan-backend.md`, section 10) and
  `src/d3d/rhi_vulkan.c` are the port that already ships this stack:
  - Portability enumeration, `VK_KHR_portability_subset`, and preferring MoltenVK when
    KosmicKrisp is also installed. This tree already does the first two.
  - Argument buffers stay on. They bind with push descriptors, and a TimeSplitters 2 or
    Burnout frame is a few hundred draws, so one command buffer never holds the residency
    set that faults here. Disabling argument buffers is not their fix. Turning them off
    here makes the present shader fail to compile.
  - Vertex stride is set on the draw (`vkCmdBindVertexBuffers2`), so a shorter stride
    cannot reuse a pipeline that fetches the longer one. This backend bakes stride into
    the pipeline, so the key now includes it.
  - Base vertex is passed through. Apple Silicon supports a non-zero base. The frames
    that faulted here were unindexed strips with base 0.
  - Cube images are cube-compatible 2D arrays. Volume images are `VK_IMAGE_TYPE_3D`.
    This title bound neither on the faulting run or the surviving one.
  - BC uploads copy the real mip extent, with the row size in blocks. Component swizzle
    (forced alpha, A8) is on the view. Morton swizzle happens on the CPU before upload.
    Sampler LOD bias is left at 0 because Metal has no per-sampler bias. This port clamps
    the bias and the LOD to the view instead.
- [elpetese/black-macos-runtime](https://github.com/elpetese/black-macos-runtime) presents
  xemu's NV2A renderer (`RECOMP_XEMU_GPU=1`) through a Cocoa framebuffer. That path is
  OpenGL, not MoltenVK, so it does not hit this fault. Their port notes describe boot,
  menus, and the first mission, and the serious bugs they hit were in the recompiler and
  the kernel, not in texture upload.
- xemu's Vulkan renderer ([PR 1701](https://github.com/xemu-project/xemu/pull/1701)) is
  Linux and Windows. MoltenVK is still blocked there on geometry shaders. The macOS FPS
  regression ([issue 2506](https://github.com/xemu-project/xemu/issues/2506)) is OpenGL
  geometry-shader cost. [CosmicSnow/xemu-for-macos](https://github.com/CosmicSnow/xemu-for-macos)
  emulates quads, strips, line loops, and provoking vertex on the CPU and turns on the
  portability extensions. That is a different renderer. It did not suggest the split.
- MoltenVK reports with the same address-fault signature:
  [#2480](https://github.com/KhronosGroup/MoltenVK/issues/2480) (UnleashedRecomp, argument-buffer
  texture heaps and buffer-to-texture copies, M1/M2 since macOS 15.4; M3/M4 were fine, and
  this machine is an M3), [#2453](https://github.com/KhronosGroup/MoltenVK/issues/2453) /
  PR 2482 (a variable-length argument-buffer array of size 1; the workaround is argument
  buffers off, which this present shader cannot use),
  [#2705](https://github.com/KhronosGroup/MoltenVK/issues/2705) (an imported `MTLTexture`
  left non-resident; this port does not import textures),
  [#2278](https://github.com/KhronosGroup/MoltenVK/issues/2278) (descriptor indexing).
  The Metal rule that matches the measurement: with argument buffers the driver does not
  see every resource, and a command buffer that outruns its residency set page-faults a
  read. Splitting the submits is what kept this device alive. DirectXbox's frames stay
  a few hundred draws, so that port never needed this split. The idea that came across
  is to keep argument buffers on and keep each submit's residency set small.
- The one-texture present shader is local. Giving that shader two textures still
  collapsed both to Metal index 0 after `mapIO()` and after each variable had its own
  SPIR-V image type. Game shaders that sample four textures at bindings 2..5 compile.
  Nothing from the GPL trees was copied into the blit.

## Launcher

`macos/launcher/package-dmg.sh` compiles the SwiftUI app with the Command Line Tools
`swiftc` (arm64, macOS 14 deployment target, `-parse-as-library`) and packs
`macos/launcher/dist/Ring Host.app` plus
`macos/launcher/dist/RingHost-macos.dmg`. The script copies tracked files
with `git ls-files`, then replaces `sdk/` with `git archive` of the pinned commit so the
image does not carry this machine's patched worktree. `tools/apply-macos-sdk.sh` applies
the patch at build time, and if that sdk has no `.git` it runs `git init` first. The
packaging check refuses an xbe, an iso, recompiled guest sources, or a `FuzionFrenzy`
or `RingHost` binary inside the app. The product directory and `*.dmg` are gitignored, and the
pre-commit hook refuses them. Ad-hoc `codesign` only. Notarization is future work.
`hdiutil create -format UDZO` warns that it is deprecated and still writes the image.

The app bundle id is `local.ringhost.launcher`. The executable is
`RingHostLauncher`, so it does not collide with the game process `RingHost`. The
icon is `RingHost.icns`, drawn at package time (black, yellow rule, yellow ring).
There is no sandbox, so
the chosen path is stored as a string (`launcher.json` in the support folder). A
security-scoped bookmark is best-effort.

Support folder: `~/Library/Application Support/RingHost`, or `--support` for a test
directory. Layout: `src/` (port copy), `src/assets` as a symlink to the chosen folder
or to `disc/` after an iso extract, `src/out/build/macos-release/RingHost` with
`settings.toml` beside it, `logs/ringhost.log`, `launcher.json`, `build.log`,
`built-version`. The game walks up from the executable to find `assets/`. Children get
`/opt/homebrew/bin` and `/usr/local/bin` on `PATH`. `GIT_TERMINAL_PROMPT=0`.

USA check: SHA-1 of `default.xbe` equals `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad`,
magic `XBEH`, and at least one other non-iso file in the folder. A lone xbe is
rejected. An `.iso` is accepted, extracted with the bundled `tools/xiso_extract.py`,
then checked the same way. No disc image was on this machine, so that extract was not
run against a real iso. Prerequisites are detected and not bundled: arm64, clang, git,
cmake >= 3.25, ninja, python3, the MoltenVK ICD, and the glslang header plus its three
libraries. openssl is not required. Capstone is unused. The install line is
`brew install cmake ninja glslang libusb molten-vk vulkan-loader vulkan-headers`. A wired Xbox Series pad opens through GameController. libusb stays linked for a USB pad Apple's Xbox driver does not claim. No kernel extension.

Version stamp is `PORT_VERSION` (`1`) plus the short git sha. A dirty worktree appends
`-dirty`, ignoring the `sdk` submodule line. A mismatch, a missing binary, or Rebuild
copies the port again and deletes `generated/` and `out/` before `build.sh macos-release`.
`config/game.toml` sets `partitions = 64`, so the compile is about 403 ninja steps.
The first configure downloads SDL and Zydis.

Settings match `settings.cpp`. Defaults follow the game: borderless, desktop size,
render 100, fps cap 60, vsync on, anisotropic 0, aspect auto, ultrawide flag on.
`--windowed` with a zero size writes 1280×720. Sound off is `XBE_MUTE=1`, not a toml
key. Scripted pad is `FUZION_AUTOSTART=1` and defaults off. HDR keys are written off.
The keyboard map is the fixed table in `src/common/input_kbm.cpp`. Controllers follow
SDL plug order. There is no 4K movie path; `docs/movie-path.md` records 640×480 Bink.

The window is a black field. The wordmark is RING / HOST in Helvetica Neue Condensed
Black, yellow, with a yellow rule and a plain yellow ring. Menu rows are Helvetica
Neue, lowercase, with a short white shadow (radius about 0.7) so the type is a little
soft, the way a 480p menu looks, and still readable. The current row is white and the
rest are a dusty blue. Faint blue digits and a few outlined shapes sit on the right.
A thin yellow bar shows compile progress. The selected page lives on the model
(`@Published`) because the Command Line Tools `swiftc` has no SwiftUI macro plugin,
so `@State` does not compile here.

A full play through the previous launcher binary, with `--disc` pointed at this
machine's extracted `assets/`, `--fps 30`, `--windowed`, `--mute`, and `--perf`,
wrote `display_mode = "windowed"`, 1280×720, `fps_cap = 30`, `vsync` on. The game
log showed those values. 122 `perf:` samples ran from 3.7 to 31.7, none over 35.
Dropping the first eight left a mean of 26.3 and a max of 30.2. The mean sits under
30 because the intro movie runs near 25 fps. The 30 fps cap is the one that held.
The menu restyle does not change that writer. Linux and Windows were not rebuilt.

## Next steps

1. The heavy scene is at the vsync budget most seconds. The dips are host draw past
   16.7 ms, not a stuck flush. A faster path needs MoltenVK to survive a non-blocking
   split. The two attempts here faulted image-view encode. Do not retry slot rotation
   or a timeline submit on 1.4.2 without a new driver.
2. Keep `FUZION_AUTOSTART_PLAY_MS` at 75000. That default is shared with Linux.
   Set `FUZION_AUTOSTART=1` or the scripted pad never runs and the intro movie stays up.
3. A root `powermetrics` sample would separate GPU and package power. The battery
   gauge above is the number this machine could produce without root.
4. Keep the patch the only SDK delta. Do not commit a new `sdk` gitlink, and do not commit
   `generated/`, `assets/`, `logs/`, pictures, or the launcher disk image.
5. The launcher's iso extract has not been run against a real disc image on this machine.
   Notarization is still future work. MoltenVK, cmake, and ninja stay detected, not bundled.
