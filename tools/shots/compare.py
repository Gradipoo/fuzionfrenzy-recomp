#!/usr/bin/env python3
"""Perceptual compare of two 24- or 32-bit BMPs.

The score is the mean absolute difference of R, G, and B, on a 0–255 scale.
Alpha is ignored. A size mismatch fails. Exit 0 when the score is within the
threshold, exit 1 otherwise. --self-check builds two synthetic BMPs and does
not read a game frame.
"""

import argparse
import struct
import sys
import tempfile
from pathlib import Path


def load_bmp(path):
    data = Path(path).read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise SystemExit(f"compare: not a BMP: {path}")
    pixel_off = struct.unpack_from("<I", data, 10)[0]
    dib = struct.unpack_from("<I", data, 14)[0]
    if dib < 40:
        raise SystemExit(f"compare: unsupported BMP header: {path}")
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bpp = struct.unpack_from("<HH", data, 26)
    if planes != 1 or bpp not in (24, 32):
        raise SystemExit(f"compare: need 24- or 32-bit BMP, got {bpp}: {path}")
    top_down = height < 0
    height = abs(height)
    channels = bpp // 8
    stride = (width * channels + 3) & ~3
    rows = []
    for y in range(height):
        src = pixel_off + y * stride
        row = []
        for x in range(width):
            i = src + x * channels
            b, g, r = data[i], data[i + 1], data[i + 2]
            row.append((r, g, b))
        rows.append(row)
    if not top_down:
        rows.reverse()
    return width, height, rows


def mean_abs(a, b):
    aw, ah, ap = a
    bw, bh, bp = b
    if (aw, ah) != (bw, bh):
        return None
    total = 0
    n = aw * ah * 3
    for y in range(ah):
        for x in range(aw):
            for c in range(3):
                total += abs(ap[y][x][c] - bp[y][x][c])
    return total / n


def write_bmp(path, rgb, paint):
    h = len(rgb)
    w = len(rgb[0])
    # 32-bit top-down, the same shape the runtime writes.
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = paint(x, y, rgb[y][x])
            pixels += bytes((b, g, r, 255))
    size = 54 + len(pixels)
    header = bytearray(54)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 2, size)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<i", header, 18, w)
    struct.pack_into("<i", header, 22, -h)
    struct.pack_into("<H", header, 26, 1)
    struct.pack_into("<H", header, 28, 32)
    struct.pack_into("<I", header, 34, len(pixels))
    path.write_bytes(header + pixels)


def self_check():
    base = [[(10, 20, 30), (40, 50, 60)], [(70, 80, 90), (100, 110, 120)]]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        same = root / "same.bmp"
        diff = root / "diff.bmp"
        write_bmp(same, base, lambda x, y, p: p)
        write_bmp(diff, base, lambda x, y, p: (p[0], p[1], p[2] + 30) if (x, y) == (0, 0) else p)
        identical = mean_abs(load_bmp(same), load_bmp(same))
        changed = mean_abs(load_bmp(same), load_bmp(diff))
    if identical != 0:
        raise SystemExit(f"self-check: identical score {identical}")
    # One channel of one pixel of four pixels moves by 30. Mean over 12 channels.
    expect = 30 / 12
    if abs(changed - expect) > 1e-9:
        raise SystemExit(f"self-check: changed score {changed} want {expect}")
    print("compare: self-check PASS")
    return 0


def main():
    p = argparse.ArgumentParser(description="Mean absolute RGB difference of two BMPs")
    p.add_argument("reference", nargs="?")
    p.add_argument("candidate", nargs="?")
    p.add_argument("--threshold", type=float, default=4.0)
    p.add_argument("--self-check", action="store_true")
    args = p.parse_args()
    if args.self_check:
        return self_check()
    if not args.reference or not args.candidate:
        p.error("need two BMPs, or --self-check")
    score = mean_abs(load_bmp(args.reference), load_bmp(args.candidate))
    if score is None:
        print(f"FAIL size {args.reference} {args.candidate}")
        return 1
    status = "PASS" if score <= args.threshold else "FAIL"
    print(f"{status} score {score:.3f} threshold {args.threshold:.3f}")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
