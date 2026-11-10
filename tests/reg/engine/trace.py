"""A run reduced to a trace: Item(t, lens, text, n, k) in time order, from the simulator's log and frame recordings.

Items: what the virtual phone logs, as the suite's protocol writes it (its suite.py: the packets it receives,
its link events), "audio start" / "audio end" (a stream of microphone packets), "boot app" / "boot net",
"fatal: <text>", "?<query> = <output>" for the queries, "frame <sha12>" for panel contents, and "log <text>"
for the firmware log lines in LOGGED.

A panel content is what the panel shows: the frame buffer while the panel has its supplies, BLANK while it has
not (the driver reads the supplies after every step and hands each change to LiveTrace.power(); lit()). A
content that stays up for less than SETTLE is a redraw in progress and is not an item, so the trace at a time
t is complete for the panels up to t - SETTLE only.

Everything but the frames is read from text the simulator's models and the firmware write for people (the
regular expressions below and the protocol's). So that a changed wording cannot pass for silence, a run must
show the power-on boot of each lens (BOOTS; statements.run() fails it otherwise), and the protocol raises
Unread for a line of the phone it does not know.
"""
import base64
import collections
import hashlib
import os
import re
import struct
import zlib

from .inputs import M, S

# n: the item's place in the trace it belongs to. k: its place among the items in the order they came to the
# trace (LiveTrace.came() counts them), None for a panel content: of two items with the same time, the one
# that was there before an input was sent is not an answer to it (asserts.Window).
Item = collections.namedtuple("Item", "t lens text n k", defaults=(None,))

LENS = {"g1": M, "g1_slave": S}
PHONE = re.compile(r"\] (g1|g1_slave)/vcentral\w*: +([\d.]+)s  (.*)")
UART = re.compile(r"\] (g1|g1_slave)/uart(0|_net): \[host:[^|]*\|virt: *([\d.]+)(ks|s|ms|us)[^]]*\] ?(.*)")
UNIT = {"ks": 1000.0, "s": 1.0, "ms": 0.001, "us": 0.000001}   # Renode prints the virtual time in the unit that fits
BOOT = "*** Booting"                                # the first line of a Zephyr image
BOOTS = ("boot app", "boot net")                    # the items every lens gives at power-on
FATAL = ("ASSERTION FAIL", "FATAL ERROR", "watch dog reset", "bt hci cmd send sync failed", "Kernel panic",
         "HardFault")
LOGGED = ("sync to slave failed", "sync to slave exceed")   # firmware log lines kept as "log <text>" items
SETTLE = 0.05                                       # a panel content shorter than this is a redraw in progress
GAP = 1.0                                           # an audio stream has ended after this long without a packet
BLANK = "63a497994321"                              # SHA-1 of an all-zero frame buffer: a dark panel
# The items the trace makes itself, as shapes ({0}: a value): what the text of an assert can start with besides
# the protocol's own items and the phone's lines (protocol.Protocol.begins()).
KINDS = BOOTS + ("audio start", "audio end", "frame {0}", "?{0}") + tuple(
    "fatal: " + text for text in FATAL) + tuple("log " + text for text in LOGGED)
WIDTH, HEIGHT = 640, 480                            # the panel; its frame buffer has 4 bits a pixel
FB_BYTES = WIDTH * HEIGHT // 2


class Unread(Exception):
    """A line the trace is made from does not read as the engine expects: the simulator's wording changed."""


def lit(rows, supplies):
    """The panel contents as seen, from the frame buffer's rows (t, sha12) and the supplies' (t, on): the frame
    buffer while the panel is on, BLANK while it is off. A panel is on until a look finds it off."""
    if not supplies:
        return rows
    out, i, j, sha, on = [], 0, 0, None, True
    for t in sorted({t for t, _ in rows} | {t for t, _ in supplies}):
        while i < len(rows) and rows[i][0] <= t:
            sha, i = rows[i][1], i + 1
        while j < len(supplies) and supplies[j][0] <= t:
            on, j = supplies[j][1], j + 1
        shown = sha if on else BLANK
        if shown is not None and (not out or out[-1][1] != shown):
            out.append((t, shown))
    return out


class Tail:
    """A text file another process appends to, read a piece at a time: lines() gives the complete lines that
    have come since the last call."""

    def __init__(self, path):
        self.path = path
        self.pos = 0                                # how far the file has been read
        self.rest = b""

    def lines(self):
        if not os.path.isfile(self.path):
            return []
        with open(self.path, "rb") as f:
            f.seek(self.pos)
            data = self.rest + f.read()
            self.pos = f.tell()
        lines = data.split(b"\n")
        self.rest = lines.pop()                     # an incomplete last line waits for the rest
        return [line.decode("utf-8", errors="replace").rstrip("\r") for line in lines]


