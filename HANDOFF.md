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
- On Apple, `Draw()` calls `Flush()` before draws 1025, 2049, and so on. One command
  buffer that records ~8500 draws faults a GPU read even when each draw is in range.
  The same draws split across submits do not. 1024 stays under the 8192-set segment
  and the upload ring. Metal argument buffers stay enabled.
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
- With the 1024-draw split, a run on 2026-10-25 stayed up through 99 s. The frame that
  used to kill the device (~53 s, about 8500 draws) ran at about 45–50 fps. After the
  scripted pad's 75 s play phase the draw count dropped to about 600 at 60 fps, then a
  heavier scene held about 10000 draws at about 40 fps. No `VK_ERROR_DEVICE_LOST`, no
  skipped draws, no pipeline failures, no upload overflow.
- A later run with `mapIO()` applied stayed up through at least 73 s. The ~53 s frame
  dropped to about 48 fps and was back at 60 fps by 58 s. Render-target readbacks at
  presents 1500 and 3000 were not blank, at host size and at guest size (present 0 is
  still all zeros). Those files were deleted.
- The one-texture present shader drew at about 60 fps through 21 s with zero pipeline
  misses. The present-900 readback was not blank, host and guest. That process then
  left with `Exit (0)` (the SDL quit path, not a device loss). The startup
  `cannot reserve 'texture'` line on that run named `lut`, the previous present
  shader, replayed while the pipeline cache was created. The new module does not
  contain that texture.
- Menus and a minigame were not confirmed from a picture. Draw counts move the way a
  title does when the scripted pad leaves the opening and enters play.
- The title opened its music package while this was running. The test set `XBE_MUTE=1`, so
  playback was not listened to.
- Guest threads run. The main thread stays in the Cocoa event loop. An earlier build aborted
  there because a guest thread called `SDL_PumpEvents`; that call is skipped on Apple now.

## What is broken / open

- The heavy scene after 75 s runs at about 40 fps. Each 1024-draw `Flush()` waits for the
  GPU, so a 10000-draw frame pays several stalls. 2745 draws in one buffer stayed alive in
  a cap test; 8500 did not. 1024 is the split that was measured with the full draw count.
- The window blit and the render target are different images. The readbacks above are
  the render target. A cache-clean run (delete `shadercache/*/pipeline.bin` first) is
  what shows whether the new present shader compiles without the `lut` line. The
  swapchain itself was not read back, and screen recording is blocked here.
- Menus and a minigame are not confirmed from a picture. The post-75 s heavy scene was
  measured on the binary from before the one-texture blit. One short run of the blit
  binary exited 0 at 22 s; that was not a GPU fault, and it has not been repeated past
  the old 53 s point on that binary yet.
- MoltenVK does not implement every feature the Linux backend uses (some BC layouts,
  `D24_UNORM_S8`, RGB32 float images, non-solid fill). The device only enables features it
  advertises; the depth format falls through the existing loop.
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

## Next steps

1. Delete `out/build/macos-release/shadercache/*/pipeline.bin`, rerun, and confirm the
   log has no `cannot reserve 'texture'` line and no `lut` in that MoltenVK error.
   One host readback past 53 s, then delete the bmp. Do not commit it.
2. A host-resolution picture of the post-75 s scene is what would confirm a minigame.
   Draw counts on the earlier binary already show that scene running with the device
   up. Leave `FUZION_AUTOSTART_PLAY_MS` at 75000; that default is shared with Linux.
3. If the heavy scene needs to be faster, try a split of 2048 draws. Do not jump to 4096
   without a run: 2745 in one buffer survived a cap test of an earlier scene, and ~8500
   did not.
4. Keep the patch the only SDK delta. Do not commit a new `sdk` gitlink, and do not commit
   `generated/`, `assets/`, `logs/`, or pictures.
