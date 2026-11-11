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
    """Size and levels (0-15) of a render as gui_fb.c writes it: "P4\\n<width> <height>\\n" and
    1-bit rows, or "P5\\n<width> <height>\\n15\\n" and a byte per pixel. Pixel bytes can be
    newlines, so the header is split off by count."""
    with open(path, "rb") as handle:
        magic, size, pixels = handle.read().split(b"\n", 2)
    width, height = map(int, size.split())
    rows = []
    if magic == b"P4":
        stride = (width + 7) // 8
        for row in range(height):
            line = pixels[row * stride:(row + 1) * stride]
            rows.append([15 if line[col // 8] & (0x80 >> (col % 8)) else 0 for col in range(width)])
    elif magic == b"P5":
        pixels = pixels.split(b"\n", 1)[1]
        for row in range(height):
            rows.append(list(pixels[row * width:(row + 1) * width]))
    else:
        raise ValueError("%s: not P4/P5" % path)
    return width, height, rows


def write_png(path, width, height, rows, scale=2):
    """level 15 = white, scaled up"""
    raw = bytearray()
    for row in rows:
        line = b"".join(bytes([min(255, level * 17)]) * scale for level in row)
        raw += (b"\0" + line) * scale

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, height * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-O", "--outdir", default="build_unit_gui")
    parser.add_argument("--no-run", action="store_true")
    parser.add_argument("--accept", action="append", default=[], metavar="NAME")
    parser.add_argument("--accept-all", action="store_true")
    args = parser.parse_args()

    out = args.outdir if os.path.isabs(args.outdir) else os.path.join(REPO, args.outdir)
    if not args.no_run:
        # the suite fails while renders differ
        subprocess.call([os.path.join(REPO, "tests", "unit", "run.sh"), "-s", "gui", "-O", out],
                        cwd=REPO)
    actual = os.path.join(out, "native_sim", "unit.gui", "gui_actual")
    if not os.path.isdir(actual):
        sys.exit("no renders in %s (did the gui suite build?)" % actual)

    renders = sorted(name for name in os.listdir(actual) if name.endswith((".pbm", ".pgm")))
    if not renders:
        print("every render matches its golden")
        return 0
    for name in renders:
        width, height, rows = read_pnm(os.path.join(actual, name))
        png = os.path.join(actual, os.path.splitext(name)[0] + ".png")
        write_png(png, width, height, rows)
        state = "changed" if os.path.exists(os.path.join(GOLDEN, name)) else "new"
        print("%-8s %-28s %dx%d  preview %s" % (state, name, width, height, png))

    accept = set(args.accept)
    unknown = accept - {os.path.splitext(name)[0] for name in renders}
    if unknown:
        sys.exit("no differing render named %s" % ", ".join(sorted(unknown)))
    accepted = [name for name in renders if args.accept_all or os.path.splitext(name)[0] in accept]
    for name in accepted:
        os.makedirs(GOLDEN, exist_ok=True)
        shutil.copyfile(os.path.join(actual, name), os.path.join(GOLDEN, name))
        print("accepted %s" % name)
    if not accepted:
        print("nothing accepted (review the previews, then --accept NAME or --accept-all)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
