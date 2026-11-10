"""What every module of the engine shares: the error of a mistake in a test file, a choice of values, how hex
bytes are read and how a time is written. It imports nothing of the engine, so every other module
can use it.
"""
import re


class RefError(Exception):
    """A mistake in a test file (a .ref): a constructor refuses its arguments, a label is not there. The loader
    reports it with the file and line."""


class OneOf:
    """any_of(a, b, ...) of values: one of them (patterns of items)."""

    def __init__(self, values):
        self.values = tuple(values)

    def __iter__(self):
        return iter(self.values)

    def __len__(self):
        return len(self.values)

    def __eq__(self, other):
        return isinstance(other, OneOf) and other.values == self.values

    def __hash__(self):
        return hash(self.values)

    def __str__(self):
        return "|".join(map(str, self.values))

    def __repr__(self):
        return "any_of(%s)" % ", ".join(map(repr, self.values))


def fmt(t):
    """Seconds as written in a report: to the millisecond, without trailing zeros."""
    text = ("%.3f" % t).rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def hexbytes(text, where, empty=False):
    """The bytes of 'AA BB CC': two hex digits a byte, spaces between them (the one reading of hex bytes in a
    test file or a packet buffer). empty: '' is no bytes, else it is a mistake."""
    if not isinstance(text, str):
        raise RefError("%s: hex bytes are written as text, '39 05 00 02 01', not %r" % (where, text))
    tokens = text.split()
    bad = [t for t in tokens if not re.fullmatch(r"[0-9A-Fa-f]{2}", t)]
    if bad or not tokens and not empty:
        raise RefError("%s: %s; hex bytes are two digits each with spaces between them, '39 05 00 02 01'" % (
            where, "%r is not a hex byte" % bad[0] if bad else "no bytes"))
    return bytes(int(t, 16) for t in tokens)
