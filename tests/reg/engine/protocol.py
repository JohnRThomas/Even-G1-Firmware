"""The protocol of a suite: what the simulator's virtual phone speaks, and so what a test of the suite sends and
what it asserts of the answers. The engine is the same for every suite; this is everything it asks of one.

A suite's suite.py defines a subclass of Protocol and names an instance of it PROTOCOL. The loader takes the
suite's vocabulary from it (NAMES), the driver its set-up (PH, SETUP), the trace its reading of the phone's
log (item()), and the run what must be accounted for (needs_assert()) and how to write an item out (show()).

The phone's model writes its lines for people. So that a change of its wording cannot turn an assert into one
that never matches, the protocol lists every line the model can write, as the format strings of the model's
source (SAYS; a unit test holds the list against tools/simulator/models): a line that is none of them is an
error of the run (trace.Unread), and a text in a test file that no item can start with is an error of the file.
"""
import re

from . import statements, trace

VALUE = re.compile(r"\{\d+[^}]*\}")                # a value in a C# format string: {0}, {1:X2}, {0,8:F2}


def shape(form):
    """A format string as its parts in order: the fixed texts, and None where a value stands."""
    parts, at = [], 0
    for g in VALUE.finditer(form):
        if g.start() > at:
            parts.append(form[at:g.start()])
        if not parts or parts[-1] is not None:
            parts.append(None)
        at = g.end()
    return parts + ([form[at:]] if at < len(form) else [])


def begins(parts, text, first=True):
    """Whether a line of that shape can start with the text, as an assert reads it (the text ends where a word
    of the line ends: asserts.Starts). A value can be anything, but a line that starts with a value is only
    taken to start with the text if the text reaches the fixed part after it."""
    if not text:
        return True
    if not parts:
        return False
    if parts[0] is None:
        if len(parts) == 1:
            return not first
        return any(text.startswith(parts[1][:2], n) and begins(parts[1:], text[n:], False)
                   for n in range(1 if first else 0, len(text))) or not first
    if text.startswith(parts[0]):
        return begins(parts[1:], text[len(parts[0]):], False)
    rest = parts[0][len(text):]
    return parts[0].startswith(text) and not (text[-1].isalnum() and rest[:1].isalnum())


class Protocol:
    PH = None                       # the virtual phone's model in a monitor command, "sysbus.vcentral "
    SETUP = ()                      # monitor commands of every session, before the run ("slave: ..." on the slave)
    WIDTH = 110                     # characters of a trace item in a failure report
    ACCOUNTING = "accounting"       # what the accounting failure is called: what must be asserted
    NAMES = {}                      # what a test file can name besides the engine's own vocabulary
    REPLACES = ()                   # the names of the engine's vocabulary that NAMES gives another meaning
    RUNNER = statements.Runner      # the class that runs a test (a protocol may poll its phone in step())
    SAYS = ()                       # every line the phone's model writes, as the format strings of its source
    MAKES = ()                      # the items item() makes of such lines instead of keeping them, as shapes

    def __init__(self):
        self.lines = [re.compile("".join(".*" if p is None else re.escape(p) for p in shape(form)))
                      for form in self.SAYS]
        self.shapes = [shape(form) for form in trace.KINDS + tuple(self.MAKES) + tuple(self.SAYS)]

    def known(self, line):
        """Whether the phone's model can have written the line (SAYS)."""
        return any(r.fullmatch(line) for r in self.lines)

    def begins(self, text):
        """Whether an item of a trace can start with the text: one of the engine's (trace.KINDS), of item()'s
        (MAKES) or a line of the phone's (SAYS)."""
        return any(begins(parts, text) for parts in self.shapes)

    def item(self, line):
        """A line of the virtual phone's log as the trace item it is: its text, "audio" for a microphone
        packet, or None to leave it out. Raises trace.Unread for a line that is not one of SAYS."""
        raise NotImplementedError

    def needs_assert(self, text):
        """Whether a trace item must be matched by an assert or ignored (the accounting)."""
        raise NotImplementedError

    def show(self, text):
        """A trace item as a person reads it, in trace.txt and in a failure report."""
        return text

    def write(self, data):
        """The monitor command that makes the phone write bytes as they are (an input given as bytes)."""
        return self.PH + 'SendPacket "%s"' % data.hex().upper()
