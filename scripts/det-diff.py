#!/usr/bin/env python3
"""Diff two determinism state logs. Prints the first divergent frame and region.

Exit 0 when every logged frame matches. Exit 1 on a mismatch, a short log, or a
parse error. --self-check builds two synthetic logs and checks this tool.
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

F_RE = re.compile(
    r"^F\s+(\d+)\s+total=([0-9a-fA-F]+)\s+data=([0-9a-fA-F]+)\s+phys=([0-9a-fA-F]+)\s+"
    r"system=([0-9a-fA-F]+)\s+user=([0-9a-fA-F]+)\s+seed=([0-9a-fA-F]+)\s+"
    r"qpc=(\d+)\s+rdtsc=(\d+)\s+delay=(\d+)\s*$"
)
PAIR_RE = re.compile(r"(\S+)=([0-9a-fA-F]+)")


class Frame:
    def __init__(self, n):
        self.n = n
        self.total = None
        self.data = None
        self.phys = None
        self.system = None
        self.user = None
        self.seed = None
        self.qpc = "0"
        self.rdtsc = "0"
        self.delay = "0"
        self.sections = {}
        self.slices = []
        self.user_slices = {}


def parse(path):
    frames = {}
    order = []

    def touch(n):
        if n not in frames:
            frames[n] = Frame(n)
            order.append(n)
        return frames[n]

    text = Path(path).read_text(errors="replace").splitlines()
    for line in text:
        if not line or line[0] == "#":
            continue
        kind = line[0]
        if kind == "F":
            m = F_RE.match(line)
            if not m:
                raise SystemExit(f"bad F line in {path}: {line}")
            fr = touch(int(m.group(1)))
            fr.total, fr.data, fr.phys, fr.system, fr.user, fr.seed, fr.qpc, fr.rdtsc, fr.delay = (
                m.group(i) for i in range(2, 11)
            )
        elif kind == "D":
            parts = line.split()
            if len(parts) < 2:
                continue
            fr = touch(int(parts[1]))
            for name, val in PAIR_RE.findall(line):
                fr.sections[name] = val
        elif kind == "P":
            parts = line.split()
            if len(parts) < 3:
                continue
            fr = touch(int(parts[1]))
            fr.slices = parts[2:]
        elif kind == "U":
            parts = line.split(None, 2)
            if len(parts) < 3:
                continue
            fr = touch(int(parts[1]))
            for name, val in PAIR_RE.findall(parts[2]):
                fr.user_slices[name] = val
    order = [n for n in order if frames[n].total is not None]
    return frames, order


def cause(a, b, region):
    hints = []
    if a.seed != b.seed:
        hints.append("RNG (the seed field changed; the MSVC rand global, or srand after image load)")
    if a.qpc != b.qpc:
        hints.append("timer (QPC was read a different number of times)")
    if a.rdtsc != b.rdtsc:
        hints.append("RDTSC (the counter was read a different number of times)")
    if a.delay != b.delay:
        hints.append("KeDelayExecutionThread (the delay count differs)")
    if region == "system" and a.data == b.data and a.phys == b.phys and a.user == b.user and a.seed == b.seed:
        hints.append("thread or kernel object (only the system region differs)")
    if region.startswith(".") or region == "data":
        if a.seed == b.seed:
            hints.append("game global in a writable image section, seed unchanged")
        else:
            hints.append("game global following the RNG change")
    if region.startswith("phys_"):
        hints.append(
            "physical RAM (guest heap, kernel data, or a mixer buffer). "
            "GPU scratch and color/depth payloads are zeroed before the hash"
        )
    if region.startswith("user_"):
        hints.append("user allocation")
    if not hints:
        hints.append("uninitialized memory, or a host callback writing guest state")
    return "; ".join(hints)


def first_diff(fa, fb):
    if fa.data != fb.data:
        names = sorted(set(fa.sections) | set(fb.sections))
        for name in names:
            if fa.sections.get(name) != fb.sections.get(name):
                return name
        return "data"
    if fa.phys != fb.phys:
        n = max(len(fa.slices), len(fb.slices))
        for i in range(n):
            av = fa.slices[i] if i < len(fa.slices) else None
            bv = fb.slices[i] if i < len(fb.slices) else None
            if av != bv:
                return f"phys_{i:02d}"
        return "phys"
    if fa.system != fb.system:
        return "system"
    if fa.user != fb.user:
        names = sorted(set(fa.user_slices) | set(fb.user_slices), key=lambda s: int(s))
        for name in names:
            if fa.user_slices.get(name) != fb.user_slices.get(name):
                return f"user_{name}"
        return "user"
    if fa.seed != fb.seed:
        return "seed"
    if fa.qpc != fb.qpc or fa.rdtsc != fb.rdtsc or fa.delay != fb.delay:
        return "counters"
    if fa.total != fb.total:
        return "total"
    return None


def dump_hint(frame, region):
    return (
        f"rerun with FUZION_STATE_DUMP_FRAME={frame} FUZION_STATE_DUMP_REGION={region} "
        f"FUZION_STATE_DUMP=<dir> to write frame_{frame}_{region}.bin"
    )


def write_report(path, frame, region, why, fa, fb, hint):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "\n".join(
            [
                f"desync frame {frame}",
                f"region {region}",
                f"cause {why}",
                f"reference_total {fa.total}",
                f"local_total {fb.total}",
                f"reference_data {fa.data}",
                f"local_data {fb.data}",
                f"reference_phys {fa.phys}",
                f"local_phys {fb.phys}",
                f"reference_system {fa.system}",
                f"local_system {fb.system}",
                f"reference_user {fa.user}",
                f"local_user {fb.user}",
                f"reference_seed {fa.seed}",
                f"local_seed {fb.seed}",
                hint,
                "",
            ]
        )
    )


def compare(path_a, path_b, out, report_dir=None):
    fa, order_a = parse(path_a)
    fb, order_b = parse(path_b)
    if not order_a or not order_b:
        out.write(f"FAIL no frames in {path_a if not order_a else path_b}\n")
        return 1
    shared = [n for n in order_a if n in fb]
    if not shared:
        out.write("FAIL logs share no frame numbers\n")
        return 1
    totals = [fa[n].total for n in order_a]
    if len(totals) > 1 and len(set(totals)) == 1:
        out.write(
            "WARN total hash is identical on every frame of the first log. "
            "Time may not be moving the sim (a QPC delta of 0).\n"
        )
    for n in shared:
        region = first_diff(fa[n], fb[n])
        if region:
            why = cause(fa[n], fb[n], region)
            hint = dump_hint(n, region)
            out.write(f"FAIL frame {n} region {region}\n")
            out.write(f"cause {why}\n")
            out.write(hint + "\n")
            if report_dir is not None:
                report = Path(report_dir) / f"desync_{n}.txt"
                write_report(report, n, region, why, fa[n], fb[n], hint)
                out.write(f"report {report}\n")
            return 1
    if len(order_a) != len(order_b) or order_a[-1] != order_b[-1]:
        out.write(
            f"FAIL length {path_a} has {len(order_a)} frames (last {order_a[-1]}), "
            f"{path_b} has {len(order_b)} frames (last {order_b[-1]})\n"
        )
        return 1
    out.write(f"PASS {len(shared)} frames match\n")
    return 0


def self_check():
    def log(frames):
        lines = ["# det 1", "# F frame total data phys system user seed qpc rdtsc delay"]
        for fr in frames:
            n = fr["n"]
            lines.append(f"D {n} .data={fr['data']}")
            slices = " ".join(fr["slices"])
            lines.append(f"P {n} {slices}")
            lines.append(f"U {n} 0={fr['user']}")
            lines.append(
                f"F {n} total={fr['total']} data={fr['data']} phys={fr['phys']} system={fr['system']} "
                f"user={fr['user']} seed={fr['seed']} qpc={fr['qpc']} rdtsc={fr['rdtsc']} delay={fr['delay']}"
            )
        return "\n".join(lines) + "\n"

    base = []
    for n in range(4):
        base.append(
            {
                "n": n,
                "total": f"{n:016x}",
                "data": "11",
                "phys": "22",
                "system": "33",
                "user": "44",
                "seed": "00000001",
                "qpc": "1",
                "rdtsc": "1",
                "delay": "0",
                "slices": ["aa"] * 2 + [f"{n:016x}"] + ["bb"] * 61,
            }
        )
    other = [dict(fr, slices=list(fr["slices"])) for fr in base]
    other[3] = dict(other[3])
    slices = list(other[3]["slices"])
    slices[2] = "deadbeef"
    other[3]["slices"] = slices
    other[3]["phys"] = "99"
    other[3]["total"] = "ff"
    other[3]["seed"] = "00000002"
    with tempfile.TemporaryDirectory() as d:
        a = Path(d) / "a.detlog"
        b = Path(d) / "b.detlog"
        a.write_text(log(base))
        b.write_text(log(other))
        from io import StringIO

        buf = StringIO()
        rc = compare(a, b, buf, report_dir=d)
        text = buf.getvalue()
        report = Path(d) / "desync_3.txt"
        body = report.read_text() if report.exists() else ""
        if rc != 1 or "frame 3" not in text or "phys_02" not in text or "RNG" not in text or "desync frame 3" not in body:
            sys.stderr.write(text + body)
            raise SystemExit("self-check failed")
        same = StringIO()
        if compare(a, a, same) != 0 or "PASS" not in same.getvalue():
            raise SystemExit("self-check same-file failed")
    print("self-check PASS")
    return 0


def main():
    p = argparse.ArgumentParser(description="Diff two det state logs")
    p.add_argument("a", nargs="?")
    p.add_argument("b", nargs="?")
    p.add_argument("--self-check", action="store_true")
    p.add_argument(
        "--report-dir",
        help="on a mismatch, write desync_<frame>.txt here (OpenRCT2 desync log)",
    )
    args = p.parse_args()
    if args.self_check:
        return self_check()
    if not args.a or not args.b:
        p.error("need two logs, or --self-check")
    report = args.report_dir
    if report is None:
        report = str(Path(args.b).resolve().parent)
    return compare(args.a, args.b, sys.stdout, report_dir=report)


if __name__ == "__main__":
    sys.exit(main())
