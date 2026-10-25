# macOS arm64 handoff

Native Apple Silicon build of this static recompilation. No game files belong in git.
`assets/`, `generated/`, and `out/` stay on the machine that owns the disc.

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

brew install cmake ninja openssl@3 glslang molten-vk vulkan-loader vulkan-headers
# optional: LunarG Vulkan SDK, then `source` its setup script so VULKAN_SDK is set
# xdvdfs, only if you want that extractor (there is no brew formula):
#   cargo install xdvdfs-cli

./build.sh
# equivalent:
#   ./tools/apply-macos-sdk.sh
#   cmake --preset macos-release
#   cmake --build --preset macos-release

file out/build/macos-release/FuzionFrenzy    # Mach-O 64-bit executable arm64
out/build/macos-release/FuzionFrenzy
```

Logs land in `out/build/macos-release/logs/`. That directory is gitignored. Do not commit it,
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
`macos/launcher/dist/Fuzion Frenzy.app` plus
`macos/launcher/dist/FuzionFrenzy-macos-launcher.dmg`. The script copies tracked files
with `git ls-files`, then replaces `sdk/` with `git archive` of the pinned commit so the
image does not carry this machine's patched worktree. `tools/apply-macos-sdk.sh` applies
the patch at build time, and if that sdk has no `.git` it runs `git init` first. The
packaging check refuses an xbe, an iso, recompiled guest sources, or a `FuzionFrenzy`
binary inside the app. The product directory and `*.dmg` are gitignored, and the
pre-commit hook refuses them. Ad-hoc `codesign` only. Notarization is future work.
`hdiutil create -format UDZO` warns that it is deprecated and still writes the image.

The app bundle id is `local.fuzionfrenzy.launcher`. The executable is
`FuzionFrenzyLauncher`, so it does not collide with the game. There is no sandbox, so
the chosen path is stored as a string (`launcher.json` in the support folder). A
security-scoped bookmark is best-effort.

Support folder: `~/Library/Application Support/FuzionFrenzy`, or `--support` for a test
directory. Layout: `src/` (port copy), `src/assets` as a symlink to the chosen folder
or to `disc/` after an iso extract, `src/out/build/macos-release/FuzionFrenzy` with
`settings.toml` beside it, `logs/fuzion.log`, `launcher.json`, `build.log`,
`built-version`. The game walks up from the executable to find `assets/`. Children get
`/opt/homebrew/bin` and `/usr/local/bin` on `PATH`. `GIT_TERMINAL_PROMPT=0`.

USA check: SHA-1 of `default.xbe` equals `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad`,
magic `XBEH`, and at least one other non-iso file in the folder. A lone xbe is
rejected. An `.iso` is accepted, extracted with the bundled `tools/xiso_extract.py`,
then checked the same way. No disc image was on this machine, so that extract was not
run against a real iso. Prerequisites are detected and not bundled: arm64, clang, git,
cmake >= 3.25, ninja, python3, the MoltenVK ICD, and the glslang header plus its three
libraries. openssl is not required. Capstone is unused. The install line is
`brew install cmake ninja glslang molten-vk vulkan-loader vulkan-headers`.

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

The window is a black field. The wordmark is the two words in a heavy system face,
yellow, with a yellow rule and a plain yellow ring. Menu rows are lowercase. The
current one is white and the rest are a dusty blue. Faint blue digits and a few
outlined shapes sit on the right. A thin yellow bar shows compile progress. The
selected page lives on the model (`@Published`) because the Command Line Tools
`swiftc` has no SwiftUI macro plugin, so `@State` does not compile here.

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
