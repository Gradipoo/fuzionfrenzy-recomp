#!/bin/bash
# Determinism matrix. Muted, minimized, fixed step. Prints PASS or FAIL per row.
# Idle rows (a, b, c) need no controller. Row (d) needs det-logs/gameplay.pads.
# Does not delete hdd/ (instance 0 and 1). Wipes only hdd2, hdd3, and hdd4 beside the exe.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${FUZION_DET_BUILD:-$ROOT/out/build/macos-release}"
BIN="$BUILD/RingHost"
FRAMES="${FUZION_DET_FRAMES:-240}"
TIMEOUT="${FUZION_DET_TIMEOUT:-180}"
LOGS="$ROOT/det-logs"
REPLAY="$LOGS/idle.pads"
DIFF="$ROOT/scripts/det-diff.py"
FAILS=0

mkdir -p "$LOGS"

say() { printf '%s\n' "$*"; }

if [[ ! -x "$BIN" ]]; then
  say "FAIL binary missing: $BIN"
  exit 1
fi

python3 "$DIFF" --self-check || { say "FAIL diff tool self-check"; exit 1; }

python3 - "$REPLAY" "$FRAMES" <<'PY'
import sys
path, frames = sys.argv[1], int(sys.argv[2])
with open(path, "w") as f:
    f.write("# fuzion-input 2\n")
    f.write("# LogKey:frame|P1 c,buttons,lx,ly,rx,ry,a,b,x,y,black,white,lt,rt|P2 ...|P3 ...|P4 ...\n")
    f.write("# generated idle replay: four ports disconnected. Not from a game run.\n")
    idle = "0,0000,0,0,0,0,00,00,00,00,00,00,00,00"
    for frame in range(frames):
        f.write(f"{frame}|{idle}|{idle}|{idle}|{idle}\n")
PY

run_one() {
  local inst="$1" log="$2" stall="$3" replay="$4" frames="$5"
  rm -rf "$BUILD/hdd$inst"
  python3 - "$BUILD" "$inst" "$log" "$stall" "$replay" "$frames" "$TIMEOUT" <<'PY'
import os, subprocess, sys
build, inst, log, stall, replay, frames, timeout = sys.argv[1:]
env = os.environ.copy()
env.pop("FUZION_AUTOSTART", None)
env.pop("FUZION_RECORD_INPUT", None)
env["XBE_MUTE"] = "1"
env["FUZION_FIXED_STEP"] = "1"
env["FUZION_MINIMIZED"] = "1"
env["FUZION_FRAMES"] = frames
env["FUZION_REPLAY_INPUT"] = replay
env["FUZION_STATE_LOG"] = log
env["FUZION_SEED"] = "1"
env["FUZION_STALL_MS"] = stall
try:
    r = subprocess.run(
        ["./RingHost", "--instance", inst],
        cwd=build,
        env=env,
        timeout=float(timeout),
    )
except subprocess.TimeoutExpired:
    sys.stderr.write(f"timeout after {timeout}s instance {inst}\n")
    sys.exit(124)
sys.exit(r.returncode)
PY
}

note_log() {
  local inst="$1"
  local latest
  latest="$(ls -t "$BUILD"/logs/ringhost-"$inst"* 2>/dev/null | head -1 || true)"
  if [[ -n "${latest:-}" ]]; then
    say "last log lines ($latest):"
    tail -n 30 "$latest"
  fi
}

row() {
  local name="$1"
  shift
  if "$@"; then
    say "$name PASS"
  else
    say "$name FAIL"
    FAILS=$((FAILS + 1))
  fi
}

# (a) same build, same replay, twice in a row
a_seq() {
  local l1="$LOGS/a1.detlog" l2="$LOGS/a2.detlog"
  rm -f "$l1" "$l2"
  run_one 2 "$l1" 0 "$REPLAY" "$FRAMES" || { note_log 2; return 1; }
  run_one 2 "$l2" 0 "$REPLAY" "$FRAMES" || { note_log 2; return 1; }
  python3 "$DIFF" "$l1" "$l2"
}

# (b) two instances at once
b_par() {
  local l2="$LOGS/b2.detlog" l3="$LOGS/b3.detlog"
  rm -f "$l2" "$l3"
  run_one 2 "$l2" 0 "$REPLAY" "$FRAMES" &
  local p2=$!
  run_one 3 "$l3" 0 "$REPLAY" "$FRAMES" &
  local p3=$!
  local c2=0 c3=0
  wait "$p2" || c2=$?
  wait "$p3" || c3=$?
  if [[ "$c2" -ne 0 || "$c3" -ne 0 ]]; then
    note_log 2
    note_log 3
    return 1
  fi
  python3 "$DIFF" "$l2" "$l3"
}

# (c) artificial stall must still match a zero-stall run
c_stall() {
  local base="$LOGS/a1.detlog" stall="$LOGS/c-stall.detlog"
  if [[ ! -f "$base" ]]; then
    run_one 2 "$base" 0 "$REPLAY" "$FRAMES" || { note_log 2; return 1; }
  fi
  rm -f "$stall"
  run_one 4 "$stall" 30 "$REPLAY" "$FRAMES" || { note_log 4; return 1; }
  python3 "$DIFF" "$base" "$stall"
}

# (d) gameplay replay, if Sean has recorded one
d_play() {
  local pads="$LOGS/gameplay.pads"
  if [[ ! -f "$pads" ]]; then
    say "SKIP no $pads"
    say "Sean: record ports 1-2, leave 3-4 empty, through character select and Twisted System, Sumo Balls, and Jetstream."
    say "From out/build/macos-release, with no other flags:"
    say "  env -u FUZION_AUTOSTART XBE_MUTE=1 FUZION_FIXED_STEP=1 FUZION_SEED=1 \\"
    say "    FUZION_RECORD_INPUT=$pads FUZION_STATE_LOG=$LOGS/gameplay.detlog ./RingHost"
    say "Quit when the third minigame has started. Do not commit det-logs/."
    return 0
  fi
  local n
  n="$(python3 - "$pads" <<'PY'
import sys
last = 0
for line in open(sys.argv[1]):
    if not line or line[0] == "#":
        continue
    head = line.split("|", 1)[0].split()[0]
    last = max(last, int(head))
print(last + 1)
PY
)"
  # Cap a long session so the matrix stays bounded. 12000 presents is 200 s at 60 Hz.
  if [[ "$n" -gt 12000 ]]; then n=12000; fi
  local l1="$LOGS/d1.detlog" l2="$LOGS/d2.detlog"
  rm -f "$l1" "$l2"
  run_one 2 "$l1" 0 "$pads" "$n" || { note_log 2; return 1; }
  run_one 3 "$l2" 0 "$pads" "$n" || { note_log 3; return 1; }
  python3 "$DIFF" "$l1" "$l2"
}

say "matrix frames=$FRAMES timeout=${TIMEOUT}s binary=$BIN"
row "a sequential" a_seq
row "b parallel" b_par
row "c stall" c_stall
if [[ -f "$LOGS/gameplay.pads" ]]; then
  row "d gameplay" d_play
else
  d_play
  say "d gameplay SKIP"
fi

if [[ "$FAILS" -eq 0 ]]; then
  say "MATRIX PASS"
  exit 0
fi
say "MATRIX FAIL ($FAILS)"
exit 1
