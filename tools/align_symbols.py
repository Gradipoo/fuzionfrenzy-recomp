#!/usr/bin/env python3
"""Neighbour-order alignment of XDK library names into out/ff.exe (XDK 3925).

Usage, from the repo root (no arguments):

    python3 tools/align_symbols.py

Writes config/found-align.toml (only names the image proves) and
docs/align-symbols.md (the method, every emitted name with its evidence, and a
ranked candidate list for every name it did not emit).

The full method and the reasons behind every threshold are in the report this
writes; the code comments only say what each part computes.

Standard library only.
"""

from __future__ import annotations

import json
import re
import struct
import sys
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ----------------------------------------------------------------------------- x86 decoder
#
# 32-bit x86, written to reproduce what sdk/tools/symbols/main.cpp reads through Zydis when it
# signs a function: instruction length, branch category and relative target, the raw displacement
# and immediates, and which general registers each instruction reads and writes, hidden operands
# included. Register bits follow the signer: eax ecx edx ebx esp ebp esi edi, and esp counts for
# nothing.

FLOW_NONE, FLOW_COND, FLOW_JMP, FLOW_CALL, FLOW_RET, FLOW_STOP = range(6)
REG_NAMES = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
GPR32 = (1, 2, 4, 8, 0, 32, 64, 128)
GPR8 = (1, 2, 4, 8, 1, 2, 4, 8)  # al cl dl bl ah ch dh bh -> enclosing register
HIDDEN = {"eax": 1, "ecx": 2, "edx": 4, "ebx": 8, "esp": 0, "ebp": 32, "esi": 64, "edi": 128}
FIXED = {"%al": 1, "%eax": 1, "%cl": 2, "%dx": 4}

# operand types read from the ModRM byte (the rest are immediates, branches, fixed registers)
MODRM_TYPES = ("E", "G", "M", "R", "S", "W", "V", "N", "C", "D", "P", "Q", "U")


def _spec(text: str):
    """'mnem type:action ... | hidden:action ...' -> (mnem, operands, hidden)."""
    head, _, tail = text.partition("|")
    words = head.split()
    ops = tuple(tuple(w.partition(":")[::2]) for w in words[1:])
    hidden = tuple(tuple(w.partition(":")[::2]) for w in tail.split())
    return (words[0], ops, hidden)


def _table(lines: str) -> dict:
    t = {}
    for line in lines.strip().splitlines():
        code, _, rest = line.strip().partition(" ")
        t[int(code, 16)] = _spec(rest)
    return t


ALU = ("add", "or", "adc", "sbb", "and", "sub", "xor", "cmp")
SHIFT = ("rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar")
JCC = "o no b nb z nz be nbe s ns p np l nl le nle".split()

