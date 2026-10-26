#!/usr/bin/env python3
"""Compare original XBE instructions in Unicorn with the recompiler's math.

Unicorn (https://github.com/unicorn-engine/unicorn, GPL-2.0) is an external
dependency, installed with pip into re/.venv or provided by Homebrew. It is
not vendored. The XBE is read from this machine and nothing it contains is
written outside re/.

The oracle binary calls the same softmath the recompiler uses: openlibm, the
66-bit pi reduction for fsin/fcos/fptan, 24-bit significand rounding, and the
indefinite 0x80000000 float-to-int. Unicorn is QEMU. Its fsin/fcos reduction
is not that 66-bit pi, and |x| >= 2^63 does not set C2 the way a real x87 and
this recompiler do. Those mismatches print FAIL qemu-x87. RNG, fistp,
cvttss2si, and a 53-bit fmul are expected to match bit for bit.

  python3.14 -m venv re/.venv
  re/.venv/bin/pip install 'unicorn==2.1.4' capstone
  python3 scripts/unicorn_diff.py
  python3 scripts/unicorn_diff.py --seed 1 --count 16 assets/default.xbe
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENV_PY = ROOT / "re" / ".venv" / "bin" / "python"
ORACLE = ROOT / "re" / "oracle" / "fp_oracle"
RNG_VA = 0x285630
MUL = 0x343FD
ADD = 0x269EC3
SCRATCH = 0x70000000
CODE = SCRATCH
CW_ADDR = SCRATCH + 0x200
IN_ADDR = SCRATCH + 0x210
IN2_ADDR = SCRATCH + 0x218
OUT_ADDR = SCRATCH + 0x220
OUT2_ADDR = SCRATCH + 0x228
STACK = SCRATCH + 0xF000

TIME_IMPORTS = {
    "KeDelayExecutionThread",
    "KeQuerySystemTime",
    "KeSetTimer",
    "KeInitializeTimerEx",
    "RtlTimeToTimeFields",
    "KeStallExecutionProcessor",
    "KeCancelTimer",
    "KeTickCount",
    "RtlTimeFieldsToTime",
    "KeQueryPerformanceCounter",
    "KeQueryPerformanceFrequency",
    "KeQueryInterruptTime",
    "NtSetSystemTime",
    "KeSetTimerEx",
    "NtSetTimerEx",
    "KeSystemTime",
    "KeInterruptTime",
    "KeTimeIncrement",
}


def reexec_venv() -> None:
    if VENV_PY.is_file() and Path(sys.executable).resolve() != VENV_PY.resolve():
        os.execv(str(VENV_PY), [str(VENV_PY), str(Path(__file__).resolve()), *sys.argv[1:]])


def ensure_imports():
    reexec_venv()
    try:
        import unicorn  # noqa: F401
        import capstone  # noqa: F401
    except ImportError:
        py = "python3.14" if Path("/opt/homebrew/bin/python3.14").is_file() else sys.executable
        print("Unicorn's Python module is not installed. It stays out of the repo.", file=sys.stderr)
        print(f"  {py} -m venv re/.venv", file=sys.stderr)
        print("  re/.venv/bin/pip install 'unicorn==2.1.4' capstone", file=sys.stderr)
        print("  python3 scripts/unicorn_diff.py", file=sys.stderr)
        sys.exit(2)


def load_xbe(path: Path):
    sys.path.insert(0, str(ROOT / "tools"))
    import xbe_info

    xbe = xbe_info.Xbe(str(path))
    names = xbe_info.load_ordinal_names(str(ROOT / "sdk"))
    return xbe, names


def build_oracle() -> None:
    newest = max(
        (ROOT / "tools" / "re" / "fp_oracle.cpp").stat().st_mtime,
        (ROOT / "sdk" / "runtime" / "cpu" / "softmath.cpp").stat().st_mtime,
    )
    if ORACLE.is_file() and ORACLE.stat().st_mtime >= newest:
        return
    objs = ROOT / "re" / "oracle" / "objs"
    objs.mkdir(parents=True, exist_ok=True)
    cflags = [
        "-std=c99", "-O2", "-ffp-contract=off", "-fno-fast-math", "-fno-builtin",
        "-fno-strict-aliasing", "-fvisibility=hidden", "-DOLM_PRIVATE", "-D__BSD_VISIBLE",
        "-Wno-deprecated-non-prototype",
        "-I", str(ROOT / "third_party" / "openlibm" / "include"),
        "-I", str(ROOT / "third_party" / "openlibm" / "src"),
        "-include", str(ROOT / "third_party" / "openlibm" / "olm_prefix.h"),
        "-arch", "arm64",
    ]
    sources = (ROOT / "third_party" / "openlibm" / "sources.txt").read_text().split()
    objects = []
    for name in sources:
        obj = objs / (Path(name).stem + ".o")
        objects.append(str(obj))
        if obj.is_file() and obj.stat().st_mtime >= (ROOT / "third_party" / "openlibm" / "src" / name).stat().st_mtime:
            continue
        subprocess.check_call(
            ["clang", *cflags, "-c", str(ROOT / "third_party" / "openlibm" / "src" / name), "-o", str(obj)],
            cwd=ROOT,
        )
    subprocess.check_call(
        [
            "clang++", "-std=c++20", "-O2", "-ffp-contract=off", "-fno-fast-math",
            "-Isdk/runtime", "-arch", "arm64",
            "tools/re/fp_oracle.cpp", "sdk/runtime/cpu/softmath.cpp",
            *objects, "-o", str(ORACLE),
        ],
        cwd=ROOT,
    )


class Oracle:
    def __init__(self) -> None:
        self.proc = subprocess.Popen(
            [str(ORACLE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
        )

    def ask(self, line: str) -> str:
        assert self.proc.stdin and self.proc.stdout
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        out = self.proc.stdout.readline().strip()
        if not out:
            raise RuntimeError(f"oracle returned nothing for {line}")
        return out

    def close(self) -> None:
        if self.proc.stdin:
            self.proc.stdin.close()
        self.proc.wait(timeout=5)


def u64(d: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", d))[0]


def f64(bits: int) -> float:
    return struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]


def exec_sections(xbe):
    for sec in xbe.sections:
        if "executable" in sec["flag_names"] and sec["raw_size"]:
            yield sec


def section_bytes(xbe, sec) -> bytes:
    return xbe.data[sec["raw_addr"] : sec["raw_addr"] + sec["raw_size"]]


class Image:
    def __init__(self, xbe, names: dict[int, str]):
        from capstone import CS_ARCH_X86, CS_MODE_32, Cs

        self.xbe = xbe
        self.names = names
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.thunks = {int(imp["thunk_addr"], 16): names.get(imp["ordinal"], "?") for imp in xbe.imports}

    def read(self, va: int, n: int) -> bytes:
        for sec in self.xbe.sections:
            base = sec["virtual_addr"]
            if base <= va < base + sec["raw_size"]:
                off = sec["raw_addr"] + (va - base)
                return self.xbe.data[off : off + n]
        return b""

    def insn_at(self, va: int):
        raw = self.read(va, 15)
        if not raw:
            return None
        return next(self.md.disasm(raw, va), None)

    def on_boundary(self, va: int, mnemonic: str) -> bool:
        ins = self.insn_at(va)
        if ins is None or ins.mnemonic != mnemonic or ins.address != va:
            return False
        for back in range(1, 16):
            prev = self.insn_at(va - back)
            if prev is not None and prev.address == va - back and prev.size == back:
                return True
        return va < 16


def map_image(mu, xbe) -> None:
    from unicorn import UC_PROT_ALL

    base = xbe.base & ~0xFFF
    end = xbe.base + xbe.size_of_image
    size = (end - base + 0xFFF) & ~0xFFF
    mu.mem_map(base, size, UC_PROT_ALL)
    for sec in xbe.sections:
        raw = xbe.data[sec["raw_addr"] : sec["raw_addr"] + sec["raw_size"]]
        if raw:
            mu.mem_write(sec["virtual_addr"], raw)
    mu.mem_map(SCRATCH, 0x10000, UC_PROT_ALL)
    mu.reg_write(mu_reg("ESP"), STACK)


def mu_reg(name: str) -> int:
    from unicorn import x86_const

    return getattr(x86_const, "UC_X86_REG_" + name)


def new_uc(xbe):
    from unicorn import UC_ARCH_X86, UC_MODE_32, Uc

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    map_image(mu, xbe)
    return mu


def run(mu, start: int, end: int, count: int = 0) -> None:
    mu.emu_start(start, end, timeout=200000, count=count)


def fld(mu, value: float, cw: int) -> None:
    code = (
        bytes.fromhex("dbe3")
        + bytes.fromhex("d92d")
        + struct.pack("<I", CW_ADDR)
        + bytes.fromhex("dd05")
        + struct.pack("<I", IN_ADDR)
    )
    mu.mem_write(CODE, code)
    mu.mem_write(CW_ADDR, struct.pack("<H", cw))
    mu.mem_write(IN_ADDR, struct.pack("<d", value))
    run(mu, CODE, CODE + len(code))


def fld_pair(mu, a: float, b: float, cw: int) -> None:
    """ST0 = a, ST1 = b, after fninit and fldcw."""
    code = (
        bytes.fromhex("dbe3")
        + bytes.fromhex("d92d")
        + struct.pack("<I", CW_ADDR)
        + bytes.fromhex("dd05")
        + struct.pack("<I", IN2_ADDR)
        + bytes.fromhex("dd05")
        + struct.pack("<I", IN_ADDR)
    )
    mu.mem_write(CODE, code)
    mu.mem_write(CW_ADDR, struct.pack("<H", cw))
    mu.mem_write(IN_ADDR, struct.pack("<d", a))
    mu.mem_write(IN2_ADDR, struct.pack("<d", b))
    run(mu, CODE, CODE + len(code))


def fstp(mu, addr: int) -> int:
    code = bytes.fromhex("dd1d") + struct.pack("<I", addr)
    mu.mem_write(CODE, code)
    run(mu, CODE, CODE + len(code))
    return struct.unpack("<Q", bytes(mu.mem_read(addr, 8)))[0]


def find_op(image: Image, mnemonic: str, pred=None):
    hits = []
    for sec in exec_sections(image.xbe):
        blob = section_bytes(image.xbe, sec)
        va0 = sec["virtual_addr"]
        step = 1
        i = 0
        while i < len(blob) - 1:
            b0 = blob[i]
            if mnemonic in ("fsin", "fcos", "fptan", "fsincos") and b0 != 0xD9:
                i += 1
                continue
            if mnemonic == "fmul" and b0 not in (0xD8, 0xDC):
                i += 1
                continue
            if mnemonic == "fistp" and b0 != 0xDB:
                i += 1
                continue
            if mnemonic == "fldcw" and b0 != 0xD9:
                i += 1
                continue
            if mnemonic == "cvttss2si" and blob[i : i + 3] != b"\xf3\x0f\x2c":
                i += 1
                continue
            va = va0 + i
            ins = image.insn_at(va)
            if ins is None or ins.mnemonic != mnemonic or ins.address != va:
                i += 1
                continue
            if not image.on_boundary(va, mnemonic):
                i += ins.size
                continue
            if pred is None or pred(ins):
                hits.append(ins)
                if len(hits) >= 40:
                    return hits
            i += ins.size
    return hits


def lcg_clusters(image: Image):
    """Straight-line MSVC rand: load the global, imul 343FDh, add 269EC3h, store it back."""
    loads = (
        b"\xa1\x30\x56\x28\x00",
        b"\x8b\x05\x30\x56\x28\x00",
        b"\x8b\x0d\x30\x56\x28\x00",
        b"\x8b\x15\x30\x56\x28\x00",
        b"\x8b\x1d\x30\x56\x28\x00",
        b"\x8b\x35\x30\x56\x28\x00",
        b"\x8b\x3d\x30\x56\x28\x00",
    )
    found = []
    seen = set()
    for sec in exec_sections(image.xbe):
        blob = section_bytes(image.xbe, sec)
        va0 = sec["virtual_addr"]
        for pat in loads:
            start = 0
            while True:
                i = blob.find(pat, start)
                if i < 0:
                    break
                start = i + 1
                va = va0 + i
                if va in seen:
                    continue
                insns = []
                cur = va
                stop = False
                for _ in range(10):
                    ins = image.insn_at(cur)
                    if ins is None or ins.address != cur:
                        stop = True
                        break
                    if ins.mnemonic in ("call", "ret", "jmp", "retn") or ins.mnemonic.startswith("j"):
                        stop = True
                        break
                    insns.append(ins)
                    cur += ins.size
                if stop and not insns:
                    continue
                text = " ".join(f"{ins.mnemonic} {ins.op_str}" for ins in insns).lower()
                if "0x343fd" not in text or "0x269ec3" not in text:
                    continue
                if "dword ptr [0x285630]," not in text:
                    continue
                seen.add(va)
                found.append(insns)
    return found


def seed_stores(image: Image, cluster_stores: set[int]) -> list[int]:
    """Stores to the RNG global that are not the update inside an LCG cluster."""
    out = []
    for sec in exec_sections(image.xbe):
        blob = section_bytes(image.xbe, sec)
        va0 = sec["virtual_addr"]
        start = 0
        pat = b"\x30\x56\x28\x00"
        while True:
            i = blob.find(pat, start)
            if i < 0:
                break
            start = i + 1
            for back in range(1, 3):
                va = va0 + i - back
                ins = image.insn_at(va)
                if ins is None or ins.mnemonic != "mov":
                    continue
                if ins.address + ins.size < va0 + i:
                    continue
                op = ins.op_str.lower()
                if op.startswith("dword ptr [0x285630]"):
                    if ins.address not in cluster_stores:
                        out.append(ins.address)
                break
    return out


def write_static_map(image: Image, clusters, stores: list[int]) -> None:
    """Addresses only. The file is game-derived and stays under re/."""
    path = ROOT / "re" / "static-map.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# addresses and instruction text. no decompiled code."]
    calls: dict[str, list[int]] = {name: [] for name in TIME_IMPORTS}
    other: dict[str, list[int]] = {name: [] for name in TIME_IMPORTS}
    for sec in image.xbe.sections:
        blob = section_bytes(image.xbe, sec)
        va0 = sec["virtual_addr"]
        for i in range(len(blob) - 5):
            if blob[i] == 0xFF and blob[i + 1] == 0x15:
                addr = int.from_bytes(blob[i + 2 : i + 6], "little")
                name = image.thunks.get(addr)
                if name in calls:
                    calls[name].append(va0 + i)
            elif i + 6 <= len(blob):
                addr = int.from_bytes(blob[i : i + 4], "little")
                name = image.thunks.get(addr)
                if name in other and not (i >= 2 and blob[i - 2] == 0xFF and blob[i - 1] == 0x15):
                    other[name].append(va0 + i)
    lines.append("## time imports")
    for name in sorted(TIME_IMPORTS):
        present = [hex(t) for t, n in image.thunks.items() if n == name]
        lines.append(
            f"IMPORT {name} thunk={present[0] if present else 'absent'} "
            f"calls={len(calls[name])} other_refs={len(other[name])}"
        )
        for va in calls[name][:40]:
            ins = image.insn_at(va)
            text = f"{ins.mnemonic} {ins.op_str}" if ins else "-"
            lines.append(f"  CALL {va:#x} {text}")
        if len(calls[name]) > 40:
            lines.append(f"  CALL ... {len(calls[name]) - 40} more")
    rdtsc = []
    for sec in exec_sections(image.xbe):
        blob = section_bytes(image.xbe, sec)
        va0 = sec["virtual_addr"]
        start = 0
        while True:
            i = blob.find(b"\x0f\x31", start)
            if i < 0:
                break
            start = i + 1
            va = va0 + i
            ins = image.insn_at(va)
            if ins and ins.mnemonic == "rdtsc" and image.on_boundary(va, "rdtsc"):
                rdtsc.append(va)
    lines.append(f"## rdtsc count={len(rdtsc)}")
    for va in rdtsc:
        lines.append(f"RDTSC {va:#x}")
    lines.append(f"## lcg clusters {len(clusters)}")
    for insns in clusters[:80]:
        lines.append(f"LCG {insns[0].address:#x} .. {insns[-1].address:#x} {insns[-1].mnemonic}")
    if len(clusters) > 80:
        lines.append(f"LCG ... {len(clusters) - 80} more")
    lines.append(f"## rng seed stores {len(stores)}")
    for va in stores[:80]:
        ins = image.insn_at(va)
        text = f"{ins.mnemonic} {ins.op_str}" if ins else "-"
        lines.append(f"SEED {va:#x} {text}")
    if len(stores) > 80:
        lines.append(f"SEED ... {len(stores) - 80} more")
    path.write_text("\n".join(lines) + "\n")


def reg_by_name(op: str) -> int | None:
    name = op.strip().lower()
    table = {
        "eax": "EAX", "ecx": "ECX", "edx": "EDX", "ebx": "EBX",
        "esp": "ESP", "ebp": "EBP", "esi": "ESI", "edi": "EDI",
    }
    if name not in table:
        return None
    return mu_reg(table[name])


def test_rng(mu, image: Image, oracle: Oracle, clusters, rng: random.Random, count: int) -> tuple[str, int]:
    seeds = [0, 1, 0x7FFF, 0x80000000, 0xFFFFFFFF, 0x12345678]
    while len(seeds) < count:
        seeds.append(rng.randrange(1 << 32))
    shapes: dict[tuple, list] = {}
    for insns in clusters:
        quartet = lcg_quartet(insns)
        if quartet is None:
            continue
        key = tuple(ins.op_str.lower() for ins in quartet)
        shapes.setdefault(key, []).append(quartet)
    tested = 0
    failed = 0
    used = []
    for quartets in list(shapes.values())[:8]:
        ok_site = None
        for quartet in quartets[:4]:
            code = b"".join(ins.bytes for ins in quartet)
            mu.mem_write(CODE, code)
            end = CODE + len(code)
            site_ok = True
            for seed in seeds:
                mu.mem_write(RNG_VA, struct.pack("<I", seed))
                try:
                    run(mu, CODE, end, count=len(quartet))
                except Exception:
                    site_ok = False
                    break
                got_state = struct.unpack("<I", bytes(mu.mem_read(RNG_VA, 4)))[0]
                state_s, _low_s = oracle.ask(f"rng {seed:08x}").split()
                if got_state != int(state_s, 16):
                    site_ok = False
                    break
            if site_ok:
                ok_site = quartet
                break
        tested += len(seeds)
        if ok_site is None:
            failed += len(seeds)
        else:
            used.append(ok_site[0].address)
    if not shapes:
        return "rng     FAIL  no LCG quartet found", 1
    status = "PASS" if failed == 0 else "FAIL"
    addrs = " ".join(f"{va:#x}" for va in used[:6])
    return (
        f"rng     {status}  shapes={min(8, len(shapes))} runs={tested} fail={failed} sites={addrs}",
        failed,
    )


def parse_mem_op(op: str):
    """Return (base_reg_name or None, disp) for `[reg]`, `[reg + n]`, `[reg - n]`."""
    op = op.lower().replace("dword ptr", "").replace("word ptr", "").strip()
    if not (op.startswith("[") and op.endswith("]")):
        return None
    body = op[1:-1].replace(" ", "")
    try:
        if "+" in body:
            reg, disp = body.split("+", 1)
            return reg, int(disp, 0)
        if "-" in body:
            reg, disp = body.split("-", 1)
            return reg, -int(disp, 0)
    except ValueError:
        return None
    return body, 0


def lcg_quartet(insns):
    """The load, imul, add, and store of one MSVC step, in that order.

    Surrounding instructions stay out of the run. They are the game using the
    result, and they read registers and stack slots this test does not set.
    """
    load = imul = add = store = None
    for ins in insns:
        op = ins.op_str.lower()
        if load is None and ins.mnemonic == "mov" and "[0x285630]" in op and "dword ptr [0x285630]," not in op:
            load = ins
        elif load is not None and imul is None and ins.mnemonic == "imul" and "0x343fd" in op:
            imul = ins
        elif imul is not None and add is None and ins.mnemonic == "add" and "0x269ec3" in op:
            add = ins
        elif add is not None and ins.mnemonic == "mov" and op.startswith("dword ptr [0x285630]"):
            store = ins
            break
    if not all((load, imul, add, store)):
        return None
    regs = []
    for ins in (load, imul, add):
        dest = ins.op_str.split(",", 1)[0].strip().lower()
        if dest not in ("eax", "ecx", "edx", "ebx", "esi", "edi"):
            return None
        regs.append(dest)
    if len(set(regs)) != 1:
        return None
    if not store.op_str.lower().endswith(regs[0]):
        return None
    return [load, imul, add, store]


def test_fistp(mu, image: Image, oracle: Oracle, values: list[float]) -> tuple[str, int]:
    hits = find_op(image, "fistp", lambda ins: parse_mem_op(ins.op_str) is not None)
    # op_str is like `dword ptr [esi]`
    chosen = None
    parsed = None
    for ins in hits:
        parsed = parse_mem_op(ins.op_str)
        if parsed and parsed[0] not in ("esp", "ebp") and reg_by_name(parsed[0]) is not None:
            chosen = ins
            break
    if chosen is None:
        return "fistp   FAIL  no original fistp with a register address", 1
    base, disp = parsed
    fail = 0
    for value in values:
        fld(mu, value, 0x027F)
        mu.reg_write(reg_by_name(base), (OUT_ADDR - disp) & 0xFFFFFFFF)
        try:
            run(mu, chosen.address, chosen.address + chosen.size, count=1)
        except Exception:
            fail += 1
            continue
        got = struct.unpack("<I", bytes(mu.mem_read(OUT_ADDR, 4)))[0]
        expect = int(oracle.ask(f"fistp {u64(value):016x}"), 16)
        if got != expect:
            fail += 1
    status = "PASS" if fail == 0 else "FAIL"
    return f"fistp   {status}  va={chosen.address:#x} n={len(values)} fail={fail}", fail


def test_cvtt(mu, image: Image, oracle: Oracle, values: list[float]) -> tuple[str, int]:
    hits = find_op(image, "cvttss2si")
    if not hits:
        return "cvtt    FAIL  no cvttss2si in the image", 1
    ins = hits[0]
    dest = ins.op_str.split(",", 1)[0].strip()
    mem = ins.op_str.split(",", 1)[1]
    parsed = parse_mem_op(mem)
    if parsed is None or reg_by_name(dest) is None:
        return f"cvtt    FAIL  unhandled form at {ins.address:#x}: {ins.op_str}", 1
    base, disp = parsed
    fail = 0
    for value in values:
        # The instruction reads a float32. The oracle sees that float widened to double.
        f = struct.unpack("<f", struct.pack("<f", value))[0]
        mu.mem_write(IN_ADDR, struct.pack("<f", f))
        mu.reg_write(reg_by_name(base), (IN_ADDR - disp) & 0xFFFFFFFF)
        try:
            run(mu, ins.address, ins.address + ins.size, count=1)
        except Exception:
            fail += 1
            continue
        got = mu.reg_read(reg_by_name(dest)) & 0xFFFFFFFF
        expect = int(oracle.ask(f"cvtt {u64(float(f)):016x}"), 16)
        if got != expect:
            fail += 1
    status = "PASS" if fail == 0 else "FAIL"
    return f"cvtt    {status}  va={ins.address:#x} n={len(values)} fail={fail}", fail


def test_fldcw(mu, image: Image) -> tuple[str, int]:
    hits = find_op(image, "fldcw", lambda ins: parse_mem_op(ins.op_str) is not None and parse_mem_op(ins.op_str)[0] not in ("esp", "ebp"))
    chosen = None
    parsed = None
    for ins in hits:
        parsed = parse_mem_op(ins.op_str)
        if parsed and reg_by_name(parsed[0]) is not None:
            chosen = ins
            break
    if chosen is None:
        return "fldcw   FAIL  no original fldcw through a register", 1
    base, disp = parsed
    fail = 0
    for cw in (0x027F, 0x007F):
        mu.mem_write(CW_ADDR, struct.pack("<H", cw))
        mu.reg_write(reg_by_name(base), (CW_ADDR - disp) & 0xFFFFFFFF)
        try:
            run(mu, chosen.address, chosen.address + chosen.size, count=1)
        except Exception:
            fail += 1
            continue
        got = mu.reg_read(mu_reg("FPCW")) & 0xFFFF
        if got != cw:
            fail += 1
    status = "PASS" if fail == 0 else "FAIL"
    return f"fldcw   {status}  va={chosen.address:#x} words=0x027f,0x007f fail={fail}", fail


def exact_round24(a: float, b: float) -> int | None:
    """24-bit significand of the exact product. None when the value is not a normal double."""
    import decimal

    prod = decimal.Decimal(a) * decimal.Decimal(b)
    if not prod.is_finite() or prod == 0:
        return u64(float(prod)) if prod == 0 or not prod.is_finite() else None
    sign = -1 if prod.is_signed() else 1
    mag = abs(prod)
    two = decimal.Decimal(2)
    # exponent of the highest set bit
    e = int(mag.ln() / two.ln())
    while two ** e > mag:
        e -= 1
    while two ** (e + 1) <= mag:
        e += 1
    scale = two ** (e - 23)
    mant = (mag / scale).to_integral_value(rounding=decimal.ROUND_HALF_EVEN)
    if mant == 1 << 24:
        mant = decimal.Decimal(1 << 23)
        e += 1
    if e > 1023 or e < -1022:
        return None
    val = decimal.Decimal(int(mant)) * scale * sign
    return u64(float(val))


def test_fmul(mu, image: Image, oracle: Oracle, pairs: list[tuple[float, float]]) -> tuple[str, int, int]:
    def good(ins) -> bool:
        op = ins.op_str.lower().replace(" ", "")
        # D8 C9 is FMUL ST(0), ST(1). Capstone 5 prints that as "st(1)".
        # "st(1), st(0)" would be the DC form and writes ST1.
        return op in ("st(1)", "st(0),st(1)", "st,st(1)")

    hits = find_op(image, "fmul", good)
    if not hits:
        return "round24 FAIL  no original `fmul st(0), st(1)`", 1, 0
    ins = hits[0]
    fail53 = 0
    fail24 = 0
    double_round = 0
    for a, b in pairs:
        try:
            fld_pair(mu, a, b, 0x027F)
            run(mu, ins.address, ins.address + ins.size, count=1)
            got = fstp(mu, OUT_ADDR)
        except Exception:
            fail53 += 1
            fail24 += 1
            continue
        expect = u64(a * b)
        if got != expect:
            fail53 += 1
        try:
            # Load at 53-bit so the inputs stay binary64, then drop the
            # control word to 24-bit. The multiply is what rounds.
            fld_pair(mu, a, b, 0x027F)
            cw = bytes.fromhex("d92d") + struct.pack("<I", CW_ADDR)
            mu.mem_write(CODE, cw)
            mu.mem_write(CW_ADDR, struct.pack("<H", 0x007F))
            run(mu, CODE, CODE + len(cw))
            run(mu, ins.address, ins.address + ins.size, count=1)
            got24 = fstp(mu, OUT_ADDR)
        except Exception:
            fail24 += 1
            continue
        model = int(oracle.ask(f"round24 {u64(a * b):016x}"), 16)
        if got24 != model:
            exact = exact_round24(a, b)
            if exact is not None and got24 == exact:
                double_round += 1
            else:
                fail24 += 1
    if fail53 or fail24:
        status = "FAIL"
    elif double_round:
        status = "FAIL double-round"
    else:
        status = "PASS"
    line = (
        f"round24 {status}  va={ins.address:#x} n={len(pairs)} "
        f"fmul53_fail={fail53} round24_fail={fail24} double_round={double_round}"
    )
    return line, fail53 + fail24, double_round


def test_trig(mu, image: Image, oracle: Oracle, mnemonic: str, values: list[float]) -> tuple[str, int, int]:
    hits = find_op(image, mnemonic)
    if not hits:
        return f"{mnemonic:<7} FAIL  no original {mnemonic}", 1, 0
    ins = hits[0]
    cmd = {"fsin": "x87sin", "fcos": "x87cos", "fptan": "x87tan", "fsincos": "sincos"}[mnemonic]
    match = 0
    differ = 0
    c2_fail = 0
    sample = ""
    for value in values:
        try:
            fld(mu, value, 0x027F)
            run(mu, ins.address, ins.address + ins.size, count=1)
        except Exception:
            differ += 1
            if not sample:
                sample = f" example {u64(value):016x} faulted"
            continue
        fpsw = mu.reg_read(mu_reg("FPSW")) & 0xFFFF
        st0 = fstp(mu, OUT_ADDR)
        st1 = None
        if mnemonic in ("fptan", "fsincos"):
            st1 = fstp(mu, OUT2_ADDR)
        huge = math.isfinite(value) and abs(value) >= 2.0**63
        if huge:
            # Recompiler: C2 set, ST0 left alone. QEMU's fsin usually computes anyway.
            unchanged = st0 == u64(value)
            if mnemonic == "fsin" and (fpsw & 0x400) and unchanged:
                match += 1
            else:
                c2_fail += 1
                differ += 1
            continue
        if mnemonic == "fsincos":
            expect = oracle.ask(f"{cmd} {u64(value):016x}")
            es, ec = expect.split()
            # Hardware and the recompiler leave cosine in ST0 and sine in ST1.
            ok = st0 == int(ec, 16) and st1 == int(es, 16)
        elif mnemonic == "fptan":
            expect = int(oracle.ask(f"{cmd} {u64(value):016x}"), 16)
            ok = st0 == u64(1.0) and st1 == expect
            if not ok and st0 != u64(1.0):
                # Stack order is part of the instruction, not the pi reduction.
                differ += 1
                if not sample:
                    sample = f" example {u64(value):016x} -> st0 {st0:016x} st1 {st1:016x} model-tan {expect:016x}"
                continue
        else:
            expect = int(oracle.ask(f"{cmd} {u64(value):016x}"), 16)
            ok = st0 == expect
        if ok:
            match += 1
        else:
            differ += 1
            if not sample:
                if st1 is None:
                    shown = f"{expect:016x}"
                    sample = f" example {u64(value):016x} -> {st0:016x} model {shown}"
                else:
                    shown = expect if isinstance(expect, str) else f"{expect:016x}"
                    sample = f" example {u64(value):016x} -> st0 {st0:016x} st1 {st1:016x} model {shown}"
    if differ == 0 and c2_fail == 0:
        status = "PASS"
    else:
        status = "FAIL qemu-x87"
    line = f"{mnemonic:<7} {status}  va={ins.address:#x} match={match} differ={differ} c2_miss={c2_fail}{sample}"
    # Trig mismatches are the QEMU limit, not an unexpected harness failure.
    return line, 0, differ + c2_fail


def trig_values(rng: random.Random, count: int) -> list[float]:
    base = [0.0, 0.5, 1.0, -0.5, math.pi / 6, math.pi / 4, math.pi / 2, math.pi, 3 * math.pi, 100.0, 1.0e6, -(math.pi)]
    while len(base) < count:
        base.append(rng.uniform(-math.pi, math.pi))
    base.append(2.0**63)
    base.append(-(2.0**63))
    return base


def fmul_pairs(rng: random.Random, count: int) -> list[tuple[float, float]]:
    pairs = [
        (1.0000001, 3.0000001),
        (1.0000000000000002, 1.0000000000000002),
        (2.5, 4.0),
        (-1.5, 2.25),
        (1e300, 1.0000000000001),
        (1.0000001192092896, 1.0000001192092896),
    ]
    while len(pairs) < count:
        pairs.append((rng.uniform(-100, 100), rng.uniform(-100, 100)))
    return pairs


def main() -> int:
    ensure_imports()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", nargs="?", default=str(ROOT / "assets" / "default.xbe"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=12)
    args = parser.parse_args()
    path = Path(args.xbe)
    if not path.is_file():
        print(f"missing {path}", file=sys.stderr)
        print("The XBE stays on this machine. Pass the path to your local default.xbe.", file=sys.stderr)
        return 2
    build_oracle()
    xbe, names = load_xbe(path)
    image = Image(xbe, names)
    clusters = lcg_clusters(image)
    store_vas = set()
    for insns in clusters:
        for ins in insns:
            if ins.op_str.lower().startswith("dword ptr [0x285630]"):
                store_vas.add(ins.address)
    stores = seed_stores(image, store_vas)
    write_static_map(image, clusters, stores)
    import unicorn

    print(f"unicorn {unicorn.__version__}  oracle softmath (openlibm + 66-bit pi)")
    print(f"xbe {path}  image {xbe.base:#x}+{xbe.size_of_image:#x}")
    mu = new_uc(xbe)
    oracle = Oracle()
    rng = random.Random(args.seed)
    unexpected = 0
    qemu = 0
    try:
        line, fail = test_rng(mu, image, oracle, clusters, rng, args.count)
        print(line)
        unexpected += 1 if fail else 0
        line, fail = test_fistp(
            mu, image, oracle,
            [0.0, 1.5, 2.5, -1.5, 0.5, -0.5, 8388608.5, -2147483648.5, 1e20, math.nan, math.inf],
        )
        print(line)
        unexpected += fail
        line, fail = test_cvtt(
            mu, image, oracle,
            [0.0, 1.9, -2.9, 1.5, -1.9, 8388607.0, 1e20, math.nan, math.inf],
        )
        print(line)
        unexpected += fail
        line, fail = test_fldcw(mu, image)
        print(line)
        unexpected += fail
        line, fail, _double = test_fmul(mu, image, oracle, fmul_pairs(rng, args.count))
        print(line)
        unexpected += fail
        for mnemonic in ("fsin", "fcos", "fptan", "fsincos"):
            line, fail, q = test_trig(mu, image, oracle, mnemonic, trig_values(rng, args.count))
            print(line)
            unexpected += fail
            qemu += q
    finally:
        oracle.close()
    print(
        f"sites   lcg_clusters={len(clusters)} "
        f"global_stores_outside_window={len(stores)}  (addresses in re/static-map.txt)"
    )
    print(f"SUMMARY unexpected={unexpected} qemu-x87={qemu}")
    print("QEMU fsin/fcos is not the 66-bit pi reduction. |x|>=2^63 does not set C2 here.")
    print("A 24-bit fmul that matches the exact product but not xbe_round24 is double rounding:")
    print("the recompiler rounds the binary64 product; x87 rounds an 80-bit product to 24 bits.")
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
