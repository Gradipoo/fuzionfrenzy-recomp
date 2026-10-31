# Agent workflow

Procedure for any coding agent working in this repository (Cursor, Claude
Code, Codex, and others). Paths are relative to the repository root.
Placeholders in `<angle brackets>` are values you choose on your machine.
They stay local.

[HANDOFF.md](../HANDOFF.md) is maintainer-local. It records one maintainer's
worker, absolute paths, HDD snapshot, and guest names. Read it when you are
on that machine. Leave those details there.

## Start

Read in this order:

1. [README.md](../README.md) for the disc you must supply and the build.
2. This file.
3. [docs/decisions.md](decisions.md) before changing math, clocks, input, or
   the netplay approach. A logged decision stays until new evidence says to
   reopen it.
4. [docs/netplay-determinism.md](netplay-determinism.md) when the task
   touches fixed step, pads, or hashes. [docs/ci.md](ci.md) when the task
   touches CI.
5. [HANDOFF.md](../HANDOFF.md) when you are continuing the maintainer's
   machine. The status, commands, and open items for that machine live in
   its maintainer-local section.

## Your disc

This repository contains no game code and no game assets. Each contributor
supplies their own legally obtained copy of the USA Xbox disc and keeps it
on their machine. It is never committed, pushed, or uploaded.

The build is pinned to one XBE. [README.md](../README.md) lists the size and
SHA-1. Check the file you extracted:

```
python3 tools/xbe_info.py assets/default.xbe
```

Put either the full extracted disc in `assets/` (`default.xbe` plus the
`Packages` folder and the rest of the files) or a disc image where the
README says to put it, then extract with `tools/xiso_extract.py`. A folder
that holds only `default.xbe` does not run: the build can recompile the XBE,
and the game still needs its data files.

`assets/`, `generated/`, `out/`, pad files, state logs, and HDD snapshots
stay untracked. The hook in the section below refuses them.

## Set up the build

From the repository root:

```
git submodule update --init sdk
git config core.hooksPath tools/hooks
./build.sh
```

`./build.sh` selects `macos-release` on Darwin and `linux-release` on Linux.
Pass a preset name to override that. On Darwin it applies
`patches/sdk-macos-arm64.patch` through `tools/apply-macos-sdk.sh` before
configure. Linux and Windows leave the submodule at the pinned commit.

Packages, the MoltenVK ICD, and the Windows cross preset are in
[README.md](../README.md). `config/game.toml` is already committed for this
XBE, so a normal build does not regenerate it.

| Host | Binary |
| --- | --- |
| macOS arm64 | `out/build/macos-release/RingHost` |
| Linux | `out/build/linux-release/FuzionFrenzy` |
| Windows | `FuzionFrenzy.exe` from `build.bat`, or `cmake --preset windows-cross` on a machine with the xwin sysroot. Run that exe on Windows. |

The first run creates `settings.toml`, `logs/`, and `hdd/` beside the
executable. Saves go in that `hdd/`. The game walks up from the executable
to find `assets/`.

`git status` showing `m sdk` means the platform patches are applied in the
worktree. Do not commit a new submodule pointer unless the task is to move
the pin. The pinned commit is recorded in the README and in `HANDOFF.md`
on the maintainer's machine.

## Record your own replays

`det-logs/` is gitignored. A recording is yours. Another person's pad file
is not in the clone.

Take a copy of the build's `hdd/` before you record, and call that copy
`<hdd-snapshot>`. A later replay of the same pads restores that snapshot
first, so both runs boot from the same saves. Delete `._*` and `.DS_Store`
inside `hdd/` after the restore. Use no `--instance` when the recording
used the `hdd/` beside the executable. Instance 2 and up use `hddN`.

Quit any copy of the game already running. Leave `FUZION_AUTOSTART` unset
so the scripted pad is not written into the file. Leave
`FUZION_PORT2_KEYS` and `FUZION_PORT2_MASH` unset for a one-controller
match. Plug in one pad, leave the other ports empty, and leave the keyboard
alone. Fixed step stays on so the file can be replayed. `<binary>` is
`RingHost` or `FuzionFrenzy` from the table above.

