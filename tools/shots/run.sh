#!/bin/sh
# Capture or compare fixed-step frames. References and outputs stay in
# screen-ref/ and screen-out/ (gitignored). This does not run in CI.
#
#   tools/shots/run.sh capture
#   tools/shots/run.sh compare
#
# SHOT_PADS   replay file (required when a scene has a present index)
# SHOT_BIN    RingHost (default out/build/macos-release/RingHost)
# SHOT_SEED   default 1
# SHOT_INSTANCE default 6 (writes that build's hdd6, not hdd/)
# SHOT_THRESHOLD mean absolute RGB difference, default 4
# SHOT_SCENES default tools/shots/scenes.txt
# SHOT_REF / SHOT_OUT override the two directories
set -eu
cd "$(dirname "$0")/../.."

mode=${1:-}
if [ "$mode" != capture ] && [ "$mode" != compare ]; then
    echo "usage: tools/shots/run.sh capture|compare" >&2
    exit 2
fi

abs() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$(pwd)/$1" ;;
    esac
}
bin=$(abs "${SHOT_BIN:-out/build/macos-release/RingHost}")
pads=${SHOT_PADS:-}
if [ -n "$pads" ]; then
    pads=$(abs "$pads")
fi
seed=${SHOT_SEED:-1}
instance=${SHOT_INSTANCE:-6}
threshold=${SHOT_THRESHOLD:-4}
scenes=${SHOT_SCENES:-tools/shots/scenes.txt}
ref=$(abs "${SHOT_REF:-screen-ref}")
out=$(abs "${SHOT_OUT:-screen-out}")
work="$out/work"

if [ ! -x "$bin" ]; then
    echo "shots: missing $bin" >&2
    exit 2
fi

mkdir -p "$ref" "$out"
report="$out/report.txt"
: >"$report"
fail=0
ran=0
skipped=0

while read -r name present rest; do
    [ -n "$name" ] || continue
    case "$name" in \#*) continue ;; esac
    if [ "$present" = "-" ] || [ -z "$present" ]; then
        echo "SKIP $name unmeasured" | tee -a "$report"
        skipped=$((skipped + 1))
        continue
    fi
    case "$present" in
        ''|*[!0-9]*) echo "shots: bad present for $name" >&2; exit 2 ;;
    esac
    if [ -z "$pads" ] || [ ! -f "$pads" ]; then
        echo "shots: SHOT_PADS must be a replay file before capturing $name" >&2
        exit 2
    fi
    ran=$((ran + 1))
    rm -rf "$work"
    mkdir -p "$work"
    # The render thread draws the previous present while the guest records the
    # next one, and the frame limit calls _Exit without waiting for the present
    # still queued. One extra guest frame lets present N finish and flush its BMP.
    # Capture every present only when the index is 0: a non-zero interval skips
    # present 0's modulo and still writes present N, without a read-back per frame.
    frames=$((present + 2))
    every=$present
    if [ "$every" -eq 0 ]; then
        every=1
    fi
    echo "shots: $mode $name present $present"
    XBE_MUTE=1 \
    FUZION_MINIMIZED=1 \
    FUZION_FIXED_STEP=1 \
    FUZION_SEED="$seed" \
    FUZION_REPLAY_INPUT="$pads" \
    FUZION_FRAMES="$frames" \
    XBE_CAPTURE_DIR="$work" \
    XBE_VK_CAPTURE_EVERY="$every" \
        "$bin" --instance "$instance" >"$out/$name.log" 2>&1 || {
            echo "FAIL $name process exited" | tee -a "$report"
            fail=1
            continue
        }
    shot=$(printf '%s/front_%05d_host.bmp' "$work" "$present")
    if [ ! -f "$shot" ]; then
        echo "FAIL $name no frame $shot" | tee -a "$report"
        fail=1
        continue
    fi
    if [ "$mode" = capture ]; then
        cp "$shot" "$ref/$name.bmp"
        echo "CAPTURE $name $ref/$name.bmp" | tee -a "$report"
        continue
    fi
    if [ ! -f "$ref/$name.bmp" ]; then
        echo "FAIL $name no reference" | tee -a "$report"
        fail=1
        continue
    fi
    cp "$shot" "$out/$name.bmp"
    if python3 tools/shots/compare.py --threshold "$threshold" "$ref/$name.bmp" "$out/$name.bmp" | tee -a "$report" | grep -q '^PASS'; then
        echo "PASS $name" | tee -a "$report"
    else
        echo "FAIL $name" | tee -a "$report"
        fail=1
    fi
done <"$scenes"

echo "shots: report $report"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
if [ "$ran" -eq 0 ]; then
    echo "shots: no measured presents in $scenes" | tee -a "$report"
    exit 2
fi
exit 0
