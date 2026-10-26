# Fuzion Frenzy - Static Recompilation

An unofficial native PC port of the original Xbox release of *Fuzion Frenzy* (USA), produced by
statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/abilliontokens/XboxXBERecomp) fork (vendored in `sdk/` as a
submodule on branch `fuzion-frenzy-port`). Windows uses Direct3D 12 and XAudio2; Linux and macOS
are native Vulkan and SDL3 builds - no Wine, and the Mac build is arm64, not Rosetta. macOS draws
through MoltenVK.

**Status: playable on Linux.** Both binaries build from this repository on Linux (`FuzionFrenzy`
for Linux, `FuzionFrenzy.exe` cross-compiled for Windows). The Linux build boots, plays the intro
movie, runs the menus and character select, and reaches the minigames and plays them, at a mean of
59.9 fps over 199 s of driven play (worst second 29.9). Characters render skinned and textured, and
the in-game clock runs at real time.

The Windows exe is cross-compiled here and has **never been run on Windows**; under Wine 11.20 it
reaches a minigame in play with zero shader failures. Audio has never been verified. One arena's
floor shades flat white - a known, unfixed defect recorded in `docs/render-defect.md`.

On Apple Silicon the same tree builds natively (arm64, MoltenVK, no Rosetta). It boots, opens a
borderless window, and draws past the point where MoltenVK used to lose the device (about 53
seconds, one command buffer holding ~8500 draws). Draws flush every 4096, and that submit waits.
A scripted run stayed up through 190 seconds. From 75 seconds the heavy scene averaged 58 fps
(median about 59) at up to about 12800 draws, with a few seconds near 48 when host draw passed
the vsync budget. Swapchain pictures, deleted after they were looked at, showed character
select, the main menu, a get-ready screen, a minigame in progress, and a round-over results
screen. The window blit is a single texture. `HANDOFF.md` has the run notes, the power numbers,
and the ports that were read for the fault.

The exact state, with evidence, is in `docs/status.md`; the first boot and what it proved is in
`docs/boot-crash.md`.

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
| Native macOS arm64 build (Vulkan via MoltenVK + SDL3) | boots, menus, and a minigame; heavy scenes ~58 fps. A local launcher builds the game from the player's disc (`HANDOFF.md`) |
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
               align_symbols.py, merge_symbols.py, and the screenshot/driving helpers.
               tools/apply-macos-sdk.sh applies the macOS patch; tools/hooks/pre-commit
               refuses commits of game data
