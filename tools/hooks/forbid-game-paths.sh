#!/bin/sh
# Exit 1 if any path is game data, recompiled guest code, or build output.
# Paths are arguments, or one per line on stdin. --self-test checks the
# rules and that workflow files do not name those paths.
set -eu

refuse() {
    echo "forbid-game-paths: $1" >&2
    return 1
}

check_one() {
    f=$1
    case "$f" in
        sdk|sdk/*)
            refuse "refusing an sdk submodule change: $f"
            return 1
            ;;
    esac
    case "$f" in
        assets|assets/*|generated|generated/*|out|out/*|build|build/*|\
        logs|logs/*|hdd|hdd/*|CrashDumps|CrashDumps/*|shadercache|shadercache/*|\
        tools/bin|tools/bin/*|.toolchain|.toolchain/*|\
        macos/launcher/dist|macos/launcher/dist/*)
            refuse "refusing game, asset, or build path: $f"
            return 1
            ;;
    esac
    case "$f" in
        *.xbe|*.XBE|*.iso|*.ISO|*.xiso|*.XISO|*.dmp|*.DMP|*.d3dcap|*.pdb|*.exe|*.dmg|*.DMG)
            refuse "refusing disc, dump, binary, or disk image: $f"
            return 1
            ;;
    esac
    case "$f" in
        det-logs|det-logs/*|*/det-logs|*/det-logs/*|\
        det-desync|det-desync/*|*/det-desync|*/det-desync/*|\
        *.pads|*.detlog|desync_*.txt|*/desync_*.txt|frame_*_*.bin|*/frame_*_*.bin)
            refuse "refusing determinism log derived from game RAM: $f"
            return 1
            ;;
    esac
    case "$f" in
        re|re/*|*.gpr|*.rep|*.rep/*|*.gzf|time-rng-map.txt|*/time-rng-map.txt)
            refuse "refusing Ghidra project, export, or symbol dump: $f"
            return 1
            ;;
    esac
    case "$f" in
        *x86_recomp*|*x86_func_mapping*|*image_symbols.h)
            refuse "refusing recompiled guest code: $f"
            return 1
            ;;
    esac
    return 0
}

self_test() {
    fail=0
    for bad in \
        assets/default.xbe \
        generated/game/x86_recomp.0.cpp \
        generated/game/image_symbols.h \
        re/ghidra/project.gpr \
        notes.pads \
        det-logs/a.txt \
        frame_1_D.bin \
        disc.iso \
        sdk/runtime/cpu/softmath.cpp
    do
        if check_one "$bad"; then
            echo "self-test: allowed $bad" >&2
            fail=1
        fi
    done
    if ! check_one "README.md"; then
        echo "self-test: refused README.md" >&2
        fail=1
    fi
    if ! check_one "tools/ci/stub_symbols.py"; then
        echo "self-test: refused the CI stub generator" >&2
        fail=1
    fi
    # Workflow steps must not name the disc, the recompiled tree, or captures.
    for yml in .github/workflows/*.yml; do
        [ -f "$yml" ] || continue
        if grep -E 'assets/|\.xbe|generated/|det-logs|\.pads|captures/|default\.xbe' "$yml"; then
            echo "self-test: $yml names a game-derived path" >&2
            fail=1
        fi
    done
    if [ "$fail" -ne 0 ]; then
        exit 1
    fi
    echo "forbid-game-paths: self-test PASS"
    exit 0
}

if [ "${1:-}" = "--self-test" ]; then
    self_test
fi

fail=0
if [ "$#" -gt 0 ]; then
    for f in "$@"; do
        check_one "$f" || fail=1
    done
else
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        check_one "$f" || fail=1
    done
fi

if [ "$fail" -ne 0 ]; then
    echo "forbid-game-paths: blocked. Game files and recompiled output stay on this machine." >&2
    exit 1
fi
exit 0
