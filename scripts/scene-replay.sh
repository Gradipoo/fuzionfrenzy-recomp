#!/bin/sh
# Resume the local scene snapshots and compare them to the gameplay2 log.
# Exits 0 with "skipped" when the disc image or det-snaps/ are absent.
# Usage: sh scripts/scene-replay.sh [path-to-FuzionFrenzy-or-RingHost]
set -eu
cd "$(dirname "$0")/.."
bin="${1:-}"
if [ -z "$bin" ]; then
    if [ -x out/build/macos-release/RingHost ]; then
        bin="out/build/macos-release/RingHost"
    elif [ -x out/build/linux-release/FuzionFrenzy ]; then
        bin="out/build/linux-release/FuzionFrenzy"
    fi
fi
if [ -n "${FUZION_HDD_SNAPSHOT:-}" ] && [ -n "$bin" ] && [ -d "${FUZION_HDD_SNAPSHOT}" ]; then
    hdd="$(dirname "$bin")/hdd"
    mkdir -p "$hdd"
    rsync -a --delete "${FUZION_HDD_SNAPSHOT}/" "$hdd/"
    find "$hdd" \( -name '._*' -o -name '.DS_Store' \) -delete
fi
root="$(pwd)"
build=""
for c in out/build/macos-release out/build/linux-release; do
    if [ -x "$c/test_scenes" ]; then
        build="$c"
        break
    fi
done
if [ -z "$build" ]; then
    echo "scene-replay: test_scenes is not built, skipped"
    exit 0
fi
exec "$build/test_scenes" ${bin:+"$bin"}
