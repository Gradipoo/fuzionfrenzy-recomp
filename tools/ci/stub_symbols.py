#!/usr/bin/env python3
"""Write a stand-in image_symbols.h so the runtime can compile with no game.

SYM_ and HAS_ names come from the runtime sources. Each SYM_ value is a
unique synthetic token, not an address from a disc image. XDK_ values come
from the XDK layout files already in sdk/xdk. The header is written under
the build directory and is not committed.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCAN_ROOTS = [ROOT / "sdk" / "runtime", ROOT / "src"]
LAYOUT_ROOT = ROOT / "sdk" / "xdk"

IDENT = r"[A-Za-z_][A-Za-z0-9_]*"
SYM_RE = re.compile(rf"\b(?:SYM|HAS|GUEST|GUEST_ORIG)_({IDENT})\b|\b(?:SYM|GUEST|GUEST_ORIG)\(({IDENT})\)")
XDK_RE = re.compile(rf"\bXDK_({IDENT})\b|\bXDK_(?:KEY|DATA)\(({IDENT})\)")
LAYOUT_RE = re.compile(rf"^({IDENT})\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)\b")
GPU_RE = re.compile(r"\bX\(([A-Za-z0-9_]+)\)")


def scan_sources():
    syms = set()
    xdks = set()
    for root in SCAN_ROOTS:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix not in {".h", ".hpp", ".cpp", ".c"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in SYM_RE.finditer(text):
                syms.add(m.group(1) or m.group(2))
            for m in XDK_RE.finditer(text):
                xdks.add(m.group(1) or m.group(2))
    return syms, xdks


def c_value(raw):
    # A layout file uses -1 for a state the XDK build does not have.
    # The runtime's absent sentinel is all-bits-one.
    if raw.startswith("-"):
        return "0xFFFFFFFFu"
    return raw


def scan_layouts():
    values = {}
    if not LAYOUT_ROOT.is_dir():
        return values
    for path in sorted(LAYOUT_ROOT.glob("*/layout.toml")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            body = line.split("#", 1)[0].strip()
            m = LAYOUT_RE.match(body)
            if m:
                values[m.group(1)] = c_value(m.group(2))
    state = ROOT / "sdk" / "runtime" / "gpu" / "state.h"
    if state.is_file():
        lines = state.read_text(encoding="utf-8", errors="replace").splitlines()
        for prefix, macro in (
            ("RenderState_", "GPU_RENDER_STATES"),
            ("TextureState_", "GPU_TEXTURE_STAGE_STATES"),
        ):
            chunk = []
            grab = False
            for line in lines:
                if not grab and f"#define {macro}(X)" in line:
                    grab = True
                if grab:
                    chunk.append(line)
                    if not line.rstrip().endswith("\\"):
                        break
            for name in GPU_RE.findall("\n".join(chunk)):
                values.setdefault(prefix + name, "0xFFFFFFFFu")
    return values


def main():
    if len(sys.argv) != 2:
        print("usage: stub_symbols.py <image_symbols.h>", file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    syms, xdks = scan_sources()
    layouts = scan_layouts()
    xdks.update(layouts)
    lines = [
        "#pragma once",
        "// Stand-in for a CI compile. Not an image. SYM tokens are synthetic.",
        '#define IMAGE_XBE "ci.xbe"',
    ]
    for i, name in enumerate(sorted(syms), start=1):
        lines.append(f"#define SYM_{name} {i:08X}")
        lines.append(f"#define HAS_{name} 0")
    # These are macros in layout.h, not layout keys. Defining them here
    # would replace the function-like macros with an integer.
    skip = {"DATA", "KEY", "RENDER_STATE", "REQUIRE", "TEXTURE_STATE"}
    for name in sorted(xdks):
        if name in skip:
            continue
        lines.append(f"#define XDK_{name} {layouts.get(name, '0xFFFFFFFFu')}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"stub_symbols: {len(syms)} symbols, {len(xdks)} layout keys -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