```
cd out/build/<preset>
env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_RECORD_INPUT=<repo>/det-logs/<name>.pads \
  FUZION_STATE_LOG=<repo>/det-logs/<name>.detlog \
  ./<binary>
```

Play the stretch you care about, then quit. Do not commit `det-logs/`.

Replay, after restoring `<hdd-snapshot>` into `hdd/`:

```
cd out/build/<preset>
rsync -a --delete <hdd-snapshot>/ hdd/
find hdd \( -name '._*' -o -name '.DS_Store' \) -delete
env -u FUZION_AUTOSTART -u FUZION_MINIMIZED -u FUZION_WATCH_VA \
  XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \
  FUZION_REPLAY_INPUT=<repo>/det-logs/<name>.pads \
  FUZION_STATE_EXPECT=<repo>/det-logs/<name>.detlog \
  FUZION_STATE_LOG=<local-log> \
  ./<binary>
python3 <repo>/scripts/det-diff.py <repo>/det-logs/<name>.detlog <local-log>
```

`<local-log>` is any path outside the repo, or under `det-logs/`. Both are
gitignored when they match the hook rules; keep them unstaged either way.
`FUZION_FRAMES` stops the run after that many presents. Unset
`FUZION_WATCH_VA` on a run you want to match. The watch is a debug tool and
is off unless that variable is set.

A headless Linux replay can use lavapipe. Point `VK_DRIVER_FILES` at
`lvp_icd.json` from that machine's Vulkan ICD directory, set `LP_NO_RAST=1`,
and start under `xvfb-run -a`. The hash covers guest RAM. Pixels stay out
of it. Wrap `xvfb-run` with `set +e` and print the exit code afterward.
`set -e` together with `xvfb-run` can hide the game log.

To compare a second operating system, copy your source tree onto that
machine or into a VM you own. Restore the same `<hdd-snapshot>` there.
Your pad file and expect log have to be readable at `<repo>/det-logs/` on
that machine. Guest names, SSH ports, and snapshot directories belong in
your notes or, on the maintainer's machine, in `HANDOFF.md`.

`scripts/det-matrix.sh` from the repo root is a short local matrix (muted,
minimized, fixed-step, seed 1, `FUZION_AUTOSTART` unset). It writes
`det-logs/idle.pads` and wipes `hdd2`, `hdd3`, and `hdd4` only.
`FUZION_DET_FRAMES`, `FUZION_DET_TIMEOUT`, and `FUZION_DET_BUILD` override
its defaults.

Flag meanings are in [docs/netplay-determinism.md](netplay-determinism.md).

## One task per agent

One session completes one task, then stops. The next task starts in a fresh
session that reads this file and, on the maintainer's machine, `HANDOFF.md`.

Take the task you were given. When the open items are listed and none is
named, take the first and leave the rest. Finish that item, including its
replays, before editing anything that belongs to the next item. Networking
stays off until a session is given that task.

## End of every task

On the maintainer's machine, update the maintainer-local section of
`HANDOFF.md` in the same commit as the work:

- **Status.** What matches now, on which hosts. Include the `det-diff` line,
  the frame count, the exit sim tick, and the frame 0 and last-frame total
  hashes when a replay ran.
- **Branch.** Name, tip commit, and the review base. Replace the previous tip.
  Keep the compare URL when no pull request exists.
- **Commands.** If a flag, path, guest name, or rebuild step changed, replace
  the command that just passed. Leave a command that still works untouched.
- **Open items.** Remove the item this task finished. Add anything the task
  uncovered. Leave the others in place.

Anywhere else, leave `HANDOFF.md` alone. Put the same facts in the report
below.

When the task chooses an approach a later agent might redo, append a dated
entry to `docs/decisions.md`. Include a timezone on the date.

Commit with your own git identity. Stage only the files the task changed.
Agents on the maintainer's worker follow the author and branch pattern
written in `HANDOFF.md`.

## Decisions log

`docs/decisions.md` is append-only. Newest work goes at the bottom. The
harness design and the hash format stay in `docs/netplay-determinism.md`.
The log records the choice, not the manual.

## Branches

Use a short lowercase name that describes the change. Push with
`git push -u origin <branch>`. The compare link uses the repository you
cloned:

