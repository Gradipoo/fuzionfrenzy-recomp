#!/bin/sh
# Muted ASan+UBSan self-test. Builds nothing. The binary is macos-asan.
#
# Wall clock, not frames: FUZION_AUTOSTART holds buttons by steady_clock.
# The default 210s covers the 75s menu script and one minigame on the
# release build's timing. A sanitizer build is slower; pass a longer
# duration as the first argument if the log never leaves the menus.
#
# Instance 5 writes out/build/macos-asan/hdd5. It does not touch hdd/,
# hdd2, hdd3, or hdd4. Frames go to out/build/macos-asan/asan-frames,
# which is under out/ and is not committed.
set -eu
cd "$(dirname "$0")/../.."
BIN="out/build/macos-asan/RingHost"
if [ ! -x "$BIN" ]; then
    echo "autostart: missing $BIN" >&2
    echo "autostart: ./build.sh macos-asan" >&2
    exit 1
fi
DUR="${1:-210}"
case "$DUR" in
    ''|*[!0-9]*) echo "autostart: duration must be seconds" >&2; exit 1 ;;
esac
LOG="out/build/macos-asan/logs"
mkdir -p "$LOG"
RUNLOG="$LOG/asan-autostart.log"
export XBE_MUTE=1
export FUZION_AUTOSTART=1
export FUZION_MINIMIZED=1
export XBE_PERF=1
export XBE_CAPTURE_DIR="asan-frames"
export XBE_VK_CAPTURE_EVERY="${XBE_VK_CAPTURE_EVERY:-300}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=1:detect_leaks=0:symbolize=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1:suppressions=$(pwd)/tools/asan/ubsan.supp}"
# shellcheck disable=SC2097,SC2098
"$BIN" --instance 5 >"$RUNLOG" 2>&1 &
pid=$!
i=0
while [ "$i" -lt "$DUR" ]; do
    if ! kill -0 "$pid" 2>/dev/null; then
        break
    fi
    i=$((i + 1))
    sleep 1
done
rc=0
if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    # The window's process group. Give it a moment, then hard-stop.
    sleep 2
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || rc=$?
    echo "autostart: stopped after ${DUR}s (child status ${rc})"
    echo "autostart: log $RUNLOG"
    exit 0
fi
wait "$pid" 2>/dev/null || rc=$?
echo "autostart: process exited at ${i}s with status ${rc}"
echo "autostart: log $RUNLOG"
if [ "$rc" -ne 0 ]; then
    exit "$rc"
fi
exit 0
