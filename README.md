# Fuzion Frenzy - Static Recompilation

An unofficial native PC port of the original Xbox release of *Fuzion Frenzy* (USA), produced by
statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/abilliontokens/XboxXBERecomp) fork (vendored in `sdk/` as a
submodule on branch `fuzion-frenzy-port`). Windows uses Direct3D 12 and XAudio2; Linux is a native
Vulkan and SDL3 build - no Wine.

**Status: builds and boots; not yet playable.** Both binaries build from this repository on Linux
(`FuzionFrenzy` for Linux, `FuzionFrenzy.exe` cross-compiled for Windows). The Linux binary starts,
reads its settings, finds and verifies the disc, recompiles and runs 10,587 guest functions, and
reaches the title's Direct3D device creation. It stops there, by design, with a named list of the
XDK data symbols this build still needs. **No picture has ever been on screen.** The exact state,
with evidence, is in `docs/status.md`; the first boot and what it proved is in `docs/boot-crash.md`.

This repository contains **no game code and no game assets**. It holds tooling, configuration and
host-side glue only. You must supply your own legally acquired copy of the disc.

## The game

| Field | Value |
| --- | --- |
| Title ID | `0x4D530002` (MS-2) |
| Region / media | North America / `dvd_x2` |
| XBE | `default.xbe`, 2,834,432 bytes, SHA-1 `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad` |
| Base / entry | `0x00010000` / `0x00030F7F` |
| Built | 2001-09-25 |
| XDK | **3925** (D3D8, DSOUND); 3911 (XAPILIB, XBOXKRNL, LIBC, XGRAPHC) |
| Kernel imports | 111, all 111 covered by the SDK runtime |
| Disc | 148 entries, 136 files, 2,083,933,828 bytes of file data |

The build is pinned to this exact XBE. A different region or revision needs its own analysis pass.

## Why this one is hard

Fuzion Frenzy was an Xbox **launch** title. Its XDK, 3925, is nearly two years older than the 5558 of
the sibling *Sims: Bustin' Out* port and three years older than the SDK's own 5849 reference set, and
**this XBE has no `.XTLID` section**. `.XTLID` is the export-id table whose ids are API identities
stable across XDK builds; it is what let the sibling port name 65 library functions outright on its
first scan. With it absent, and byte signatures from a different build worthless, `xbe_symbols` names
only **14 of the 200** library functions this image actually links:

```
xbe_symbols assets/default.xbe sdk/xdk --out config/game.toml
  found 14 of 240 functions, 0 of 12 data
```

(240 minus the 40 XNET, BINK and XONLINE names this XBE has no section for = 200 reachable.)

Every `0` in `[symbols]` means the **game's own console code runs**. For D3D8 that code drives the
NV2A directly, which is what the first boot proved: a signal 11 reading the NV2A register window at
`0xFD000000` from the title's own device-init path. The remaining names are therefore being
established by static analysis of the image, verified against properties measured to be stable across
two real XDK builds (`ret` identical in 171/171 names, `regs` in 175/175, byte length in 171/175).
`docs/xbe-analysis.md` has the full account, including the seeding experiments that did not help.

## Status

| Stage | State |
| --- | --- |
| Disc image read and extracted (`tools/xiso_extract.py`) | done - 136 files in `assets/` |
| XBE identified and analysed (`tools/xbe_info.py`) | done - see the table above |
| XDK signature set for build 3925 | **seeded only** - `sdk/xdk/3925/signatures.toml` from `tools/sig_bootstrap.py`; no `.XTLID`, so it rests on constants alone |
| `config/game.toml` | 15 of 240 `[symbols]` named, 2 of 12 `[data]` named |
| `sdk/xdk/3925/layout.toml` | **empty** - only `CDevice_Push = 0x0` and the miniport at `CDevice + 0x2308` are proven so far |
| Recompilation | done - 10,587 functions, 633,913 instructions, 2,165,441 of 2,368,328 code bytes (91.4%) |
| Kernel import coverage (`tools/kernel_coverage.py`) | done - 111 of 111 (`docs/kernel-coverage.md`) |
| Native Linux build (Vulkan + SDL3) | done - builds, links, boots |
| Windows build (D3D12 + XAudio2), cross-compiled on Linux | done - builds and links; **never run on Windows** |
| Picture on screen | **not reached** |
| Audio | **not reached** |
| F1 / F2 panels in play | **not verified** - the code is carried over whole but has never been seen running |

