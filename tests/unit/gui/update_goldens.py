#!/usr/bin/env python3
"""Regenerate the gui suite's golden images, deliberately.

Review the PNG previews before accepting.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GOLDEN = os.path.join(HERE, "golden")


def read_pnm(path):
    data = open(path, "rb").read()
    magic = data[:2]
    pos = 2
    vals = []
    need = 3 if magic == b"P5" else 2
    while len(vals) < need:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        start = pos
        while data[pos:pos + 1].isdigit():
            pos += 1
        vals.append(int(data[start:pos]))
    pos += 1
    w, h = vals[0], vals[1]
    rows = []
    if magic == b"P4":
        stride = (w + 7) // 8
        for y in range(h):
            row = data[pos + y * stride:pos + (y + 1) * stride]
            rows.append([15 if row[x // 8] & (0x80 >> (x % 8)) else 0 for x in range(w)])
    elif magic == b"P5":
        for y in range(h):
            rows.append(list(data[pos + y * w:pos + (y + 1) * w]))
    else:
        raise ValueError("%s: not P4/P5" % path)
    return w, h, rows


def write_png(path, w, h, rows, scale=2):
    """level 15 = white, scaled up"""
    raw = bytearray()
    for row in rows:
        line = bytes(min(255, v * 17) for v in row for _ in range(scale))
        for _ in range(scale):
            raw.append(0)
            raw.extend(line)

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-O", "--outdir", default="build_unit_gui")
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--accept", action="append", default=[], metavar="NAME")
    ap.add_argument("--accept-all", action="store_true")
    args = ap.parse_args()

    out = args.outdir if os.path.isabs(args.outdir) else os.path.join(REPO, args.outdir)
    if not args.no_run:
        # the suite fails while renders differ
        subprocess.call([os.path.join(REPO, "tests", "unit", "run.sh"), "-s", "gui", "-O", out],
                        cwd=REPO)
    actual = os.path.join(out, "native_sim", "unit.gui", "gui_actual")
    if not os.path.isdir(actual):
        sys.exit("no renders in %s (did the gui suite build?)" % actual)

    renders = sorted(f for f in os.listdir(actual) if f.endswith((".pbm", ".pgm")))
    if not renders:
        print("every render matches its golden")
        return 0
    for f in renders:
        w, h, rows = read_pnm(os.path.join(actual, f))
        png = os.path.join(actual, os.path.splitext(f)[0] + ".png")
        write_png(png, w, h, rows)
        state = "changed" if os.path.exists(os.path.join(GOLDEN, f)) else "new"
        print("%-8s %-28s %dx%d  preview %s" % (state, f, w, h, png))

    accept = set(args.accept)
    unknown = accept - {os.path.splitext(f)[0] for f in renders}
    if unknown:
        sys.exit("no differing render named %s" % ", ".join(sorted(unknown)))
    copied = 0
    for f in renders:
        if args.accept_all or os.path.splitext(f)[0] in accept:
            os.makedirs(GOLDEN, exist_ok=True)
            shutil.copyfile(os.path.join(actual, f), os.path.join(GOLDEN, f))
            print("accepted %s" % f)
            copied += 1
    if copied == 0:
        print("nothing accepted (review the previews, then --accept NAME or --accept-all)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