class LiveTrace:
    """The trace of a run in progress. After each step of the emulation read(now) takes in what the simulator
    has written since (its log, the frame recordings) and closes the audio streams that have ended; power()
    and add() take what the driver itself finds (a panel's supplies, a query's output); items(now) is the trace
    so far, without side effects. item: the protocol's reading of a line of the virtual phone, as the item it
    is, "audio" for a microphone packet, or None to drop it."""

    def __init__(self, out, item):
        self.item = item
        self.fixed = []                             # (t, lens, text) of every item but the panel contents
        self.audio = {}                             # {lens: [first, last packet]} of the streams that run
        self.log = Tail(os.path.join(out, "log.txt"))
        self.frames = {lens: Tail(os.path.join(out, lens + ".frames")) for lens in (M, S)}
        self.rows = {M: [], S: []}                  # (t, sha12) of every change of each frame buffer
        self.supplies = {M: [], S: []}              # (t, on) of every change of each panel's supplies

    def read(self, now):
        self.feed(self.log.lines())
        for lens in (M, S):
            rows = self.rows[lens]
            for line in self.frames[lens].lines():
                p = line.split()                    # "<t> <n> <x> <y> <w> <h> <sha> <data>", or "<t> <n> same"
                if len(p) == 8 and (not rows or p[6][:12] != rows[-1][1]):
                    rows.append((float(p[0]), p[6][:12]))
        for lens, (_, last) in list(self.audio.items()):
            if now - last >= GAP:
                self.fixed.append((last, lens, "audio end"))
                del self.audio[lens]

    def feed(self, lines):
        for line in lines:
            g = PHONE.search(line) if "/vcentral" in line else None
            if g:
                lens, t, x = LENS[g[1]], float(g[2]), self.item(g[3].strip())
                if x == "audio":
                    self.sound(lens, t)
                elif x:
                    self.fixed.append((t, lens, x))
            g = UART.search(line) if "/uart" in line else None
            if g:
                lens, t = LENS[g[1]], float(g[3]) * UNIT[g[4]]
                if BOOT in g[5]:
                    self.fixed.append((t, lens, "boot " + ("app" if g[2] == "0" else "net")))
                self.fixed += [(t, lens, "fatal: " + p) for p in FATAL if p in g[5]]
                self.fixed += [(t, lens, "log " + p) for p in LOGGED if p in g[5]]

    def sound(self, lens, t):
        """A microphone packet at t: it goes on with the lens's stream, or starts one ("audio start") when
        there is none or the last packet is GAP or more ago (that stream ended there)."""
        stream = self.audio.get(lens)
        if stream and t - stream[1] < GAP:
            stream[1] = t
            return
        if stream:
            self.fixed.append((stream[1], lens, "audio end"))
        self.fixed.append((t, lens, "audio start"))
        self.audio[lens] = [t, t]

    def power(self, lens, t, on):
        self.supplies[lens].append((t, on))

    def running(self):
        """{lens: the time of the last packet} of the audio streams that have not ended."""
        return {lens: last for lens, (_, last) in self.audio.items()}

    def came(self):
        """How many items have come so far, panel contents apart: the k of the next one."""
        return len(self.fixed)

    def add(self, t, lens, text):
        self.fixed.append((t, lens, text))

    def items(self, now, final=False):
        """[Item] up to now. A panel content is in it once it has stayed up for SETTLE; final (the run is
        over): the contents up at the end are in it whatever their age, and the audio streams still open
        end at their last packet."""
        found = list(self.fixed)
        if final:
            found += [(last, lens, "audio end") for lens, (_, last) in self.audio.items()]
        came = len(found)
        for lens in (M, S):
            rows, last = lit(self.rows[lens], self.supplies[lens]) + [(now, None)], None
            for n, ((t, sha), (end, _)) in enumerate(zip(rows, rows[1:])):
                if (end - t >= SETTLE or final and n == len(rows) - 2) and sha != last:
                    found.append((t, lens, "frame " + sha))
                    last = sha
        order = sorted(range(len(found)), key=lambda i: (found[i][0], i))
        return [Item(found[i][0], found[i][1], found[i][2], n, i if i < came else None) for n, i in enumerate(order)]


def chunk(kind, data):
    """A chunk of a PNG file."""
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def write_png(path, fb):
    """A frame buffer (4 bits a pixel, WIDTH x HEIGHT) as a grey PNG."""
    row = WIDTH // 2
    lines = b"".join(b"\0" + fb[i:i + row] for i in range(0, len(fb), row))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 4, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(lines)) + chunk(b"IEND", b""))


def read_png(path):
    """The frame buffer of a PNG that write_png() wrote."""
    with open(path, "rb") as f:
        data = f.read()
    at, packed = 8, b""
    while at < len(data):
        size, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IHDR" and data[at + 8:at + 21] != struct.pack(">IIBBBBB", WIDTH, HEIGHT, 4, 0, 0, 0, 0):
            raise ValueError("%s is not a %dx%d grey picture of 4 bits a pixel" % (path, WIDTH, HEIGHT))
        if kind == b"IDAT":
            packed += data[at + 8:at + 8 + size]
        at += 12 + size
    lines, row = zlib.decompress(packed), WIDTH // 2 + 1
    return b"".join(lines[i + 1:i + row] for i in range(0, len(lines), row))


def sha12(fb):
    """The name of a panel content in a trace: the first 12 hex digits of its frame buffer's SHA-1."""
    return hashlib.sha1(fb).hexdigest()[:12]


def png(out, items):
    """A PNG of each panel content among the items, into out: <lens>_<sha12>.png."""
    for lens in (M, S):
        want = {i.text[6:] for i in items if i.lens == lens and i.text.startswith("frame ")}
        path = os.path.join(out, lens + ".frames")
        if want and os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    p = line.split()
                    if len(p) == 8 and p[6][:12] in want:
                        want.discard(p[6][:12])
                        write_png(os.path.join(out, "%s_%s.png" % (lens, p[6][:12])),
                                  zlib.decompress(base64.b64decode(p[7]), -15))
        if BLANK in want:                       # dark only because the panel was off: never written
            write_png(os.path.join(out, "%s_%s.png" % (lens, BLANK)), bytes(FB_BYTES))