ONE = _table("""
06 push %es | esp:rw mesp:w
07 pop %es | esp:rw mesp:r
0E push %cs | esp:rw mesp:w
16 push %ss | esp:rw mesp:w
17 pop %ss | esp:rw mesp:r
1E push %ds | esp:rw mesp:w
1F pop %ds | esp:rw mesp:r
27 daa | eax:rw
2F das | eax:rw
37 aaa | eax:rw
3F aas | eax:rw
60 pushad | eax:r ecx:r edx:r ebx:r esp:rw ebp:r esi:r edi:r mesp:w
61 popad | eax:w ecx:w edx:w ebx:w esp:rw ebp:w esi:w edi:w mesp:r
62 bound Gv:r Ma:r
63 arpl Ew:rw Gw:r
68 push sIz | esp:rw mesp:w
69 imul Gv:w Ev:r sIz
6A push sIb | esp:rw mesp:w
6B imul Gv:w Ev:r sIb
6C insb Yb:w %dx:r | edi:rw
6D insd Yv:w %dx:r | edi:rw
6E outsb %dx:r Xb:r | esi:rw
6F outsd %dx:r Xv:r | esi:rw
84 test Eb:r Gb:r
85 test Ev:r Gv:r
86 xchg Eb:rw Gb:rw
87 xchg Ev:rw Gv:rw
88 mov Eb:w Gb:r
89 mov Ev:w Gv:r
8A mov Gb:w Eb:r
8B mov Gv:w Ev:r
8C mov Ev:w Sw:r
8D lea Gv:w M:a
8E mov Sw:w Ew:r
90 nop
98 cwde | eax:rw
99 cdq | eax:r edx:w
9A callf Ap | esp:rw mesp:w
9B fwait
9C pushfd | esp:rw mesp:w
9D popfd | esp:rw mesp:r
9E sahf | eax:r
9F lahf | eax:w
A0 mov %al:w Ob:r
A1 mov %eax:w Ov:r
A2 mov Ob:w %al:r
A3 mov Ov:w %eax:r
A4 movsb Yb:w Xb:r | esi:rw edi:rw
A5 movsd Yv:w Xv:r | esi:rw edi:rw
A6 cmpsb Xb:r Yb:r | esi:rw edi:rw
A7 cmpsd Xv:r Yv:r | esi:rw edi:rw
A8 test %al:r Ib
A9 test %eax:r Iz
AA stosb Yb:w %al:r | edi:rw
AB stosd Yv:w %eax:r | edi:rw
AC lodsb %al:w Xb:r | esi:rw
AD lodsd %eax:w Xv:r | esi:rw
AE scasb %al:r Yb:r | edi:rw
AF scasd %eax:r Yv:r | edi:rw
C2 ret Iw | esp:rw mesp:r
C3 ret | esp:rw mesp:r
C4 les Gv:w Mp:r
C5 lds Gv:w Mp:r
C8 enter Iw Ib | ebp:rw esp:rw mesp:w
C9 leave | ebp:rw esp:rw
CA retf Iw | esp:rw mesp:r
CB retf | esp:rw mesp:r
CC int3
CD int Ib
CE into
CF iretd | esp:rw mesp:r
D4 aam Ib | eax:rw
D5 aad Ib | eax:rw
D6 salc | eax:w
D7 xlat | eax:rw ebx:r
E0 loopne Jb | ecx:rw
E1 loope Jb | ecx:rw
E2 loop Jb | ecx:rw
E3 jecxz Jb | ecx:r
E4 in %al:w Ib
E5 in %eax:w Ib
E6 out Ib %al:r
E7 out Ib %eax:r
E8 call Jz | esp:rw mesp:w
E9 jmp Jz
EA jmpf Ap
EB jmp Jb
EC in %al:w %dx:r
ED in %eax:w %dx:r
EE out %dx:r %al:r
EF out %dx:r %eax:r
F1 int1
F4 hlt
F5 cmc
F8 clc
F9 stc
FA cli
FB sti
FC cld
FD std
""")
for _i, _m in enumerate(ALU):
    _a = "r" if _m == "cmp" else "rw"
    ONE[_i * 8 + 0] = _spec(f"{_m} Eb:{_a} Gb:r")
    ONE[_i * 8 + 1] = _spec(f"{_m} Ev:{_a} Gv:r")
    ONE[_i * 8 + 2] = _spec(f"{_m} Gb:{_a} Eb:r")
    ONE[_i * 8 + 3] = _spec(f"{_m} Gv:{_a} Ev:r")
    ONE[_i * 8 + 4] = _spec(f"{_m} %al:{_a} Ib")
    ONE[_i * 8 + 5] = _spec(f"{_m} %eax:{_a} Iz")
for _r in range(8):
    ONE[0x40 + _r] = _spec("inc Zv:rw")
    ONE[0x48 + _r] = _spec("dec Zv:rw")
    ONE[0x50 + _r] = _spec("push Zv:r | esp:rw mesp:w")
    ONE[0x58 + _r] = _spec("pop Zv:w | esp:rw mesp:r")
    ONE[0xB0 + _r] = _spec("mov Zb:w Ib")
    ONE[0xB8 + _r] = _spec("mov Zv:w Iv")
    if _r:
        ONE[0x90 + _r] = _spec("xchg Zv:rw %eax:rw")
for _i, _c in enumerate(JCC):
    ONE[0x70 + _i] = _spec(f"j{_c} Jb")

