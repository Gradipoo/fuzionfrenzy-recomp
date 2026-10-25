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
  (MoltenVK often lacks `OPAQUE`).
- Linker is Apple `ld64`. `clang -fuse-ld=lld` is rejected by the Command Line Tools clang.
  There are no strong `X86_FUNC` overrides in `src/game` yet; weak recompiled functions just
  need to link.
- Shaders: Homebrew glslang. DXC is used when `VULKAN_SDK` provides `dxc/dxcapi.h` and
  `libdxcompiler.dylib`.
- If `VK_DRIVER_FILES` is unset, `main` points the loader at Homebrew's MoltenVK ICD.

Presets: `macos-debug`, `macos-release`, `macos-relwithdebinfo`. `CMAKE_OSX_ARCHITECTURES=arm64`.
No `-march=x86-64-v2`.

## What works on this Mac

Recorded after the build in this checkout. Update this section when that changes.

- Toolchain present: Apple clang (arm64), CMake, Ninja, Homebrew `sdl3` (the build still
  fetches SDL3 3.4.16), `molten-vk` 1.4.2, `vulkan-loader` 1.4.363, `glslang` 16.6.0,
  OpenSSL 3. Page size 16 KB. `mmap` of 4 GB and `shm_open` + `MAP_FIXED` were checked with a
  throwaway program before the game build.
- Boot result: not recorded yet. Fill this in after `./build.sh` and a run.

## What is broken / open

- 16 KB protection is coarser than the guest's 4 KB commits. A title that depends on a fault
  from a 4 KB hole inside a committed host page will not fault.
- MoltenVK will not implement every Vulkan 1.2 feature the Linux backend uses (BC textures,
  `D24_UNORM_S8`, RGB32 float images, fill modes). The device only enables features it
  advertises; unsupported formats fall through the existing depth-format loop. Further
  rejections show up as validation or `vkCreate*` failures at runtime.
- No HDR. No image-to-image handover.
- Linux and Windows were not rebuilt here (this machine is an Apple Silicon Mac). Their
  sources are the unpatched submodule. A Linux configure is the check that the patch did not
  leak into the submodule SHA.
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

1. Finish the native Release build and run the executable. Note how far it gets: configure,
   compile, link, process start, window, intro, menus, minigame. Describe it without quoting
   game text or committing pictures.
2. If Vulkan device or swapchain creation fails, the log line is the next edit (almost always
   a MoltenVK feature or format). If the process dies in `mem::Init`, the 4 GB reservation or
   the shm aliases failed.
3. If it runs and then faults, `logs/` has the guest address. A fault on a guest address that
   `IsCommitted` says is free, inside a 16 KB page that also holds a commit, is the coarse
   page-size limit above.
4. Keep the patch the only SDK delta. Do not commit a new `sdk` gitlink, and do not commit
   `generated/`.