## Controls

The game only ever reads an Xbox pad, so the host translates keyboard and mouse into one on port 0,
and controllers are picked up in the order they are plugged in. This is the host layer's mapping as
`src/common/input_kbm.cpp` implements it. **None of it has been exercised in this game yet.**

| Keyboard / mouse | Pad |
| --- | --- |
| `W` `A` `S` `D` | left stick |
| arrow keys | D-pad |
| mouse, right button held | right stick |
| `Q` / `E` | right stick left/right |
| `R` / `F` | right stick up/down |
| mouse wheel | right stick up/down, in pulses |
| space | A |
| escape, backspace | B |
| `X` / `Y` | X / Y |
| `Z` / `C` | left / right trigger |
| `1` / `2` | black / white |
| enter | Start |
| tab | Back |
| shift / ctrl, middle mouse | left / right stick click |

Keys are read only while the game window has focus. While the F1 settings panel is up it owns the
keyboard and the mouse and the game sees neither.

## Settings, F1 / F2 overlay

**F1** opens the host settings panel (display mode, resolution, aspect ratio, ultrawide, vsync, frame
cap, render scale, anisotropic filtering, HDR on Windows, and a Cheats section) and **F2** the
frame-time graph. **F3** captures a frame when `XBE_CAPTURE_DIR` is set. In the F1 panel the
**Resolution** control is greyed out in borderless mode - borderless always follows the display it is
on - and is editable in windowed and fullscreen, with the stored value surviving a mode switch.

The Cheats section currently shows *"No cheats identified for this title yet."* No cheat code or cheat
setter address has been found in this image; the plumbing is real and the table is empty rather than
invented.

Settings live in `settings.toml` beside the executable; the first run also creates `logs/` and `hdd/`
(where saves go) beside it. Keys are read by `sdk/runtime/app/settings.cpp`:

| Key | Values / default |
| --- | --- |
| `display_mode` | `fullscreen` / `borderless` / `windowed` (default `borderless`) |
| `width`, `height` | output size in pixels; `0` = desktop mode (default) |
| `render_percent` | render scale as a percent of the output height (default `100`) |
| `fps_cap` | frame-rate cap; `0` = unlimited (default `60`) |
| `vsync` | `true` / `false` (default `true`) |
| `anisotropic_filtering` | `0` = as the game asks (default) |
| `aspect_ratio` | `auto` (default), `4:3`, `16:9`, `16:10`, `21:9`, `32:9` |
| `ultrawide` | `true` (default) / `false` |
| `hdr`, `hdr_paper_white`, `hdr_peak` | Windows/D3D12 only |
| `join_ip` | system-link co-op host address (empty by default) |

## Diagnostic environment variables

| Variable | Effect |
| --- | --- |
| `XBE_PERF=1` | per-second `perf:` log lines (fps, draws, upload, clears) |
| `XBE_CAPTURE_DIR=<dir>` | where F3 frame captures and periodic front-buffer dumps are written |
| `XBE_VK_CAPTURE_EVERY=<n>` | dump the guest front buffer every *n* presents |
| `XBE_MUTE=1` | silence SDL audio output for capture or benchmark runs |
| `XBE_VK_DEBUG_DRAW=1` | full per-draw Vulkan dump |
| `FUZION_AUTOSTART=1` | scripted pad self-test on port 0 (blind Start/Up/A cycle) |
| `FUZION_STEP=<file>` | step mode: each token written to the file is one burst of pad-0 pulses |
| `FUZION_NO_STEP=1` | disable the frame-step fix in `src/game/hooks.cpp` (there is none yet) |