patches/       sdk-macos-arm64.patch, applied on Darwin only, onto the pinned sdk/ submodule
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

   **macOS, Apple Silicon** (native arm64, Vulkan via MoltenVK + SDL3):

   Prerequisites:

   - Xcode Command Line Tools (`xcode-select --install`). The compiler must be arm64. Do not
     build this target under Rosetta.
   - Homebrew packages: `brew install cmake ninja openssl@3 glslang libusb molten-vk vulkan-loader vulkan-headers`
   - `libusb` is how a wired Xbox One controller is opened. macOS has no kernel
     driver for that pad, and GameController only sees it over Bluetooth. The
     build links the Homebrew dylib. No kernel extension.
   - A Vulkan ICD. Those Homebrew packages are enough: MoltenVK's ICD is
     `$(brew --prefix)/etc/vulkan/icd.d/MoltenVK_icd.json`, and the game points the loader at it
     when `VK_DRIVER_FILES` is unset. The [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#mac)
     is the other way to get MoltenVK, and it also ships DXC (`libdxcompiler.dylib`). With
     `VULKAN_SDK` set, the build looks there for DXC headers and the game prefers DXC over glslang.
     glslang from Homebrew is enough to compile shaders when DXC is absent.
   - `xdvdfs` is not a Homebrew formula. Install it with `cargo install xdvdfs-cli` if you want
     that extractor. This repository's `tools/xiso_extract.py` reads a disc image without it.

   The `sdk/` submodule has to be checked out (`git submodule update --init sdk`). On Darwin,
   `./build.sh` and CMake both run `tools/apply-macos-sdk.sh`, which applies
   `patches/sdk-macos-arm64.patch` onto that submodule. The patch is not applied on Linux or
   Windows, so those builds keep the submodule as upstream pinned it. Re-running the script does
   nothing once the patch is present.

   ```
   git submodule update --init sdk
   ./build.sh
   ```

   `./build.sh` with no argument selects `macos-release` on Darwin and `linux-release` on Linux.
   The executable is `out/build/macos-release/RingHost`. Run it from anywhere; it walks up
   from its own directory to find `assets/`. `file` on the binary should report
   `Mach-O 64-bit executable arm64`. Linux and Windows still build `FuzionFrenzy`. The
   Mac process is named Ring Host so the dock and the window are not the game's title.
   The log prefix on Mac is `ringhost`.

   GitHub Actions (`.github/workflows/ci.yml`, `docs/ci.md`) builds the SDK
   tools, the runtime, the math digest, and det-diff on Ubuntu and on macOS 14.
   It does not use a disc image. The digest on every runner must match
   `tools/fp-digest.expected`.

   `./build.sh macos-asan` is the ASan+UBSan preset (RelWithDebInfo, `-O1`).
   `tools/asan/autostart.sh` runs a muted scripted session on `--instance 5`.
   What that build ignores on purpose, and why MSan is not a macOS preset, is
   in `docs/sanitizers.md`.

   Tracy is off unless the build asks for it (`./build.sh macos-tracy`, or
   `-DXBE_TRACY=ON`). Zones, the Metal debugger, and Instruments are in
   `docs/profiling.md`.

   A release run is quiet unless something fails. `XBE_PERF=1` prints a `perf:` line once a
   second (the macOS patch logs those at report level, so they show up in a Release build).
   `XBE_MUTE=1` silences SDL audio. How far a run gets, and what is still open, is in
   `HANDOFF.md`.

   `macos/launcher/package-dmg.sh` builds a player app and a compressed disk image at
   `macos/launcher/dist/` (gitignored): `Ring Host.app` and `RingHost-macos.dmg`. The
   image holds the SwiftUI launcher and this port's sources. The player points it at
   their own USA disc image or extracted folder. The launcher checks the `default.xbe`
   SHA-1, copies the port into `~/Library/Application Support/RingHost`, applies the
   Darwin patch, and runs `./build.sh macos-release` there. The game binary
   (`RingHost`) and `settings.toml` stay in that folder and are rebuilt when the port
   version inside the app changes. An older `Application Support/FuzionFrenzy` folder
   is not reused. MoltenVK, cmake, and ninja are detected, with the Homebrew line above
   when one is missing. The first compile downloads SDL. The app is ad-hoc signed.
   Notarization is future work. The icon is drawn by the package script: a black
   field, a yellow rule, and a yellow ring.

   The window is black. The wordmark is RING / HOST in Helvetica Neue Condensed Black,
   with a yellow rule that ends in a plain ring. Menu type is Helvetica Neue with a
   short same-color shadow, about one old 480p pixel, so it is slightly soft and still
   readable. The current row is white and the others are a dusty blue, with faint blue
   digits along the right edge. The drawing is original. The pages are play, picture,
   sound, controls, and disc.

   Picture writes `display_mode`, size, `fps_cap` (30 is the low-power cap, 60 is the
   default), `vsync`, `render_percent`, `aspect_ratio`, the ultrawide flag (extra-wide
   drawing still waits on `SIMSBO_HORPLUS`), and `anisotropic_filtering`. Sound off sets
   `XBE_MUTE=1`. The scripted pad is off unless chosen, and then sets `FUZION_AUTOSTART=1`
   without touching `FUZION_AUTOSTART_PLAY_MS`. System link writes `join_ip`. The keyboard
   map is the table above and is shown as fixed. HDR stays off. Disc movies stay 640×480.

   The same `settings.toml`, `logs/` and `hdd/` as on Linux are created beside the executable.
   HDR settings are ignored. Keyboard and pad mapping is the table above.

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
material. `assets/`, `generated/`, `*.iso`, `*.xiso`, `*.xbe` and `out/` are ignored by git for that
reason. Install the commit hook so a slip is rejected rather than only ignored:

```
git config core.hooksPath tools/hooks
```

or `ln -sfn ../../tools/hooks/pre-commit .git/hooks/pre-commit`. The hook refuses `assets/`,
`generated/`, build output, recompiled guest sources, `macos/launcher/dist/`, and
`*.xbe` / `*.iso` / `*.xiso` / dumps / `*.dmg`.
`core.hooksPath` replaces any other hooks in `.git/hooks`. This repository ships no other hooks.

The SDK is MIT licensed; see `sdk/LICENSE` and `sdk/THIRD_PARTY.txt`. This repository adds no
license of its own. The macOS patch was written for this port. Its Darwin memory behaviour (an
unnamed `shm_open` object for the physical aliases, and 4 KB guest pages on a 16 KB host page)
follows the approach described by [DirectXbox-Recomp](https://github.com/phobos665/DirectXbox-Recomp)
(`src/platform/posix_memory.c`), which is GPL-3.0. That source was used as a reference and is not
copied here. DirectXbox-Recomp is a different recompiler; this game stays on the XboxXBERecomp SDK
so the Linux and Windows builds are unchanged.
