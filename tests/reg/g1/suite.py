"""The G1 app's protocol (suite g1): the simulator's virtual phone sysbus.vcentral, the packets it writes and
the packets the glasses answer with.

In a test file:

    send('39 05 00 02 01')       a packet the phone writes: its bytes in hex, lengths and sequence numbers
                                 included. The packets of a bulk transfer come from a buffer: buf('NAME', 3),
                                 buf('NAME', 2, 24) (statements.py)
    phone_on(os=1, hb=None)      the phone switches on and sends the app's first two packets: 4D FB (the MTU it
                                 negotiated, 251) and 2C <os> (the battery query, which also tells the glasses
                                 the phone's system: 1 Android, 2 iOS); hb: heartbeat seconds
    heartbeat(hb)                the monitor command that sets the heartbeat; PHONE_ON, PHONE_OFF the switch
    recv(lens, "F5 1E")          a packet from the glasses that starts with those bytes (".." a wildcard byte:
                                 "2B 69 .. 06"); recv(lens, "'net build time") a text packet that starts with
                                 that text; count= as event()
    quiet(), never(), ignore()   take a hex pattern for packets too
    link_up(within=5)            both lenses report the link after the first phone_on: the file must have a
                                 line labelled "phone_on"

In the trace: "rx <hex>" a packet the phone received, masked (packet()), "tx <hex>" one it wrote, its link events
as it logs them, and "audio start" / "audio end" for a stream of 0xF1 packets. The battery events F5 0A are left
out: the fuel gauge runs on its own clock.

A packet is written as text, "rx 'base: 0' (20 B)", only when it is one of the firmware's text replies (TEXT:
the answers to the '#' debug commands, which have no command byte, and the two serial numbers); every other
packet is hex whatever its bytes are, so that "32 6D" matches the display settings also when all of their
bytes happen to be printable.

Accounting: every "rx F5" event, boot, fatal and audio stream must be asserted.
"""
import re

from engine import asserts, statements
from engine.asserts import Event, is_hex
from engine.base import OneOf, RefError
from engine.inputs import B
from engine.protocol import Protocol
from engine.trace import Unread

PH = "sysbus.vcentral "
PHONE_ON, PHONE_OFF = PH + "Enabled true", PH + "Enabled false"

# Bytes [from, to) that differ between builds or runs: the times and CRCs of 0x21's audio records, the battery
# levels in 0x2C, the uptime in 0x37.
MASKS = {b"\x21": (7, None), b"\x2C\x66": (2, 4), b"\x37\x37": (2, 6)}
# What a text reply starts with: the '#' debug commands' answers (src/ble/ble_proto.c: "#o" and "#x" give
# "base: <n>", "#v" the build times, "#b" the battery line) and the serial numbers (commands 0x33, 0x34).
TEXT = (b"base: ", b"net build time", b"B:", b"\x33", b"\x34")
ESCAPES = {"\\": "\\\\", "'": "\\'", "\r": "\\r", "\n": "\\n", "\t": "\\t"}


def quoted(text):
    """A text packet as the trace writes it: in single quotes, with \\\\ \\' \\r \\n \\t and \\xNN escaped."""
    return "'%s'" % "".join(ESCAPES.get(c) or (c if 32 <= ord(c) < 127 else "\\x%02x" % ord(c)) for c in text)


def packet(d):
    """A received packet, masked: hex bytes, or for a text reply (TEXT) its text, without the build dates and the
    battery level."""
    if d.startswith(TEXT):
        text = re.sub(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d", "<date>", quoted(d.rstrip(b"\0").decode("latin-1")))
        return re.sub(r"B:\d+", "B:..", text) + " (%d B)" % len(d)
    x = d.hex(" ").upper().split()
    for prefix, (a, b) in MASKS.items():
        if d.startswith(prefix):
            x[a:b] = [".."] * len(x[a:b])
    return " ".join(x)


def recv(lens, pattern, count=1):
    patterns = pattern.values if isinstance(pattern, OneOf) else (pattern,)
    for p in patterns:
        if not isinstance(p, str) or not (is_hex(p) or p[:1] == "'" and len(p) > 1):
            raise RefError("recv() takes the hex bytes a packet starts with (\"2B 69 .. 06\") or the start of a "
                           "text packet after a single quote (\"'net build time\"), not %r; the phone's link "
                           "events, boots and audio are event()" % (p,))
    return Event(lens, OneOf(p if is_hex(p) else "rx " + p for p in patterns), count, "recv",
                 "recv(%s, %s%s)" % (lens, pattern, asserts.count_text(count)))


def link_up(within=5):
    """The app's expectation once it connects: both lenses report the link (F5 11) and the master stops failing
    to sync the slave after the window. Counted from the line labelled "phone_on"."""
    return asserts.together(asserts.after(("phone_on", 1), recv(B, "F5 11"), within=within),
                            asserts.never("log sync to slave failed", after=(("phone_on", 1), within)))


def heartbeat(hb):
    return PH + "HeartbeatSeconds %g" % asserts.seconds(hb, "heartbeat()")


def phone_on(os=1, hb=None):
    """os: the phone's system as the app tells the glasses in its battery query, 1 Android, 2 iOS."""
    if os not in (1, 2):
        raise RefError("phone_on(os=): 1 (Android) or 2 (iOS), not %r" % (os,))
    return ([heartbeat(hb)] if hb is not None else []) + [PHONE_ON, send("4D FB"), bytes((0x2C, os))]


def send(text):
    """The bytes of a packet written as hex."""
    return statements.hexbytes(text, "send()")


class G1(Protocol):
    PH = PH
    ACCOUNTING = "accounting: every F5 event, boot, fatal and audio stream is asserted"
    NAMES = {"recv": recv, "link_up": link_up, "send": send, "phone_on": phone_on, "heartbeat": heartbeat,
             "PH": PH, "PHONE_ON": PHONE_ON, "PHONE_OFF": PHONE_OFF}
    # tools/simulator/models/BLE_VirtualCentral.cs, every Say():
    SAYS = ("link dropped: the lens was reset", "connected to {0}", "disconnected: {0}",
            "pairing failed: the lens refused to encrypt with the new key (0x{0:X2})",
            "the lens has no key for this phone (0x{0:X2}): bond forgotten", "link encrypted with the bond's key",
            "ATT MTU {0}", "{0}: write 0x{1:X4}, notify 0x{2:X4}, notifications on", "rx {0}", "tx {0}",
            "pairing: started", "pairing failed: {0}", "pairing failed: the lens gave reason 0x{0:X2}",
            "pairing: complete, link encrypted, bonded")
    MAKES = ("rx {0}",)

    def item(self, line):
        if not line.startswith("rx "):
            if not self.known(line):
                raise Unread("a line of the phone that is not in SAYS (g1/suite.py): %r" % line)
            return line                         # "tx <hex>", and the phone's link events as it words them
        if not re.fullmatch(r"rx( [0-9A-F]{2})+", line):
            raise Unread("a packet the phone received is not written as hex bytes: %r" % line)
        if line.startswith("rx F1 "):
            return "audio"
        if line.startswith("rx F5 0A "):
            return None
        return "rx " + packet(bytes.fromhex(line[3:]))

    def needs_assert(self, text):
        return text.startswith(("rx F5 ", "boot", "fatal", "audio"))


PROTOCOL = G1()