`XBE_VK_DEBUG=1` enables the Vulkan validation layer. The sibling port records that it hard-froze
this machine twice; prefer the logging and capture paths above.

## Layout

```
assets/        your disc image and/or the files extracted from it (never committed)
cmake/         windows-cross.cmake - clang-cl + xwin toolchain for the Windows exe on Linux
config/        game.toml (the recompiler config), found-*.toml (proven symbols, merged into it),
               ref/5558-signed.toml (a second XDK build's signature set, used as a reference)
docs/          analysis and bring-up notes
sdk/           XboxXBERecomp SDK submodule (recompiler, runtime, XDK HLE, renderers)
src/common/    host-side code: F1/F2 overlay, keyboard-as-pad, GameInfo
src/game/      hooks and overrides for recompiled functions
tools/         xiso_extract.py, xbe_info.py, kernel_coverage.py, sig_bootstrap.py,
               align_symbols.py, merge_symbols.py, and the screenshot/driving helpers
generated/     recompiler output (never committed)
```

## Build it from source

You supply the game: the USA Xbox release, as either

- the whole disc image (`.iso`) on its own in `assets/`, or
- the fully extracted files in `assets/` (`default.xbe` plus the `Packages` folder and everything else).

> **Warning:** a folder holding only `default.xbe` is the one shape that does **not** work. The build
> needs `assets/default.xbe` to hash and recompile, while the game needs its data files; that folder
> alone leaves the title with nothing to load.

1. Put your disc image in the repository root, or the extracted files in `assets/`.
2. Extract it:
   ```
   python3 tools/xiso_extract.py info    "Fuzion Frenzy (USA).xiso.iso"
   python3 tools/xiso_extract.py extract "Fuzion Frenzy (USA).xiso.iso" assets
   ```
3. Check the XBE is the one this configuration targets - the SHA-1 must match the table above:
   ```
   python3 tools/xbe_info.py assets/default.xbe
   ```
4. Build:

   **Linux** (native Vulkan + SDL3):
   ```
   ./build.sh
   ```
   builds `FuzionFrenzy` into `out/build/linux-release`. You need clang, cmake and ninja on `PATH`,
   plus SDL3 and the Vulkan headers and loader.

   **Windows, cross-compiled on Linux** (needs an
   [xwin](https://github.com/Jake-Shadle/xwin) sysroot at `~/.cache/xwin/sysroot`):
   ```
   cmake --preset host-tools    && cmake --build out/build/host-tools
   cmake --preset windows-cross && cmake --build out/build/windows-cross
   ```

   `config/game.toml` is already written and committed for this XBE, so neither route needs to
   regenerate it.

## Working on the port

- Never edit anything in `generated/`; it is rewritten on every recompile.
- Newly proven XDK symbols go into a `config/found-*.toml` and are merged with
  `python3 tools/merge_symbols.py --apply`, which refuses a name claimed at two addresses, two names
  at one address, an address outside the image, and an address in the wrong library section.
- Behaviour changes go in `src/game/` as `X86_FUNC(sub_XXXXXXXX)` overrides, or as `[[hook]]` entries
  in `config/game.toml`.
- Analysis mistakes are corrected in `config/game.toml` with `[[function]]`, `[[switch]]` and
  `[xbe] data_ranges`.

## Legal

Bring your own disc. Do not commit or distribute the XBE, the disc image, extracted assets, or any key
material. `assets/`, `generated/`, `*.iso`, `*.xbe` and `out/` are ignored by git for that reason. The
SDK is MIT licensed; see `sdk/LICENSE` and `sdk/THIRD_PARTY.txt`.
