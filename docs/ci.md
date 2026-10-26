# CI

`.github/workflows/ci.yml` runs on Ubuntu x86_64 and macOS 14 arm64. It does
not download, build, or upload a disc image, recompiled guest code, or
captures. Nothing in that workflow names those paths. `tools/hooks/forbid-game-paths.sh`
is what `tools/hooks/pre-commit` uses, and the Ubuntu job runs it on the diff
against the pull request base (or the default branch, on a push).

What each runner builds:

| Step | Needs a disc image |
| --- | --- |
| `scripts/fp-digest-host.sh` | No. Native clang only. The printed hash must be `tools/fp-digest.expected` (`06a5a5d542b715f9`). |
| `scripts/det-diff.py --self-check` | No. Synthetic logs. |
| `XBE_HOST_TOOLS_ONLY` | No. `xbe_recomp` and `xbe_symbols`. |
| `XBE_CI_RUNTIME` | No. The runtime is compiled against a synthetic symbol header written in the build directory by `tools/ci/stub_symbols.py`. Those tokens are not guest addresses. |
| Launcher `swiftc` | No, and only on macOS. The disk-image pack stays a local script. |

Ubuntu also runs the digest under valgrind (`FP_VALGRIND=1`). That is the
uninitialized-use check. MemorySanitizer is not used: it needs an
instrumented libc, and a job without one false-fails. ASan on macOS does not
see uninitialized reads either (`docs/sanitizers.md`).

`scripts/fp-cross.sh` is the local cross-arch check (arm64, x86_64 under
Rosetta, gcc when it is installed). CI does not run it. Each runner hashes
its own native binary and compares that hash to the same file.
