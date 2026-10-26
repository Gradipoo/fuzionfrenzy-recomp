# Agent workflow

Durable rules for handing this repo from one agent to the next. Current facts
(branch tip, replay commands, hashes, open items) live in `HANDOFF.md`. Settled
choices live in `docs/decisions.md`. This file is the procedure.

## Start

Read in this order:

1. `HANDOFF.md`. It is the cold start: latest branch, per-host determinism
   status, exact Mac and Lima commands, env vars, gotchas, and open items.
2. `docs/decisions.md`, before changing math, clocks, input, or the netplay
   approach. A logged decision stays until new evidence says to reopen it.
3. `docs/netplay-determinism.md` when the task touches fixed step, pads, or
   hashes. `docs/ci.md` when the task touches CI.

The XBE stays on this Mac. Recordings, HDD snapshots, and state logs stay
local. Paths and the snapshot checksum are in `HANDOFF.md`.

## One task per agent

One session completes one task, then stops. A fresh agent starts the next
task from `HANDOFF.md`.

Take the task the coordinator named. When the coordinator points at the open
items and does not name one, take the first open item and leave the rest.
Finish that item, including its replays, before editing anything that belongs
to the next item. Networking stays off until a session is given that task.

## End of every task

Update `HANDOFF.md` in the same commit as the work, then push.

- **Status.** What matches now, on which hosts. Include the `det-diff` line,
  the frame count, the exit sim tick, and the frame 0 and last-frame total
  hashes when a replay ran.
- **Branch.** Name, tip commit, and the review base. Replace the previous tip.
  Keep the compare URL when no pull request exists.
- **Commands.** If a flag, path, guest name, or rebuild step changed, replace
  the command in `HANDOFF.md` with the one that just passed. Leave a command
  that still works untouched.
- **Open items.** Remove the item this task finished. Add anything the task
  uncovered. Leave the others in place.
- **Decisions.** When the task chooses an approach a later agent might redo,
  append a dated entry to `docs/decisions.md` and link it from the handoff
  status in one sentence.

Commit as `port <port@local>` by setting `GIT_AUTHOR_NAME`,
`GIT_AUTHOR_EMAIL`, `GIT_COMMITTER_NAME`, and `GIT_COMMITTER_EMAIL` on that
command. Leave git config alone. Stage only the files the task changed.
`git status` showing `m sdk` is the patched worktree; the submodule pointer
stays `58757ab`.

## Decisions log

`docs/decisions.md` is append-only. Each entry has the date in
America/Los_Angeles, the commit, the choice, and what would reopen it.
Newest work goes at the bottom. The harness design and the hash format stay
in `docs/netplay-determinism.md`; the log records the choice, not the manual.

## Branch naming

New branches are `cursor/<descriptive-name>-c2dc`, lowercase. Continue
`cursor/linux-det-c2dc` when the task extends the determinism line. Open a
new branch off the tip in `HANDOFF.md` when the coordinator asks for a
separate line. The review base for this line is `cursor/fixed-step-pace-c2dc`.
Screenshot, Tracy, Ghidra, and ASan branches stay separate.

Push with `git push -u origin <branch>`. Open or update the pull request with
the session's pull-request tool, base `cursor/fixed-step-pace-c2dc` unless
`HANDOFF.md` names another base. `gh` views CI (`gh run list`, `gh run view`).
When pull-request creation returns unauthenticated, report the compare URL
and stop:

`https://github.com/Gradipoo/fuzionfrenzy-recomp/compare/<base>...<branch>`

## How to verify

The exact replay block is in `HANDOFF.md`. Use that block. A passing run is
the source for the next handoff.

A change that can move a hash:

1. Restore Sean's HDD snapshot into the build's `hdd/` and delete `._*` and
   `.DS_Store` before the run. Gameplay2 uses no `--instance`, and leaves
   `FUZION_AUTOSTART`, `FUZION_MINIMIZED`, and `FUZION_WATCH_VA` unset.
2. Replay on the Mac from `out/build/macos-release` with `RingHost`. Two
   replays when the change touches scheduling, input, or the guest turn.
3. Compare with `python3 scripts/det-diff.py <expect> <local>`. The expect
   log for gameplay2 is `det-logs/gameplay2.detlog`. Also diff the two Mac
   logs against each other.
4. For a cross-host task, replay in Lima after the Mac logs match.
   `limactl shell ff-det` (arm64), then `limactl shell ff-det-x86` (x86_64).
   `LP_NO_RAST=1` and `VK_DRIVER_FILES` pointing at `lvp_icd.json`. `set +e`
   around `xvfb-run`, then print the exit code. Home on the guest is the Mac
   home, read-only, so the pads path is the Mac path. Host `/tmp` is not
   mounted; each guest has its own `/tmp/ff-hdd-sean`.

`scripts/det-diff.py --self-check` is the synthetic check CI runs. It does
not boot the game.

A math change also runs `scripts/fp-digest-host.sh`. The printed hash must
match `tools/fp-digest.expected` (`06a5a5d542b715f9`). `scripts/fp-cross.sh`
is the local arm64 / Rosetta x86_64 / gcc check. CI does not run it.

View CI with `gh run list --branch <branch>`. The workflow is
`.github/workflows/ci.yml`: ubuntu-latest (valgrind math digest) and
macos-14. It does not boot the game and has no Windows job.

Logs, pad files, and region dumps stay out of the commit. Copy a guest log
back to the Mac `/tmp` when the report needs it.

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

## Report back to the coordinator

Send this, in this order. Times in America/Los_Angeles.

```
Results
- Hosts and the det-diff line (PASS N frames, or the first mismatched frame and region).
- Frame 0 total, last frame total, exit sim tick, exit code.
- Hashes that moved, and the commit that moved them.

Branch
- cursor/<name>-c2dc @ <commit>
- Compare: https://github.com/Gradipoo/fuzionfrenzy-recomp/compare/<base>...<branch>
- CI: green, failed, or not rechecked, with the run id when one finished.

Sean
- What Sean has to do. Write "Sean does not need to do anything" when that is true.
  A re-record, a sudo password, a plugged pad, or a Windows machine belongs here.
```

State a result that was not re-run as not re-run. Leave networking, save-state
replays, and the other open items for the session that is given them.
