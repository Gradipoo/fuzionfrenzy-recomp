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
  (MoltenVK often lacks `OPAQUE`). When `VK_EXT_robustness2` is advertised, image and buffer
  robustness are enabled. On this Mac that is image-only (`robustBufferAccess2` is not
  advertised). It does not stop the page fault below.
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
  1.2 MB, consistent with the intro movie). From about 13 s the title is a rendered scene:
  hundreds to a few thousand draws per frame, about 60 fps, GPU time about 3 ms. A
  host-resolution readback of that scene was a picture, not a black frame. The guest-sized
  readback of the same present was all zeros; that capture path is wrong, the blit source
  was not.
- The title opened its music package while this was running. The test set `XBE_MUTE=1`, so
  playback was not listened to.
- Guest threads run. The main thread stays in the Cocoa event loop. An earlier build aborted
  there because a guest thread called `SDL_PumpEvents`; that call is skipped on Apple now.

## What is broken / open

- At about 53 s, every run so far, the GPU resets. MoltenVK logs
  `VK_ERROR_OUT_OF_DEVICE_MEMORY` / lost device, and the Metal callback is
  `kIOGPUCommandBufferCallbackErrorInnocentVictim` ("Discarded (victim of GPU error/recovery)").
  The guilty command buffer is not in that line. A `gpuEvent` report for one of the runs says
  `restart_reason_desc` "BIF0 page fault" (a read). After that, `vkQueueSubmit` returns
  `VK_ERROR_DEVICE_LOST` (-4) and present time in the perf log collapses, while the guest
  keeps scheduling frames that no longer display. Robust image access is on and did not
  prevent it, so this is an unmapped GPU address (a freed or never-bound resource, or a
  buffer read — `robustBufferAccess2` is not advertised on this device), not an in-range
  image sample.
- Menus and minigames are not confirmed. `FUZION_AUTOSTART` only switches to stick-and-A
  play at 75 s, which is after the device is already lost. The 60 fps scene before the fault
  is still the opening, not a confirmed minigame.
- MoltenVK does not implement every feature the Linux backend uses (some BC layouts,
  `D24_UNORM_S8`, RGB32 float images, non-solid fill). The device only enables features it
  advertises; the depth format falls through the existing loop. This run's shaders and
  pipelines were created (no `vkCreate*` errors before the page fault).
- 16 KB protection is coarser than the guest's 4 KB commits. A title that depends on a fault
  from a 4 KB hole inside a committed host page will not fault. This run did not hit that.
- No HDR. No image-to-image handover (no abstract-namespace sockets, no `pidfd`). This title
  is one XBE, so handover is not on the boot path.
- Screen recording is not permitted for this environment, so the window itself was not
  captured. The picture check used the backend's own host-target readback, then those files
  were deleted. Do not commit them.
- Linux and Windows were not rebuilt here. Their sources are the unpatched submodule. A Linux
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
`FUZION_AUTOSTART`). Leave `XBE_VK_DEBUG` unset unless you mean to; the README records that
the validation layer has frozen a machine.

## Next steps

1. Identify the resource behind the BIF0 page fault at ~53 s. The MoltenVK line is only the
   innocent command buffer. A Metal GPU capture, or shader validation
   (`MTL_SHADER_VALIDATION=1`) if the fault is actually a bounds error, should name it.
   Suspects: a texture or buffer destroyed while the GPU still reads it, or a buffer read
   past its allocation (`robustBufferAccess2` is unavailable here). Do not treat a guest CPU
   fault as the first theory; this run did not log one.
2. Once the device stays up past 75 s, rerun with `FUZION_AUTOSTART=1` and confirm menus and
   a minigame the same way Linux did. `XBE_PERF=1` is enough to see fps and draw counts.
3. The guest-sized frame dump (`front_NNNNN.bmp` from `XBE_VK_CAPTURE_EVERY`) came back
   black while `front_NNNNN_host.bmp` had the picture. Fix that readback only if you need
   guest-resolution captures. Keep capture output outside the repo and delete it.
4. Keep the patch the only SDK delta. Do not commit a new `sdk` gitlink, and do not commit
   `generated/`, `assets/`, `logs/`, or pictures.