# groups selected by ModRM.reg: opcode -> 8 specs (None = invalid)
GROUPS = {
    0x80: [_spec(f"{m} Eb:{'r' if m == 'cmp' else 'rw'} Ib") for m in ALU],
    0x81: [_spec(f"{m} Ev:{'r' if m == 'cmp' else 'rw'} Iz") for m in ALU],
    0x82: [_spec(f"{m} Eb:{'r' if m == 'cmp' else 'rw'} Ib") for m in ALU],
    0x83: [_spec(f"{m} Ev:{'r' if m == 'cmp' else 'rw'} sIb") for m in ALU],
    0x8F: [_spec("pop Ev:w | esp:rw mesp:r")] + [None] * 7,
    0xC0: [_spec(f"{m} Eb:rw Ib") for m in SHIFT],
    0xC1: [_spec(f"{m} Ev:rw Ib") for m in SHIFT],
    0xC6: [_spec("mov Eb:w Ib")] + [None] * 7,
    0xC7: [_spec("mov Ev:w Iz")] + [None] * 7,
    0xD0: [_spec(f"{m} Eb:rw") for m in SHIFT],
    0xD1: [_spec(f"{m} Ev:rw") for m in SHIFT],
    0xD2: [_spec(f"{m} Eb:rw %cl:r") for m in SHIFT],
    0xD3: [_spec(f"{m} Ev:rw %cl:r") for m in SHIFT],
    0xF6: [_spec(s) for s in (
        "test Eb:r Ib", "test Eb:r Ib", "not Eb:rw", "neg Eb:rw", "mul Eb:r | eax:rw",
        "imul Eb:r | eax:rw", "div Eb:r | eax:rw", "idiv Eb:r | eax:rw")],
    0xF7: [_spec(s) for s in (
        "test Ev:r Iz", "test Ev:r Iz", "not Ev:rw", "neg Ev:rw", "mul Ev:r | eax:rw edx:w",
        "imul Ev:r | eax:rw edx:w", "div Ev:r | eax:rw edx:rw", "idiv Ev:r | eax:rw edx:rw")],
    0xFE: [_spec("inc Eb:rw"), _spec("dec Eb:rw")] + [None] * 6,
    0xFF: [_spec(s) for s in (
        "inc Ev:rw", "dec Ev:rw", "call Ev:r | esp:rw mesp:w", "callf Mp:r | esp:rw mesp:w",
        "jmp Ev:r", "jmpf Mp:r", "push Ev:r | esp:rw mesp:w")] + [None],
}

TWO = _table("""
02 lar Gv:w Ew:r
03 lsl Gv:w Ew:r
05 syscall
06 clts
07 sysret
08 invd
09 wbinvd
0B ud2
0D nop Ev:r
0E femms
0F 3dnow Pq Qq Ib
18 prefetch Ev:r
20 mov Rd:w Cd
21 mov Rd:w Dd
22 mov Cd Rd:r
23 mov Dd Rd:r
30 wrmsr | ecx:r eax:r edx:r
31 rdtsc | eax:w edx:w
32 rdmsr | ecx:r eax:w edx:w
33 rdpmc | ecx:r eax:w edx:w
34 sysenter
35 sysexit | ecx:r edx:r
37 getsec | eax:rw ebx:r
50 movmskps Gd:w Ux
6E movd Px Ed:r
70 pshufw Px Qx Ib
77 emms
79 vmwrite Gd:r Ed:r
7F movq Qx Px
A0 push %fs | esp:rw mesp:w
A1 pop %fs | esp:rw mesp:r
A2 cpuid | eax:rw ecx:rw ebx:w edx:w
A3 bt Ev:r Gv:r
A4 shld Ev:rw Gv:r Ib
A5 shld Ev:rw Gv:r %cl:r
A8 push %gs | esp:rw mesp:w
A9 pop %gs | esp:rw mesp:r
AA rsm
AB bts Ev:rw Gv:r
AC shrd Ev:rw Gv:r Ib
AD shrd Ev:rw Gv:r %cl:r
AE grp15 Ex
AF imul Gv:rw Ev:r
B0 cmpxchg Eb:rw Gb:r | eax:rw
B1 cmpxchg Ev:rw Gv:r | eax:rw
B2 lss Gv:w Mp:r
B3 btr Ev:rw Gv:r
B4 lfs Gv:w Mp:r
B5 lgs Gv:w Mp:r
B6 movzx Gv:w Eb:r
B7 movzx Gv:w Ew:r
B9 ud1 Gv:r Ev:r
BB btc Ev:rw Gv:r
BC bsf Gv:w Ev:r
BD bsr Gv:w Ev:r
BE movsx Gv:w Eb:r
BF movsx Gv:w Ew:r
C0 xadd Eb:rw Gb:rw
C1 xadd Ev:rw Gv:rw
C2 cmpps Vx Wx Ib
C3 movnti M:w Gd:r
C4 pinsrw Px Ed:r Ib
C5 pextrw Gd:w Nx Ib
C6 shufps Vx Wx Ib
D7 pmovmskb Gd:w Nx
F7 maskmovq Px Nx | edi:r
FF ud0 Gv:r Ev:r
""")
for _r in range(0x10, 0x18):
    TWO[_r] = _spec("sse Vx Wx")