`https://github.com/Gradipoo/fuzionfrenzy-recomp/compare/<base>...<branch>`

The base is the branch the task named. When the task names none, use the
repository default, or the review base written in `HANDOFF.md` when you are
on the maintainer's machine.

The maintainer's cloud agents use the `cursor/<descriptive-name>-c2dc`
pattern recorded in `HANDOFF.md`. Other agents use their own names. Keep
screenshot, Tracy, Ghidra, and ASan work on their own branches unless the
task says to merge them.

`gh` is for viewing CI (`gh run list`, `gh run view`). Open a pull request
with the tool your environment provides. If that tool cannot authenticate,
report the compare URL and stop.

## How to verify

A change that can move a hash:

1. Restore `<hdd-snapshot>` into the build's `hdd/` and delete `._*` and
   `.DS_Store`. Leave `FUZION_AUTOSTART`, `FUZION_MINIMIZED`, and
   `FUZION_WATCH_VA` unset when the recording left them unset.
2. Replay on the host you built. Run a second replay when the change
   touches scheduling, input, or the guest turn.
3. Compare with `python3 scripts/det-diff.py <expect> <local>`. Also diff
   the two local logs against each other.
4. For a cross-host task, replay on the second host only after the first
   host matches. Use the same pads, the same seed, and the same HDD
   snapshot. On Linux set `LP_NO_RAST=1` and point `VK_DRIVER_FILES` at
   lavapipe when you want the checksum run described above.

`scripts/det-diff.py --self-check` is the synthetic check CI runs. It does
not boot the game.

A math change also runs `scripts/fp-digest-host.sh`. The printed hash must
match `tools/fp-digest.expected` (`06a5a5d542b715f9`). `scripts/fp-cross.sh`
is the local cross-compiler check (arm64, x86_64, gcc when it is installed).
CI does not run it.

View CI with `gh run list --branch <branch>`. The workflow is
`.github/workflows/ci.yml`: Ubuntu (valgrind math digest), macOS 26, and
Windows. It does not boot the game. See [docs/ci.md](ci.md).

## No game content

`tools/hooks/pre-commit` refuses a commit that stages game data, recompiled
guest code, or build output. The rules are `tools/hooks/forbid-game-paths.sh`.
The Ubuntu CI job runs that script on the diff. Install the hook from the
repo root, one of:

```
git config core.hooksPath tools/hooks
ln -sfn ../../tools/hooks/pre-commit .git/hooks/pre-commit
```

`core.hooksPath` replaces every hook under `.git/hooks`. This repo ships no
other hooks. `tools/hooks/forbid-game-paths.sh --self-test` checks the list.

The hook refuses `sdk` and `sdk/*`; `assets`, `generated`, `out`, `build`,
`logs`, `hdd`, `CrashDumps`, `shadercache`, `tools/bin`, `.toolchain`,
`macos/launcher/dist`; `*.xbe`, `*.iso`, `*.xiso`, `*.dmp`, `*.d3dcap`,
`*.pdb`, `*.exe`, `*.dmg`; `det-logs`, `det-desync`, `*.pads`, `*.detlog`,
`desync_*.txt`, `frame_*_*.bin`; `re/`, `*.gpr`, `*.rep`, `*.gzf`,
`time-rng-map.txt`; and any path containing `x86_recomp`,
`x86_func_mapping`, or `image_symbols.h`. Leave `.DS_Store` unstaged.

## Report

Send this back to the person who assigned the task. Name the timezone on
any clock time.

```
Results
- Hosts and the det-diff line (PASS N frames, or the first mismatched frame and region).
- Frame 0 total, last frame total, exit sim tick, exit code.
- Hashes that moved, and the commit that moved them.
- What was not re-run.

Branch
- <branch> @ <commit>, base <base>
- Compare: https://github.com/Gradipoo/fuzionfrenzy-recomp/compare/<base>...<branch>
- CI: green, failed, or not rechecked, with the run id when one finished.

Needed from you
- What the person who assigned the task has to do. Write "Nothing required
  from you" when that is true. A new recording, a privilege prompt, a
  plugged-in pad, or a Windows machine belongs here.
```

Leave networking, save-state replays, and the other open items for the
session that is given them.
