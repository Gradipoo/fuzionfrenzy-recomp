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

import bisect
import json
import math
import re
import struct
import sys
import tomllib
from collections import Counter
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
A8 test %al:r sIb
A9 test %eax:r sIz
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
    ONE[_i * 8 + 4] = _spec(f"{_m} %al:{_a} sIb")
    ONE[_i * 8 + 5] = _spec(f"{_m} %eax:{_a} sIz")
for _r in range(8):
    ONE[0x40 + _r] = _spec("inc Zv:rw")
    ONE[0x48 + _r] = _spec("dec Zv:rw")
    ONE[0x50 + _r] = _spec("push Zv:r | esp:rw mesp:w")
    ONE[0x58 + _r] = _spec("pop Zv:w | esp:rw mesp:r")
    ONE[0xB0 + _r] = _spec("mov Zb:w sIb")
    ONE[0xB8 + _r] = _spec("mov Zv:w sIv")
    if _r:
        ONE[0x90 + _r] = _spec("xchg Zv:rw %eax:rw")
for _i, _c in enumerate(JCC):
    ONE[0x70 + _i] = _spec(f"j{_c} Jb")

# groups selected by ModRM.reg: opcode -> 8 specs (None = invalid)
GROUPS = {
    # general-purpose immediates are signed (sign-extended) in the signer's decoder, shift counts,
    # port numbers, ret/enter sizes and SIMD selectors are not
    0x80: [_spec(f"{m} Eb:{'r' if m == 'cmp' else 'rw'} sIb") for m in ALU],
    0x81: [_spec(f"{m} Ev:{'r' if m == 'cmp' else 'rw'} sIz") for m in ALU],
    0x82: [_spec(f"{m} Eb:{'r' if m == 'cmp' else 'rw'} sIb") for m in ALU],
    0x83: [_spec(f"{m} Ev:{'r' if m == 'cmp' else 'rw'} sIb") for m in ALU],
    0x8F: [_spec("pop Ev:w | esp:rw mesp:r")] + [None] * 7,
    0xC0: [_spec(f"{m} Eb:rw Ib") for m in SHIFT],
    0xC1: [_spec(f"{m} Ev:rw Ib") for m in SHIFT],
    0xC6: [_spec("mov Eb:w sIb")] + [None] * 7,
    0xC7: [_spec("mov Ev:w sIz")] + [None] * 7,
    0xD0: [_spec(f"{m} Eb:rw") for m in SHIFT],
    0xD1: [_spec(f"{m} Ev:rw") for m in SHIFT],
    0xD2: [_spec(f"{m} Eb:rw %cl:r") for m in SHIFT],
    0xD3: [_spec(f"{m} Ev:rw %cl:r") for m in SHIFT],
    0xF6: [_spec(s) for s in (
        "test Eb:r sIb", "test Eb:r sIb", "not Eb:rw", "neg Eb:rw", "mul Eb:r | eax:rw",
        "imul Eb:r | eax:rw", "div Eb:r | eax:rw", "idiv Eb:r | eax:rw")],
    0xF7: [_spec(s) for s in (
        "test Ev:r sIz", "test Ev:r sIz", "not Ev:rw", "neg Ev:rw", "mul Ev:r | eax:rw edx:w",
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
                rd |= 64 if kind == "X" else 128  # ds:[esi] and es:[edi], either address size
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


# ----------------------------------------------------------------------------- images

ADDR_LO, ADDR_HI = 0x10000, 0x4000000  # values the signer takes as image addresses
K_SMALL, K_NEGATIVE = 0x40, 0xFFFFFF00  # constants below / from these are frame and counter noise


def u16(d: bytes, o: int) -> int:
    return struct.unpack_from("<H", d, o)[0]


def u32(d: bytes, o: int) -> int:
    return struct.unpack_from("<I", d, o)[0]


@dataclass
class Section:
    name: str
    va: int
    vsize: int
    rsize: int

    @property
    def code_end(self) -> int:
        """End of the bytes the signer sweeps: min(virtual, raw) size."""
        return self.va + min(self.vsize, self.rsize)


def xbe_sections(path: Path) -> tuple[int, list[Section]]:
    """Base address and section table of an XBE."""
    d = path.read_bytes()
    if d[:4] != b"XBEH":
        raise SystemExit(f"{path}: not an XBE")
    base = u32(d, 0x104)
    out = []
    for i in range(u32(d, 0x11C)):
        o = u32(d, 0x120) - base + i * 56
        _, va, vsize, _, rsize = struct.unpack_from("<IIIII", d, o)
        name_at = u32(d, o + 20) - base
        out.append(Section(d[name_at:d.index(b"\0", name_at)].decode(), va, vsize, rsize))
    return base, out


def pe_base_and_exports(data: bytes) -> tuple[int, dict[int, str]]:
    """Image base and {address: name} of a flat PE written by sdk/tools/symbols/xbe2pe.py."""
    pe = u32(data, 0x3C)
    opt = pe + 24
    base = u32(data, opt + 28)
    erva = u32(data, opt + 96)
    # xbe2pe lays every section out at file offset = rva
    nfun, nnames = u32(data, erva + 20), u32(data, erva + 24)
    funcs, names, ords = u32(data, erva + 28), u32(data, erva + 32), u32(data, erva + 36)
    out = {}
    for i in range(nnames):
        name_at = u32(data, names + 4 * i)
        name = data[name_at:data.index(b"\0", name_at)].decode()
        ordinal = u16(data, ords + 2 * i)
        if ordinal < nfun:
            out[base + u32(data, funcs + 4 * ordinal)] = name
    return base, out


class Image:
    """A flat guest image (offset = address - base) with the sections the signer decodes."""

    def __init__(self, data: bytes, base: int, sections: list[Section], code: list[str]) -> None:
        self.data = data
        self.base = base
        self.sections = sections
        self.code = [s for s in sections if s.name in code]
        self.cache: dict[int, Insn | None] = {}

    def code_at(self, va: int) -> Section | None:
        for s in self.code:
            if s.va <= va < s.code_end:
                return s
        return None

    def section_name(self, va: int) -> str | None:
        for s in self.sections:
            if s.va <= va < s.va + s.vsize:
                return s.name
        return None

    def decode(self, va: int) -> Insn | None:
        if va in self.cache:
            return self.cache[va]
        s = self.code_at(va)
        ins = decode(self.data, va - self.base, s.code_end - self.base, va) if s else None
        self.cache[va] = ins
        return ins

    def sweep_calls(self) -> list[int]:
        """rel32 call targets of a linear sweep of the code sections, as the signer's Sweep finds them."""
        calls: list[int] = []
        starts: dict[str, bytearray] = {}
        d, base = self.data, self.base

        def step(s: Section, st: bytearray, o: int) -> int:
            st[o] = 1
            va = s.va + o
            # zero fill between library data and code would swallow the next function's first bytes
            ins = decode(d, va - base, s.code_end - base, va) if d[va - base] else None
            if ins is None:
                return 1
            for i in range(1, ins.length):
                st[o + i] = 0
            if ins.flow == FLOW_CALL and ins.rel and ins.rel_size == 32:
                calls.append(ins.target)
            return ins.length

        for s in self.code:
            st = starts[s.name] = bytearray(s.code_end - s.va)
            o = 0
            while o < len(st):
                o += step(s, st, o)
        # restart at call targets the sweep ran past, until it falls back in step
        for t in list(calls):
            s = self.code_at(t)
            if s is None or starts[s.name][t - s.va]:
                continue
            st = starts[s.name]
            o = t - s.va
            o += step(s, st, o)
            while o < len(st) and not st[o]:
                o += step(s, st, o)
        return sorted(set(calls))


# ----------------------------------------------------------------------------- function shape

@dataclass
class Shape:
    """What the signer records for a function, computed the signer's way."""

    va: int
    len: int
    ret: tuple[int, ...]
    regs: int
    calls: int
    k: tuple[int, ...]
    targets: tuple[int, ...] = ()


def extent(img: Image, va: int, bound: int) -> int:
    """Reachable code from va, branches followed while below bound (the signer's Extent)."""
    work = [va]
    seen: set[int] = set()
    end = va
    while work:
        p = work.pop()
        while va <= p < bound and p not in seen:
            seen.add(p)
            ins = img.decode(p)
            if ins is None:
                break
            q = p + ins.length
            end = max(end, q)
            if ins.flow in (FLOW_RET, FLOW_STOP):
                break
            if ins.flow in (FLOW_JMP, FLOW_COND) and ins.rel:
                work.append(ins.target)
            if ins.flow == FLOW_JMP:
                break
            p = q
    return end - va


def trace(img: Image, va: int, length: int) -> tuple[tuple[int, ...], tuple[int, ...], int]:
    """Constants, ret sizes and entry registers of the code reachable in [va, va + length)."""
    home = img.code_at(va)
    home_name = home.name if home else None
    k: list[int] = []
    rets: set[int] = set()
    regs = 0
    seen: dict[int, int] = {}  # registers written on every path to an instruction
    work = [(va, 0)]
    while work:
        p, wr = work.pop()
        while va <= p < va + length:
            prev = seen.get(p)
            first = prev is None
            # every register written on all earlier paths here is written on this one too
            if not first and not (prev & ~wr):
                break
            if not first:
                wr &= prev
            seen[p] = wr
            ins = img.decode(p)
            if ins is None:
                break
            regs |= (0 if ins.noread else ins.rd) & ~wr
            wr |= ins.wt
            if ins.flow == FLOW_CALL:
                wr |= 7  # eax ecx edx
            if first and not ins.stack and not ins.rel:
                values = ([ins.disp] if ins.disp_size else []) + [v for v, _ in ins.imms]
                for v in values:
                    v &= 0xFFFFFFFF
                    is_addr = ADDR_LO <= v < ADDR_HI
                    text_in_library = is_addr and home_name != ".text" and img.section_name(v) == ".text"
                    if K_SMALL <= v < K_NEGATIVE and (not is_addr or text_in_library):
                        k.append(v)
            if ins.flow == FLOW_RET:
                rets.add(ins.imms[0][0] if ins.imms else 0)
            if ins.flow in (FLOW_RET, FLOW_STOP):
                break
            if ins.flow == FLOW_COND and ins.rel:
                work.append((ins.target, wr))
            if ins.flow == FLOW_JMP:
                if not ins.rel:
                    break
                p = ins.target
            else:
                p += ins.length
    return tuple(sorted(k)), tuple(sorted(rets)), regs


def external_targets(img: Image, va: int, length: int) -> list[int]:
    """rel32 call and jump targets leaving [va, va + length), in a linear walk from va."""
    out = []
    p = va
    while p < va + length:
        ins = img.decode(p)
        if ins is None:
            break
        if ins.rel and ins.rel_size == 32 and not va <= ins.target < va + length:
            out.append(ins.target)
        p += ins.length
    return out


def shape_at(img: Image, va: int, bounds: list[int]) -> Shape | None:
    """The function at va, its extent stopped by the next known start (the signer's ExtentBefore)."""
    s = img.code_at(va)
    if s is None:
        return None
    i = bisect.bisect_right(bounds, va)
    bound = min(bounds[i] if i < len(bounds) else 1 << 32, s.code_end)
    length = extent(img, va, bound)
    if not length:
        return None
    k, ret, regs = trace(img, va, length)
    targets = external_targets(img, va, length)
    return Shape(va, length, ret, regs, len(targets), k, tuple(targets))


def regs_text(mask: int) -> list[str]:
    return [REG_NAMES[i] for i in range(8) if mask >> i & 1]


def regs_mask(names: list[str] | None) -> int:
    return sum(1 << REG_NAMES.index(n) for n in names or [])


# ----------------------------------------------------------------------------- tuning
#
# Every number here is explained, with the measurement behind it, in docs/align-symbols.md.

LIBRARY_SECTIONS = ("D3D", "DSOUND", "XPP")
STABLE = 0x10000  # constants from here up are hardware/ABI identities; below are layout offsets
LEN_BAND_BYTES = 8  # length band: |len - reference| <= max(8 bytes, 12% of the reference length)
LEN_BAND_SHARE = 0.12
CALLS_BAND = 1  # |calls - reference| <= 1
DISTINCT_DF = 2  # a shared stable constant found in at most 2 target functions of the section
MARGIN_MIN = 2.0  # alignment score lead of the chosen candidate over every alternative alignment
EXCLUSIVE_MIN = 1.5  # pair score lead of the chosen candidate over every other function it competes with
SIZE_RATIO_MAX = 3.0  # a tight bracket's two stretches differ in byte size by at most this factor

W_REGS, W_REGS_MISS = 2.0, -4.0
W_LEN_EXACT, W_LEN_IN_BASE, W_LEN_IN_SLOPE, W_LEN_OUT_MAX = 3.0, 0.5, 2.0, 3.0
W_CALLS_EQ, W_CALLS_STEP = 1.0, 1.0
W_K_DISTINCT, W_K_SHARED, W_K_MISSING = 3.0, 1.5, -1.5


def hx(v: int) -> str:
    return f"0x{v:X}"


# ----------------------------------------------------------------------------- reference names

@dataclass
class Ref:
    """One function of the 5558 sequence: a target name, or an unnamed function between them."""

    name: str
    sec: str
    addr: int  # in out/sims5558.exe
    ret: tuple[int, ...] | None
    regs: int | None
    lens: tuple[int, ...]
    calls: tuple[int, ...]
    k: frozenset[int]
    spacer: bool = False

    def describe(self) -> str:
        ret = "?" if self.ret is None else ("jmp" if not self.ret else "/".join(map(str, self.ret)))
        regs = "?" if self.regs is None else (",".join(regs_text(self.regs)) or "-")
        lens = "/".join(str(n) for n in self.lens) or "?"
        calls = "/".join(str(n) for n in self.calls) or "?"
        return f"ret {ret}, regs {regs}, len {lens}, calls {calls}"


def sig_ret(sig: dict) -> tuple[int, ...] | None:
    """Bytes a signature's rets pop; () when it was traced and leaves by jump; None when unknown."""
    if not sig:
        return None
    if "ret" in sig:
        r = sig["ret"]
        return tuple(sorted(set(r if isinstance(r, list) else [r])))
    return () if "regs" in sig else None


def stable(k) -> frozenset[int]:
    return frozenset(c for c in k if c >= STABLE)


def ret_text(ret: tuple[int, ...] | None) -> str:
    return "?" if ret is None else ("jmp" if not ret else "/".join(map(str, ret)))


def load_refs(targets: dict, s49: dict, s58: dict) -> tuple[dict[str, Ref], list[tuple[str, str, str]]]:
    """Target names of the library sections, with the invariants both reference builds record."""
    refs: dict[str, Ref] = {}
    other: list[tuple[str, str, str]] = []
    for group, rows in targets.items():
        for row in rows:
            name = row["name"]
            a, b = s49.get(name) or {}, s58.get(name) or {}
            sec = b.get("sec") or a.get("sec") or group
            if sec not in LIBRARY_SECTIONS:
                other.append((name, sec, "not a library section"))
                continue
            if not row.get("addr5558"):
                other.append((name, sec, "no address in out/sims5558.exe"))
                continue
            ra, rb = sig_ret(a), sig_ret(b)
            ret = ra if ra is not None else rb
            regs = regs_mask(a["regs"]) if "regs" in a else (regs_mask(b["regs"]) if "regs" in b else None)
            lens = tuple(sorted({s["len"] for s in (a, b) if s.get("len")}))
            calls = tuple(sorted({len(s.get("calls", [])) for s in (a, b) if s.get("len")}))
            shaped = [stable(s.get("k", [])) for s in (a, b) if "regs" in s]
            k = frozenset.intersection(*shaped) if shaped else frozenset()
            refs[name] = Ref(name, sec, row["addr5558"], ret, regs, lens, calls, k)
    return refs, other


# ----------------------------------------------------------------------------- the two images

def inventory(path: Path) -> list[int]:
    """Function starts the recompiler emitted (its X86FuncMappings table)."""
    return sorted({int(a, 16) for a in re.findall(r"\{0x([0-9A-F]+)u, sub_", path.read_text())})


def keep_starts(starts: list[int], shapes: dict[int, Shape], calls: set[int]) -> tuple[list[int], list[int]]:
    """Starts minus interior ones: not a call target and inside the extent of the start before it."""
    kept: list[int] = []
    interior: list[int] = []
    for v in starts:
        if v not in shapes:
            continue
        if kept and v not in calls and v < kept[-1] + shapes[kept[-1]].len:
            interior.append(v)
            continue
        kept.append(v)
    return kept, interior


@dataclass
class World:
    refs: dict[str, Ref]
    other: list[tuple[str, str, str]]
    anchors: dict[str, int]
    ff: Image
    ff_sec: dict[str, Section]
    ff_calls: set[int]
    ff_shapes: dict[int, Shape]
    ff_starts: dict[str, list[int]]
    ff_interior: dict[str, list[int]]
    ff_inventory: int
    ref_img: Image
    ref_calls: set[int]
    ref_bounds: list[int]
    ref_starts: list[int]
    ref_names: dict[int, str]
    ref_shapes: dict[int, Shape | None] = field(default_factory=dict)
    df: dict[str, Counter] = field(default_factory=dict)
    self_check: tuple[int, int, list[str]] = (0, 0, [])

    def ref_shape(self, va: int) -> Shape | None:
        if va not in self.ref_shapes:
            self.ref_shapes[va] = shape_at(self.ref_img, va, self.ref_bounds)
        return self.ref_shapes[va]

    def sec_of_ff(self, va: int) -> str | None:
        for name in LIBRARY_SECTIONS:
            s = self.ff_sec[name]
            if s.va <= va < s.va + s.vsize:
                return name
        return None

    def ref_span(self, sec: str) -> tuple[int, int]:
        """The 5558 addresses the section's target names occupy."""
        names = [r for r in self.refs.values() if r.sec == sec]
        lo = min(r.addr for r in names)
        hi = max(r.addr + max(r.lens or (1,)) for r in names)
        return lo, hi


def load_world() -> World:
    cfg = tomllib.loads((ROOT / "config/game.toml").read_text())
    targets = json.loads((ROOT / "docs/symbol-targets.json").read_text())
    s49 = tomllib.loads((ROOT / "sdk/xdk/5849/signatures.toml").read_text())["symbols"]
    s58 = tomllib.loads((ROOT / "config/ref/5558-signed.toml").read_text())["symbols"]
    refs, other = load_refs(targets, s49, s58)
    anchors = {n: v for n, v in cfg["symbols"].items() if v}

    # the target: out/ff.exe bytes, the section table of the XBE it was made from
    base, sections = xbe_sections(ROOT / cfg["xbe"]["path"])
    data = (ROOT / "out/ff.exe").read_bytes()
    xbe = (ROOT / cfg["xbe"]["path"]).read_bytes()
    for s in sections:
        o = s.va - base
        hdr = u32(xbe, 0x120) - base + sections.index(s) * 56
        raw = u32(xbe, hdr + 12)
        n = min(s.vsize, s.rsize)
        if data[o:o + n] != xbe[raw:raw + n]:
            raise SystemExit(f"out/ff.exe does not hold the bytes of {cfg['xbe']['path']} section {s.name}")
    ff = Image(data, base, sections, cfg["xbe"]["code_sections"])
    ff_calls = set(ff.sweep_calls())
    bounds = sorted(ff_calls | set(anchors.values()) | {int(k, 0) for k in cfg.get("functions", {})})
    ff_sec = {s.name: s for s in sections}
    inv = inventory(ROOT / cfg["xbe"]["out"] / "x86_func_mapping.cpp")
    ff_shapes: dict[int, Shape] = {}
    ff_starts: dict[str, list[int]] = {}
    ff_interior: dict[str, list[int]] = {}
    for name in LIBRARY_SECTIONS:
        s = ff_sec[name]
        starts = [v for v in inv if s.va <= v < s.va + s.vsize]
        for v in starts:
            sh = shape_at(ff, v, bounds)
            if sh:
                ff_shapes[v] = sh
        ff_starts[name], ff_interior[name] = keep_starts(starts, ff_shapes, ff_calls)

    # the reference: out/sims5558.exe, whose exports are its recompiler's function starts and names.
    # Its XBE is not in this repository, so its sections are rebuilt: .text up to the first library
    # target, code up to the end of the last export, which reproduces the signer's sweep exactly
    # (checked below against every signed function).
    rdata = (ROOT / "out/sims5558.exe").read_bytes()
    rbase, exports = pe_base_and_exports(rdata)
    isize = u32(rdata, u32(rdata, 0x3C) + 24 + 56)
    text_end = min(r.addr for r in refs.values())
    flat = Image(rdata, rbase, [Section("all", 0x11000 + rbase - 0x10000, isize, isize)], ["all"])
    last = max(exports)
    code_end = last + extent(flat, last, 1 << 32)
    lo = rbase + 0x1000
    rsecs = [Section(".text", lo, text_end - lo, text_end - lo),
             Section("library", text_end, code_end - text_end, code_end - text_end)]
    ref_img = Image(rdata, rbase, rsecs, [".text", "library"])
    ref_calls = set(ref_img.sweep_calls())
    named = {v for v, n in exports.items() if not n.startswith("sub_")}
    ref_bounds = sorted(ref_calls | named)
    ref_starts = sorted(set(exports) | {r.addr for r in refs.values()})
    world = World(refs, other, anchors, ff, ff_sec, ff_calls, ff_shapes, ff_starts, ff_interior, len(inv),
                  ref_img, ref_calls, ref_bounds, ref_starts, exports)

    # decoder self-check: every signed 5558 target function must come out as the signer recorded it
    checked = agreed = 0
    bad: list[str] = []
    for name, r in refs.items():
        sig = s58.get(name) or {}
        if not sig.get("len"):
            continue
        sh = world.ref_shape(r.addr)
        checked += 1
        want = (sig["len"], sig_ret(sig), regs_mask(sig.get("regs")), tuple(sig.get("k", [])),
                len(sig.get("calls", [])))
        got = (sh.len, sh.ret, sh.regs, sh.k, sh.calls) if sh else None
        if got == want:
            agreed += 1
        else:
            bad.append(name)
    world.self_check = (agreed, checked, bad)

    for name in LIBRARY_SECTIONS:
        df: Counter = Counter()
        for v in ff_starts[name]:
            df.update(stable(ff_shapes[v].k))
        world.df[name] = df
    return world


def spacer_ref(world: World, va: int, sec: str) -> Ref | None:
    sh = world.ref_shape(va)
    if sh is None:
        return None
    name = world.ref_names.get(va, f"sub_{va:08X}")
    return Ref(name, sec, va, sh.ret, sh.regs, (sh.len,), (sh.calls,), stable(sh.k), spacer=True)


# ----------------------------------------------------------------------------- scoring

@dataclass
class Verdict:
    """How one target function measures against one reference function."""

    score: float
    regs_ok: bool | None
    tlen: int
    lref: int  # the reference length (5849 or 5558) nearest tlen
    band: float
    dcalls: int
    k_status: str  # n/a: reference has no stable constant | missing | shared | distinctive
    k_shared: tuple[int, ...]

    @property
    def dlen(self) -> int:
        return abs(self.tlen - self.lref)

    @property
    def len_ok(self) -> bool:
        return self.dlen <= self.band

    @property
    def calls_ok(self) -> bool:
        return self.dcalls <= CALLS_BAND

    def required_failures(self) -> list[str]:
        """Checks a candidate must pass to be emitted, whatever else it has (ret is checked earlier)."""
        out = []
        if self.regs_ok is False:
            out.append("regs differ")
        if not self.calls_ok:
            out.append(f"calls off by {self.dcalls}")
        if self.k_status == "missing":
            out.append("shares none of the reference's stable constants")
        if not self.len_ok and self.k_status != "distinctive":
            out.append(f"len {self.tlen - self.lref:+d} outside ±{self.band:.0f} with no distinctive constant")
        return out

    @property
    def strict(self) -> bool:
        """Every check passes, the length band included."""
        return not self.required_failures() and self.len_ok

    def holds(self) -> str:
        parts = ["ret"]
        if self.regs_ok:
            parts.append("regs")
        if self.len_ok:
            parts.append("len exact" if not self.dlen else f"len {self.tlen - self.lref:+d} (band ±{self.band:.0f})")
        if self.calls_ok:
            parts.append("calls" if not self.dcalls else "calls ±1")
        if self.k_status in ("shared", "distinctive"):
            parts.append(("distinctive " if self.k_status == "distinctive" else "") + "k " +
                         " ".join(hx(c) for c in self.k_shared))
        return ", ".join(parts)

    def failed(self) -> str:
        out = []
        if self.regs_ok is False:
            out.append("regs")
        if not self.len_ok:
            out.append(f"len {self.tlen - self.lref:+d}")
        if not self.calls_ok:
            out.append(f"calls ±{self.dcalls}")
        if self.k_status == "missing":
            out.append("k")
        return ", ".join(out) or "none"


def evaluate(ref: Ref, t: Shape, df: Counter) -> Verdict | None:
    """The pairing of a reference function with a target function, None when ret differs."""
    if ref.ret is not None and ref.ret != t.ret:
        return None
    score = 0.0
    regs_ok = None if ref.regs is None else ref.regs == t.regs
    if regs_ok is not None:
        score += W_REGS if regs_ok else W_REGS_MISS
    lens = ref.lens or (t.len,)
    lref = min(lens, key=lambda n: abs(t.len - n))
    dlen = abs(t.len - lref)
    band = max(LEN_BAND_BYTES, LEN_BAND_SHARE * lref)
    if dlen == 0:
        score += W_LEN_EXACT
    elif dlen <= band:
        score += W_LEN_IN_BASE + W_LEN_IN_SLOPE * (1 - dlen / band)
    else:
        score -= min(W_LEN_OUT_MAX, 0.5 + (dlen - band) / band)
    dcalls = min(abs(t.calls - c) for c in ref.calls) if ref.calls else 0
    score += W_CALLS_EQ if dcalls == 0 else -W_CALLS_STEP * (dcalls - 1)
    shared = tuple(sorted(ref.k & stable(t.k)))
    if not ref.k:
        k_status = "n/a"
    elif not shared:
        k_status = "missing"
        score += W_K_MISSING
    elif any(df[c] <= DISTINCT_DF for c in shared):
        k_status = "distinctive"
        score += W_K_DISTINCT
    else:
        k_status = "shared"
        score += W_K_SHARED
    return Verdict(score, regs_ok, t.len, lref, band, dcalls, k_status, shared)


# ----------------------------------------------------------------------------- brackets and regions

@dataclass
class Anchor:
    name: str
    ref: int  # 5558 address
    ff: int


@dataclass
class Region:
    """One monotone alignment: a run of the 5558 sequence against a run of the target sequence."""

    sec: str
    kind: str  # tight | loose | inverted | forward | backward | unanchored
    left: Anchor | None
    right: Anchor | None
    refs: list[Ref]
    cands: list[Shape]
    placements: dict[str, tuple] = field(default_factory=dict)
    path: list[tuple[int, int]] = field(default_factory=list)


def section_anchors(world: World, anchors: dict[str, int], sec: str) -> list[Anchor]:
    out = []
    for name, va in anchors.items():
        r = world.refs.get(name)
        if r and r.sec == sec and world.sec_of_ff(va) == sec:
            out.append(Anchor(name, r.addr, va))
    return out


def bracket(world: World, anchors: dict[str, int], ref: Ref) -> tuple[str, Anchor | None, Anchor | None]:
    """The anchors nearest the name in 5558 order, and how the target agrees with them.

    tight: the two anchors are in the same order in the target, nothing proven lies between them
    there, and the two stretches they enclose are of comparable size. loose: same order, but the
    target stretch holds other anchors (a moved block) or the stretches differ grossly in size.
    """
    sa = section_anchors(world, anchors, ref.sec)
    below = max((a for a in sa if a.ref < ref.addr), key=lambda a: a.ref, default=None)
    above = min((a for a in sa if a.ref > ref.addr), key=lambda a: a.ref, default=None)
    if below and above:
        if below.ff > above.ff:
            return "inverted", below, above
        inside = any(below.ff < v < above.ff for v in anchors.values())
        span_ref, span_ff = above.ref - below.ref, above.ff - below.ff
        ratio = max(span_ref, span_ff) / max(1, min(span_ref, span_ff))
        return ("loose" if inside or ratio > SIZE_RATIO_MAX else "tight"), below, above
    if below or above:
        return ("forward" if below else "backward"), below, above
    return "unanchored", None, None


def build_region(world: World, anchors: dict[str, int], sec: str, kind: str,
                 left: Anchor | None, right: Anchor | None) -> Region:
    s = world.ff_sec[sec]
    lo58, hi58 = world.ref_span(sec)
    anchor_ff = set(anchors.values())
    anchor_ref = {world.refs[n].addr for n in anchors if n in world.refs}
    ff_sorted = sorted(a for a in anchor_ff if s.va <= a < s.va + s.vsize)

    def next_anchor_ff(v: int) -> int:
        return min((a for a in ff_sorted if a > v), default=s.va + s.vsize)

    def prev_anchor_ff(v: int) -> int:
        return max((a for a in ff_sorted if a < v), default=s.va - 1)

    # 5558 run
    r_lo = left.ref if left else lo58 - 1
    r_hi = right.ref if right else hi58
    # target runs, in the order the 5558 run expects them
    if kind in ("tight", "loose"):
        spans = [(left.ff, right.ff)]
    elif kind == "inverted":
        spans = [(left.ff, next_anchor_ff(left.ff)), (prev_anchor_ff(right.ff), right.ff)]
    elif kind == "forward":
        spans = [(left.ff, next_anchor_ff(left.ff))]
    elif kind == "backward":
        spans = [(prev_anchor_ff(right.ff), right.ff)]
    else:
        spans = [(s.va - 1, s.va + s.vsize)]
    cands: list[Shape] = []
    for a, b in spans:
        for v in world.ff_starts[sec]:
            if a < v < b and v not in anchor_ff:
                cands.append(world.ff_shapes[v])
    by_addr = {r.addr: r for r in world.refs.values() if r.sec == sec}
    refs: list[Ref] = []
    for v in world.ref_starts:
        if not r_lo < v < r_hi or v in anchor_ref:
            continue
        if v in by_addr:
            refs.append(by_addr[v])
        elif v in world.ref_calls or not refs or v >= refs[-1].addr + max(refs[-1].lens or (0,)):
            sp = spacer_ref(world, v, sec)
            if sp:
                refs.append(sp)
    return Region(sec, kind, left, right, refs, cands)


def align(region: Region, df: Counter) -> None:
    """Best monotone pairing (Needleman-Wunsch, free gaps) and each name's max-marginal lead."""
    refs, cands = region.refs, region.cands
    n, m = len(refs), len(cands)
    S: list[list[float | None]] = []
    V: list[list[Verdict | None]] = []
    for r in refs:
        row_v = [evaluate(r, t, df) for t in cands]
        V.append(row_v)
        S.append([v.score if v else None for v in row_v])
    F = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        fi, fp, si = F[i], F[i - 1], S[i - 1]
        for j in range(1, m + 1):
            best = fp[j] if fp[j] > fi[j - 1] else fi[j - 1]
            s = si[j - 1]
            if s is not None and fp[j - 1] + s > best:
                best = fp[j - 1] + s
            fi[j] = best
    G = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        gi, gn, si = G[i], G[i + 1], S[i]
        for j in range(m - 1, -1, -1):
            best = gn[j] if gn[j] > gi[j + 1] else gi[j + 1]
            s = si[j]
            if s is not None and gn[j + 1] + s > best:
                best = gn[j + 1] + s
            gi[j] = best
    # the best path, for neighbour context
    i, j = n, m
    path = []
    while i and j:
        s = S[i - 1][j - 1]
        if s is not None and F[i][j] == F[i - 1][j - 1] + s and s > 0:
            path.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif F[i][j] == F[i - 1][j]:
            i -= 1
        else:
            j -= 1
    region.path = path[::-1]
    for i, r in enumerate(refs):
        if r.spacer:
            continue
        skip = max(F[i][j] + G[i + 1][j] for j in range(m + 1))
        options = sorted(((F[i][j] + S[i][j] + G[i + 1][j + 1], j) for j in range(m) if S[i][j] is not None),
                         reverse=True)
        if options and options[0][0] > skip:
            best, j = options[0]
            rival = max(skip, options[1][0] if len(options) > 1 else -math.inf)
            rival_j = options[1][1] if len(options) > 1 and options[1][0] >= skip else None
            region.placements[r.name] = (j, V[i][j], best - rival, rival_j, i)
        else:
            lead = skip - (options[0][0] if options else -math.inf)
            region.placements[r.name] = (None, None, lead, options[0][1] if options else None, i)


# ----------------------------------------------------------------------------- decisions

@dataclass
class Result:
    ref: Ref
    kind: str
    left: Anchor | None
    right: Anchor | None
    region: Region
    choice: Shape | None = None
    verdict: Verdict | None = None
    lead: float = 0.0  # alignment lead over the best alignment that places the name elsewhere or nowhere
    bracket_rival: tuple[Shape, Verdict] | None = None  # best other function in the bracket, pair score
    section_rival: tuple[Shape, Verdict] | None = None  # best other function in the section, pair score
    ranked: list[tuple[Shape, Verdict]] = field(default_factory=list)  # whole section, best first
    emit: bool = False
    reasons: list[str] = field(default_factory=list)

    def gap(self, rival: tuple[Shape, Verdict] | None) -> float:
        return math.inf if rival is None else self.verdict.score - rival[1].score


def section_ranking(world: World, ref: Ref, anchors: dict[str, int]) -> list[tuple[Shape, Verdict]]:
    """Every target function of the section scored against the name, best first."""
    df = world.df[ref.sec]
    taken = set(anchors.values())
    scored = []
    for v in world.ff_starts[ref.sec]:
        if v in taken:
            continue
        ver = evaluate(ref, world.ff_shapes[v], df)
        if ver:
            scored.append((world.ff_shapes[v], ver))
    scored.sort(key=lambda x: -x[1].score)
    return scored


def best_other(scored: list[tuple[Shape, Verdict]], va: int) -> tuple[Shape, Verdict] | None:
    return next((x for x in scored if x[0].va != va), None)


def decide(world: World, anchors: dict[str, int], names: list[str]) -> dict[str, Result]:
    """Places every name and decides which placements the image proves."""
    regions: dict[tuple, Region] = {}
    results: dict[str, Result] = {}
    for name in names:
        ref = world.refs[name]
        kind, left, right = bracket(world, anchors, ref)
        key = (ref.sec, kind, left.name if left else None, right.name if right else None)
        if key not in regions:
            region = build_region(world, anchors, ref.sec, kind, left, right)
            align(region, world.df[ref.sec])
            regions[key] = region
        region = regions[key]
        res = results[name] = Result(ref, kind, left, right, region)
        res.ranked = section_ranking(world, ref, anchors)
        j, ver, lead, _, _ = region.placements[name]
        res.lead = lead
        why = res.reasons
        if kind not in ("tight", "loose"):
            why.append({"inverted": "its nearest anchors are in opposite order in the target (a moved block)",
                        "forward": "anchored on one side only: no anchor above it in 5558 order",
                        "backward": "anchored on one side only: no anchor below it in 5558 order",
                        "unanchored": "no proven anchor in this section"}[kind])
        if j is None:
            why.append("the alignment leaves it unmatched" if region.cands else "no target function in the bracket")
            continue
        res.choice, res.verdict = region.cands[j], ver
        in_bracket = sorted(((c, v) for c in region.cands if (v := evaluate(ref, c, world.df[ref.sec]))),
                            key=lambda x: -x[1].score)
        res.bracket_rival = best_other(in_bracket, res.choice.va)
        res.section_rival = best_other(res.ranked, res.choice.va)
        if ref.ret is None:
            why.append("reference ret unknown")
        why.extend(ver.required_failures())
        if lead < MARGIN_MIN:
            why.append(f"ambiguous alignment: lead {lead:.2f} < {MARGIN_MIN}")
        if res.gap(res.bracket_rival) < EXCLUSIVE_MIN:
            rv = res.bracket_rival
            why.append(f"not exclusive in its bracket: {hx(rv[0].va)} scores {rv[1].score:.2f} "
                       f"against {ver.score:.2f}")
        if kind == "loose":
            if not ver.len_ok:
                why.append("loose bracket and length outside band")
            if res.gap(res.section_rival) < EXCLUSIVE_MIN:
                rv = res.section_rival
                why.append(f"loose bracket and not exclusive in {ref.sec}: {hx(rv[0].va)} scores "
                           f"{rv[1].score:.2f} against {ver.score:.2f}")
        res.emit = not why
    # no address may carry two names
    by_addr: dict[int, list[str]] = {}
    for name, res in results.items():
        if res.emit:
            by_addr.setdefault(res.choice.va, []).append(name)
    for va, names_at in by_addr.items():
        if len(names_at) > 1:
            for name in names_at:
                results[name].emit = False
                results[name].reasons.append(f"{hx(va)} is also chosen for {', '.join(n for n in names_at if n != name)}")
    return results


def holdout(world: World) -> list[tuple[str, int, Result]]:
    """Each library anchor removed in turn and placed by the rest, as if it were unknown."""
    out = []
    for name, va in sorted(world.anchors.items(), key=lambda x: x[1]):
        if name not in world.refs:
            continue
        rest = {n: v for n, v in world.anchors.items() if n != name}
        out.append((name, va, decide(world, rest, [name])[name]))
    return out