for _r in range(0x19, 0x20):
    TWO[_r] = _spec("nop Ev:r")
for _r in (0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F):
    TWO[_r] = _spec("sse Vx Wx")
for _r in range(0x40, 0x50):
    TWO[_r] = _spec(f"cmov{JCC[_r - 0x40]} Gv:rw Ev:r")
for _r in list(range(0x51, 0x70)) + [0x74, 0x75, 0x76, 0x7C, 0x7D] + list(range(0xD0, 0x100)):
    if _r not in TWO:
        TWO[_r] = _spec("sse Px Qx")
for _r in range(0x80, 0x90):
    TWO[_r] = _spec(f"j{JCC[_r - 0x80]} Jz")
for _r in range(0x90, 0xA0):
    TWO[_r] = _spec(f"set{JCC[_r - 0x90]} Eb:w")
for _r in range(0xC8, 0xD0):
    TWO[_r] = _spec("bswap Zd:rw")

GROUPS2 = {
    0x00: [_spec(s) for s in ("sldt Ew:w", "str Ew:w", "lldt Ew:r", "ltr Ew:r", "verr Ew:r",
                              "verw Ew:r")] + [None, None],
    0x01: [_spec("grp7 Ex")] * 8,
    0x71: [None, None, _spec("psrlw Nx Ib"), None, _spec("psraw Nx Ib"), None, _spec("psllw Nx Ib"), None],
    0x72: [None, None, _spec("psrld Nx Ib"), None, _spec("psrad Nx Ib"), None, _spec("pslld Nx Ib"), None],
    0x73: [None, None, _spec("psrlq Nx Ib"), _spec("psrldq Nx Ib"), None, None, _spec("psllq Nx Ib"),
           _spec("pslldq Nx Ib")],
    0xBA: [None] * 4 + [_spec("bt Ev:r Ib"), _spec("bts Ev:rw Ib"), _spec("btr Ev:rw Ib"),
                        _spec("btc Ev:rw Ib")],
    0xC7: [None, _spec("cmpxchg8b M:rw | eax:rw edx:rw ebx:r ecx:r")] + [None] * 4 +
          [_spec("rdrand Rd:w"), _spec("rdseed Rd:w")],
}
INVALID2 = {0x04, 0x0A, 0x0C, 0x24, 0x25, 0x26, 0x27, 0x36, 0x39, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
            0x7A, 0x7B, 0xA6, 0xA7}
# operands that must name memory: the register form is another instruction or none
MEMORY_ONLY = {"M", "Ma", "Mp"}
STRING_OPS = {"movsb", "movsd", "cmpsb", "cmpsd", "stosb", "stosd", "lodsb", "lodsd", "scasb",
              "scasd", "insb", "insd", "outsb", "outsd"}


class Insn:
    """One decoded instruction, as much of it as the signer reads."""

    __slots__ = ("length", "mnem", "flow", "rel", "rel_size", "target", "disp", "disp_size", "imms",
                 "rd", "wt", "stack", "noread")

    def __init__(self) -> None:
        self.flow = FLOW_NONE
        self.rel = False
        self.rel_size = 0
        self.target = 0
        self.disp = 0
        self.disp_size = 0
        self.imms: list[tuple[int, int]] = []  # (value as the signer reads it, size in bytes)
        self.rd = 0
        self.wt = 0
        self.stack = False
        self.noread = False


def _sx(v: int, bits: int) -> int:
    """Sign extension of a bits-wide value to 64 bits, kept as an unsigned Python int."""
    if v >> (bits - 1) & 1:
        v -= 1 << bits
    return v & 0xFFFFFFFFFFFFFFFF


