# CI

`.github/workflows/ci.yml` runs on Ubuntu x86_64, macOS 14 arm64, and
Windows. It does not download, build, or upload a disc image, recompiled
guest code, or captures. Nothing in that workflow names those paths.
`tools/hooks/forbid-game-paths.sh` is what `tools/hooks/pre-commit` uses,
and the Ubuntu job runs it on the diff against the pull request base (or
the default branch, on a push).

ccache is the C and C++ compiler launcher when `ccache` is on `PATH`.
`CMakeLists.txt` sets that before `project()`. Each job sets `CCACHE_DIR`
to `.ccache` in the workspace and restores it with `actions/cache`. The
configure step fails if the C++ launcher in `CMakeCache.txt` is not ccache.

What each runner builds:

| Step | Needs a disc image |
| --- | --- |
| `scripts/fp-digest-host.sh` | No. Native clang only. The printed hash must be `tools/fp-digest.expected` (`06a5a5d542b715f9`). |
| `scripts/det-diff.py --self-check` | No. Synthetic logs. |
| `XBE_HOST_TOOLS_ONLY` | No. `xbe_recomp` and `xbe_symbols`. |
| `XBE_CI_RUNTIME` | No. The runtime is compiled against a synthetic symbol header written in the build directory by `tools/ci/stub_symbols.py`. Those tokens are not guest addresses. On Linux the job sets `SDL_UNIX_CONSOLE_BUILD` so SDL3 configures with X11 and Wayland off. On macOS 14, `patches/sdk-macos-pipe.patch` replaces `pipe2` (the 14 SDK does not declare it) with `pipe` and `FD_CLOEXEC` for the relaunch handover. |
| Launcher `swiftc` | No, and only on macOS. The disk-image pack stays a local script. |
| `ctest -L fast` | No. `det_clock`, `xinput_packet`, `arena`, `fp_digest`, and `det_diff`. The scene resume test is label `scenes` and is not in this job. |

Windows runs the runtime, the math digest, `det-diff.py --self-check`, and
`ctest -L fast`. It does not build the host tools or the launcher. The
runtime links the Windows SDK libraries (`d3d12`, `xaudio2`, and the rest)
instead of SDL and Vulkan.

Ubuntu also runs the digest under valgrind (`FP_VALGRIND=1`). That is the
uninitialized-use check. MemorySanitizer is not used: it needs an
instrumented libc, and a job without one false-fails. ASan on macOS does not
see uninitialized reads either (`docs/sanitizers.md`).

`scripts/fp-cross.sh` is the local cross-arch check (arm64, x86_64 under
Rosetta, gcc when it is installed). CI does not run it. Each runner hashes
its own native binary and compares that hash to the same file.
