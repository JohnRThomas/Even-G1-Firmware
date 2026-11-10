#!/usr/bin/env python3
"""Writes the packet buffers of the g1 suite (tests/reg/g1/packets/<name>.hex, one packet of the G1 app a line, as
buf('NAME', 3) and buf('NAME', 2, 24) take them): the bulk transfers of image/bitmap.ref, dashboard/stocks.ref,
dashboard/canvas.ref and navigation/session.ref.

    python3 tests/reg/g1/packets/gen_packets.py [DIRECTORY]      (default: the directory of this script)

The content is made up: where the app would send a picture, a chart or a map there is noise, the first bytes of
SHAKE-128 over a name, so that every run sends the same bytes. Only the framing is the app's. The code is that of
the first regression runner (commit f9788035, tests/reg/run.py: f, chunked, noise, stock, bmp), which built these
packets in its scenarios. tests/reg/check_packets.py compares what this writes with the .hex files here."""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BITMAP = """\
# image/bitmap.ref: the 0x15 packets of the bitmap upload, a 576x136 1-bpp BMP of noise (%d bytes) in 194-byte
# pieces: 15, the packet index from 0, the piece; the first packet carries the flash address 00 1C 00 00."""
STOCK = """\
# dashboard/stocks.ref: stock %d, %s with its 1886-byte chart of noise (0x06 sub 4), in %d chunks:
# 06, u16 length, seq, 04, u16 chunk count, u16 chunk index from 1, the piece."""
CANVAS_MAP = """\
# dashboard/canvas.ref: the canvas background map (0x06 sub 7), 6392 bytes of noise behind 01 04 02 01,
# in %d chunks: 06, u16 length, seq, 07, u16 chunk count, u16 chunk index from 1, the piece."""
NAV_OVERVIEW_MAP = """\
# navigation/session.ref: the overview map (0x0A sub 2), an RLE-coded tile in %d chunks:
# 0A, u16 length, seq, 02, u16 chunk count, u16 chunk index from 1, the piece."""
NAV_PANORAMIC = """\
# navigation/session.ref: the panoramic map (0x0A sub 3), 16592 bytes of noise in %d chunks:
# 0A, u16 length, seq, 03, u16 chunk count, u16 chunk index from 1, 00, the piece."""
USE = """\
# One packet a line; buf('%s', n) is packet n, from 1, and buf('%s', 1, %d) all %d of them.
# Made by packets/gen_packets.py."""
# the two stocks: number, buffer name, seq of the first chunk, the texts (symbol, name, price, change, percent), status
STOCKS = ((1, "gme", 3, ("GME.XNYS", "GameStop Corp", "21.36", "-0.09", "-0.42%"), 2),
          (2, "aapl", 20, ("AAPL.XNAS", "Apple Inc", "270.10", "+1.25", "+0.46%"), 1))


def u(n, size=2):
    """n as size bytes, little-endian."""
    return (n & (1 << 8 * size) - 1).to_bytes(size, "little")


def frame(cmd, seq, sub, body=b""):
    """A packet of the app: cmd, u16 length of the whole packet, seq, sub, body."""
    return bytes((cmd,)) + u(5 + len(body)) + bytes((seq & 255, sub)) + bytes(body)


def chunks(data, size):
    """The data in pieces of size bytes, the last one shorter."""
    return [data[i:i + size] for i in range(0, len(data), size)] or [b""]


def chunked(cmd, seq, sub, data, size=180, head=b""):
    """The data as framed chunks: u16 chunk count, u16 chunk index from 1, head, the piece; a new seq for each chunk,
    as the app does."""
    c = chunks(data, size)
    return [frame(cmd, seq + i, sub, u(len(c)) + u(i + 1) + head + p) for i, p in enumerate(c)]


def noise(n, key):
    """n made-up bytes, the same in every run: the first bytes of SHAKE-128 over the key."""
    return hashlib.shake_128(key.encode()).digest(n)


def tlv(texts):
    """The texts as {number from 1, length, text}."""
    return b"".join(bytes((k, len(x))) + x.encode() for k, x in enumerate(texts, 1))


def stock(n, texts, status):
    """What the app sends for dashboard stock n: 01 01 02 n 01, the texts, 06 01 status, 07, u16 size of the chart
    and the chart, 1886 bytes of noise."""
    return bytes((1, 1, 2, n, 1)) + tlv(texts) + bytes((6, 1, status, 7)) + u(1886) + noise(1886, "chart%d" % n)


def bmp(w=576, h=136, pad=2):
    """A 1-bit BMP file of noise, palette black and white, with pad zero bytes behind the pixels: they bring the
    file to a multiple of 4 bytes, as the QSPI flash wants."""
    px = noise(w * h // 8, "bitmap")
    file_header = b"BM" + struct.pack("<IHHI", 62 + len(px) + pad, 0, 0, 62)
    info_header = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 1, 0, len(px), 0, 0, 2, 0)
    return file_header + info_header + b"\0\0\0\0\xff\xff\xff\0" + px + bytes(pad)


def bitmap():
    """bitmap.hex: the BMP in pieces of 194 bytes, each behind 15 and its index from 0 (one byte); the first packet
    has the flash address between the index and the piece."""
    image = bmp()
    packets = [bytes((0x15, i & 255)) + b"\0\x1c\0\0" * (i == 0) + p for i, p in enumerate(chunks(image, 194))]
    return "bitmap", BITMAP % len(image), packets


def stocks():
    """stock_gme.hex and stock_aapl.hex: the two stocks of the dashboard, each with its chart."""
    for n, name, seq, texts, status in STOCKS:
        packets = chunked(6, seq, 4, stock(n, texts, status))
        yield "stock_" + name, STOCK % (n, texts[0], len(packets)), packets


def canvas_map():
    """canvas_map.hex: the map behind the dashboard canvas."""
    packets = chunked(6, 4, 7, b"\1\4\2\1" + noise(6392, "map"))
    return "canvas_map", CANVAS_MAP % len(packets), packets


def nav_overview_map():
    """nav_overview_map.hex: 272 pairs {17, a noise byte}, which the firmware reads as run lengths."""
    packets = chunked(0x0A, 0x78, 2, b"".join(bytes((17, x)) for x in noise(272, "overview")), 185)
    return "nav_overview_map", NAV_OVERVIEW_MAP % len(packets), packets


def nav_panoramic():
    """nav_panoramic.hex: the panoramic map and its mask, with a 00 before each piece."""
    packets = chunked(0x0A, 0x81, 3, noise(16592, "panoramic"), 185, b"\0")
    return "nav_panoramic", NAV_PANORAMIC % len(packets), packets


def write(out_dir, name, header, packets):
    """Writes NAME.hex: the header, how a test takes the packets, then one packet a line as hex bytes."""
    lines = (header + "\n" + USE % (name, name, len(packets), len(packets))).splitlines()
    with open(os.path.join(out_dir, name + ".hex"), "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines + [p.hex(" ").upper() for p in packets]) + "\n")
    print("%-18s %d packets" % (name, len(packets)))


def main(out_dir=HERE):
    for name, header, packets in (bitmap(), *stocks(), canvas_map(), nav_overview_map(), nav_panoramic()):
        write(out_dir, name, header, packets)


if __name__ == "__main__":
    main(*sys.argv[1:2])