def decode(b: bytes, o: int, end: int, va: int) -> Insn | None:
    """The instruction at b[o] (virtual address va), reading no byte at or past end."""
    end = min(end, o + 15)
    p = o
    opsize16 = addr16 = False
    rep = 0
    while True:
        if p >= end:
            return None
        c = b[p]
        if c == 0x66:
            opsize16 = True
        elif c == 0x67:
            addr16 = True
        elif c in (0xF2, 0xF3):
            rep = c
        elif c in (0xF0, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65):
            pass
        else:
            break
        p += 1
    op = b[p]
    p += 1
    two = False
    spec = None
    x87 = False
    group = None
    if op == 0x0F:
        if p >= end:
            return None
        op = b[p]
        p += 1
        two = True
        if op in (0x38, 0x3A):
            if p >= end:
                return None
            p += 1
            spec = _spec("sse3 Vx Wx Ib") if op == 0x3A else _spec("sse3 Vx Wx")
        elif op in INVALID2:
            return None
        elif op in GROUPS2:
            group = GROUPS2[op]
        elif op == 0x78:
            spec = _spec("extrq Vx Wx Ib Ib") if rep or opsize16 else _spec("vmread Ed:w Gd:r")
        elif op == 0x7E:
            spec = _spec("movq Vx Wx") if rep == 0xF3 else _spec("movd Ed:w Px")
        elif op == 0xB8:
            if rep != 0xF3:
                return None
            spec = _spec("popcnt Gv:w Ev:r")
        elif op == 0x2A and rep:
            spec = _spec("cvtsi2ss Vx Ed:r")
        elif op in (0x2C, 0x2D) and rep:
            spec = _spec("cvtss2si Gd:w Wx")
        else:
            spec = TWO.get(op)
            if spec is None:
                return None
    elif 0xD8 <= op <= 0xDF:
        x87 = True
    elif op in GROUPS:
        group = GROUPS[op]
    else:
        spec = ONE.get(op)
        if spec is None:
            return None

    # ModRM, SIB and displacement
    modrm = None
    mod = reg = rm = 0
    mem_regs = 0  # base and index bits of the memory operand
    mem_esp = False
    need_modrm = x87 or group is not None or (
        spec is not None and any(t[0][0] in MODRM_TYPES for t in spec[1]))
    ins = Insn()
    if need_modrm:
        if p >= end:
            return None
        modrm = b[p]
        p += 1
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if mod != 3:
            if addr16:
                mem_regs = (8 | 64, 8 | 128, 32 | 64, 32 | 128, 64, 128, 32, 8)[rm]
                if mod == 0 and rm == 6:
                    mem_regs = 0
                    dsz = 2
                else:
                    dsz = (0, 1, 2)[mod]
            else:
                dsz = (0, 1, 4)[mod]
                if rm == 4:
                    if p >= end:
                        return None
                    sib = b[p]
                    p += 1
                    base, index = sib & 7, (sib >> 3) & 7
                    if index != 4:
                        mem_regs |= GPR32[index]
                    if base == 5 and mod == 0:
                        dsz = 4
                    else:
                        mem_regs |= GPR32[base]
                        mem_esp = base == 4
                elif rm == 5 and mod == 0:
                    dsz = 4
                else:
                    mem_regs |= GPR32[rm]
            if dsz:
                if p + dsz > end:
                    return None
                raw = int.from_bytes(b[p:p + dsz], "little")
                ins.disp = _sx(raw, dsz * 8)
                ins.disp_size = dsz * 8
                p += dsz
        if group is not None:
            spec = group[reg]
            if spec is None:
                return None

    rd = wt = 0
    stack = False
    first_reg = None  # the first operand, when it is a register: (class, number, size)
    second_reg = None
    visible = 0

    if x87:
        if mod != 3:
            rd |= mem_regs
            stack |= mem_esp
            first_reg = None
        elif op == 0xDF and modrm == 0xE0:
            wt |= 1  # fnstsw ax
        ins.mnem = "x87"
        flow = FLOW_NONE
        visible = 1
    else:
        mnem, ops, hidden = spec
        ins.mnem = mnem
        wide = 2 if opsize16 else 4
        for idx, (typ, act) in enumerate(ops):
            kind = typ[0]
            regid = None
            if typ[0] == "%":
                bits = FIXED.get(typ, 0)
                regid = ("gpr", typ)
                if act:
                    if "r" in act:
                        rd |= bits
                    if act in ("w", "rw"):
                        wt |= bits
            elif kind in "EMW" or typ in ("Qq", "Qx", "Ma", "Mp"):
                if mod == 3:
                    if typ in MEMORY_ONLY:
                        return None
                    if kind == "E":
                        bits = GPR8[rm] if typ == "Eb" else GPR32[rm]
                        regid = ("gpr8" if typ == "Eb" else "gpr", rm)
                        if "r" in act:
                            rd |= bits
                        if act in ("w", "rw"):
                            wt |= bits
                        if regid == ("gpr", 4):
                            regid = ("esp",)
                    else:
                        regid = ("other", rm)
                else:
                    rd |= mem_regs
                    stack |= mem_esp
            elif kind == "G":
                bits = GPR8[reg] if typ == "Gb" else GPR32[reg]
                regid = ("gpr8" if typ == "Gb" else "gpr", reg)
                if "r" in act:
                    rd |= bits
                if act in ("w", "rw"):
                    wt |= bits
                if regid == ("gpr", 4):
                    regid = ("esp",)
            elif kind == "R":
                bits = GPR32[rm]
                regid = ("gpr", rm)
                if "r" in act:
                    rd |= bits
                if act in ("w", "rw"):
                    wt |= bits
            elif kind == "Z":
                r = op & 7
                bits = GPR8[r] if typ == "Zb" else GPR32[r]
                regid = ("gpr8" if typ == "Zb" else "gpr", r)
                if regid == ("gpr", 4):
                    regid = ("esp",)
                if "r" in act:
                    rd |= bits
                if act in ("w", "rw"):
                    wt |= bits
            elif kind in "SVNCDPU":
                regid = ("other", kind, reg if kind in "SVCDP" else rm)
            elif kind in "XY":
                if addr16:
                    rd |= 64 if kind == "X" else 128
                else:
                    rd |= 64 if kind == "X" else 128
            elif kind == "O":
                dsz = 2 if addr16 else 4
                if p + dsz > end:
                    return None
                ins.disp = _sx(int.from_bytes(b[p:p + dsz], "little"), dsz * 8)
                ins.disp_size = dsz * 8
                p += dsz
            elif kind == "I" or typ.startswith("sI"):
                signed = typ.startswith("s")
                t = typ[1:] if signed else typ
                size = {"Ib": 1, "Iw": 2, "Iz": wide, "Iv": wide}[t]
                if p + size > end:
                    return None
                raw = int.from_bytes(b[p:p + size], "little")
                p += size
                ins.imms.append((_sx(raw, size * 8) if signed else raw, size))
                regid = ("imm",)
            elif kind == "J":
                size = 1 if typ == "Jb" else wide
                if p + size > end:
                    return None
                raw = int.from_bytes(b[p:p + size], "little")
                p += size
                ins.rel = True
                ins.rel_size = size * 8
                ins.imms.append((_sx(raw, size * 8), size))
                regid = ("imm",)
            elif kind == "A":
                size = wide
                if p + size + 2 > end:
                    return None
                ins.imms.append((int.from_bytes(b[p:p + size], "little"), size))
                ins.imms.append((int.from_bytes(b[p + size:p + size + 2], "little"), 2))
                p += size + 2
                regid = ("imm",)
            elif typ == "1":
                regid = ("imm",)
            if idx == 0:
                first_reg = regid
            elif idx == 1:
                second_reg = regid
            visible += 1
        for name, act in hidden:
            if name == "mesp":
                stack = True
                continue
            bits = HIDDEN[name]
            if "r" in act:
                rd |= bits
            if act in ("w", "rw"):
                wt |= bits
        if mnem in STRING_OPS and rep:
            rd |= 2
            wt |= 2
        if first_reg == ("esp",):
            stack = True
        if mnem in ("ret", "retf", "iretd"):
            flow = FLOW_RET
        elif mnem in ("int3", "hlt"):
            flow = FLOW_STOP
        elif mnem in ("call", "callf"):
            flow = FLOW_CALL
        elif mnem in ("jmp", "jmpf"):
            flow = FLOW_JMP
        elif (mnem[0] == "j" and mnem not in ("jmp", "jmpf")) or mnem in ("loop", "loope", "loopne"):
            flow = FLOW_COND
        else:
            flow = FLOW_NONE
        same = (visible == 2 and first_reg is not None and first_reg == second_reg
                and first_reg[0] in ("gpr", "gpr8", "esp"))
        ins.noread = (same and mnem in ("xor", "sub", "sbb")) or (
            mnem == "push" and first_reg is not None and first_reg[0] != "imm")
    ins.length = p - o
    if ins.length > 15:
        return None
    ins.flow = flow
    ins.rd = rd
    ins.wt = wt
    ins.stack = stack
    if ins.rel:
        ins.target = (va + ins.length + ins.imms[0][0]) & 0xFFFFFFFF
    return ins
