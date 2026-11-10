"""Asserts: what a run's trace must contain, evaluated order-free inside causal windows.

An assert is evaluated against the whole trace (trace.py) within a window [start, end] of emulated seconds: the
window of the line it stands in (statements.py), the one after()/each() sets from an input anchor (a labelled
action or a mark), or the whole run when it stands alone. Asserts inside one window hold in any order;
sequence() is the only ordered form.

What the glasses send to the phone is asserted with recv() and the other forms of the suite's protocol (its
suite.py). Everything else in the trace is asserted with event(), quiet(), never() or ignore(), by the text it
starts with: the virtual phone's link events ("pairing: complete", "disconnected"), "boot", "fatal",
"audio start", "log sync to slave failed". The text must end where a word of the item ends ("ack 09 1" is not
the start of "ack 09 12"). any_of(...) takes several patterns.

Lenses: M, S, B (both, the assert must hold on each) and A (the lens of the window's anchor).

What each form says about time:

    recv(), event() ...     count=N: exactly N matching items in the window (default 1); count=at_least(N).
                            In a line that goes on as soon as its asserts hold the count is taken again when
                            the run is over, over the line's whole window (statements.py).
    quiet(), never(),       nothing in the whole window: known only once the window has closed, so a line
    no_audio(), unchanged   waits for its end
    changes, shows(x)       the panel content changes / x comes up at some time in the window; the content
                            that was already up when the window opened does not count for shows(x)
    blank, nonblank,        the state of the panel at the end of the window, where it must have held for HOLD
    settles_to(x),          seconds: in a line, as soon as it has held that long after the input; after an
    same_as(), same_on_both anchor or alone, in the last HOLD seconds of the window. A content that was up
    differs_from()          for a moment does not pass for one that stays
    audio()                 a stream that starts in the window and lasts as long as asked
    state()                 every output of that query in the window

Accounting: every item of the trace that the protocol says needs an assert (what the glasses send on their own,
boots, fatals, audio streams) must be matched by an assert, in the window that assert has, or ignored with
ignore(pattern); the run fails otherwise. Boots in the first second are the power-on.

known_failure("reason", assert, seeds=(...)) marks one assert as known to fail: see KnownFailure.
"""
import bisect
import re

from .base import OneOf, RefError, fmt
from .inputs import A, B, M, S
from .trace import BLANK, BOOTS, SETTLE

DEFAULT_WITHIN = 3.0
HOLD = 0.3                                           # seconds a panel state must have held to be asserted
EPS = 1e-9                                           # times are sums of steps: equal within this
LIMIT = 12                                           # items shown of a window on failure


# ------------------------------------------------------------------------------------------------- arguments

class AtLeast:
    def __init__(self, n):
        self.n = n

    def __repr__(self):
        return "at_least(%d)" % self.n


def whole(n, what, least=0):
    """n when it is a whole number from least on, else a RefError naming what it is."""
    if isinstance(n, bool) or not isinstance(n, int) or n < least:
        raise RefError("%s is a whole number from %d on, not %r" % (what, least, n))
    return n


def seconds(x, what, least=0.0):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or x < least or x != x:
        raise RefError("%s is a number of seconds%s, not %r" % (what, "" if least <= 0 else " from %g on" % least, x))
    return float(x)


def at_least(n):
    return AtLeast(whole(n, "at_least()", 1))


def count_of(count, what):
    if isinstance(count, AtLeast):
        return count
    return whole(count, "%s: count=" % what)


def enough(count, n):
    return n >= count.n if isinstance(count, AtLeast) else n == count


def wanted(count):
    return "at least %d" % count.n if isinstance(count, AtLeast) else "%d" % count


def count_text(count):
    """The count as an argument in an assert's text: nothing for the default, exactly one."""
    return "" if not isinstance(count, AtLeast) and count == 1 else ", count=%r" % (count,)


def lens_of(lens, what, anchor=True):
    names = (M, S, B, A) if anchor else (M, S, B)
    if isinstance(lens, str) and lens in names:
        return lens
    raise RefError("%s takes a lens (M, S%s), not %r" % (what, ", B or A" if anchor else " or B", lens))


def anchor_of(anchor, what):
    """An anchor as written: seconds; "label"; ("label", n) its nth occurrence from 1; ("label", lens);
    ("label", lens, n)."""
    if isinstance(anchor, (int, float)) and not isinstance(anchor, bool):
        return seconds(anchor, what)
    if isinstance(anchor, str) and anchor:
        return anchor
    if isinstance(anchor, tuple) and 2 <= len(anchor) <= 3 and isinstance(anchor[0], str) and anchor[0]:
        rest = anchor[1:]
        if len(rest) == 1 and (rest[0] in (M, S, B) or isinstance(rest[0], int) and not isinstance(rest[0], bool)
                               and rest[0] >= 1):
            return anchor
        if len(rest) == 2 and rest[0] in (M, S, B) and isinstance(rest[1], int) and rest[1] >= 1:
            return anchor
    raise RefError('%s takes seconds, a label, ("label", n), ("label", lens) or ("label", lens, n), not %r'
                   % (what, anchor))


def after_of(after, what):
    """An after= argument: seconds, or (anchor, seconds after the anchor's last input)."""
    if isinstance(after, tuple):
        if len(after) != 2:
            raise RefError("%s: after= is seconds or (anchor, seconds), not %r" % (what, after))
        return (anchor_of(after[0], what + ": after="), seconds(after[1], what + ": after=", float("-inf")))
    return seconds(after, what + ": after=")


def label_of(anchor):
    """The label an anchor names, or None for a time."""
    if isinstance(anchor, tuple):
        return anchor[0]
    return anchor if isinstance(anchor, str) else None


# -------------------------------------------------------------------------------------------------- patterns

class Pattern:
    """What an item of the trace must look like; text: as it is written in a test file."""
    text = "?"

    def match(self, item_text):
        raise NotImplementedError

    def __str__(self):
        return self.text


class Starts(Pattern):
    """The item starts with the text, which ends where a word of the item ends."""

    def __init__(self, text):
        self.text, self.cut = text, text[-1:].isalnum()

    def match(self, x):
        n = len(self.text)
        return x.startswith(self.text) and not (self.cut and len(x) > n and x[n].isalnum())


class Bytes(Pattern):
    """A packet the phone received ("rx AA BB ..") that starts with the hex bytes; ".." is any byte."""

    def __init__(self, text):
        self.text = text
        tokens = [r"(?:[0-9A-F]{2}|\.\.)" if t == ".." else re.escape(t.upper()) for t in text.split()]
        self.regex = re.compile("rx " + " ".join(tokens) + "(?: |$)")

    def match(self, x):
        return self.regex.match(x) is not None


class Either(Pattern):
    def __init__(self, patterns):
        self.patterns = list(patterns)
        self.text = "|".join(p.text for p in self.patterns)

    def match(self, x):
        return any(p.match(x) for p in self.patterns)


def is_hex(text):
    tokens = text.split() if isinstance(text, str) else []
    return bool(tokens) and all(re.fullmatch(r"[0-9A-Fa-f]{2}|\.\.", t) for t in tokens)


def pattern_of(x, what):
    """The Pattern of an argument: the text an item starts with, hex bytes a packet starts with, any_of() of
    those, or a Pattern of the protocol's."""
    if isinstance(x, Pattern):
        return x
    if isinstance(x, OneOf) and len(x):
        return Either(pattern_of(v, what) for v in x)
    if isinstance(x, str) and x.strip():
        return Bytes(x) if is_hex(x) else Starts(x)
    raise RefError("%s takes the text an item starts with, not %r" % (what, x))


# --------------------------------------------------------------------------------------------------- context

class Context:
    """One look at a run: the trace up to now, the anchors, the named frames, the suite's protocol and the
    seed. final: the run is over (the trace is complete). strict: an assert that cannot be evaluated (a
    label the run has not reached) raises its RefError instead of failing (violated()). running: {lens: the
    time of the last packet} of the audio streams that have no "audio end" yet."""

    def __init__(self, items, anchors, now, proto, frames=None, seed=None, final=False, strict=False,
                 running=None):
        self.items = items
        self.anchors = anchors
        self.now = now
        self.proto = proto
        self.frames = frames or {}             # {name: sha12}, the suite's frames.txt
        self.seed = seed
        self.final = final
        self.strict = strict
        self.running = running or {}
        self.seen = now if final else now - SETTLE      # up to where the panel contents are known
        self.of = {M: [], S: []}
        self.shown = {M: ([], []), S: ([], [])}         # the times and the contents of each panel's changes
        for i in items:
            self.of[i.lens].append(i)
            if i.text.startswith("frame "):
                self.shown[i.lens][0].append(i.t)
                self.shown[i.lens][1].append(i.text[6:])
        self.times = {lens: [i.t for i in found] for lens, found in self.of.items()}

    def strictly(self):
        return Context(self.items, self.anchors, self.now, self.proto, self.frames, self.seed, self.final, True,
                       self.running)

    def lenses(self, lens, window):
        if lens == A:
            lens = window.lens
            if lens is None:
                raise RefError("lens A outside an anchored window")
        return (M, S) if lens == B else (lens,)

    def between(self, lens, start, end, pattern=None):
        """The lens's items from start to end, both included, that match the pattern."""
        times, found = self.times[lens], self.of[lens]
        found = found[bisect.bisect_left(times, start):bisect.bisect_right(times, end)]
        return found if pattern is None else [i for i in found if pattern.match(i.text)]

    def inside(self, lens, window, pattern=None, start=None):
        """The lens's items in the window that match the pattern; start: only from that time on. An item on the
        very second the window opens or closes is in it by when it came (Window.since, Window.until)."""
        found = self.between(lens, window.start if start is None else max(start, window.start), window.end, pattern)
        if window.since is not None:
            found = [i for i in found if i.t > window.start or i.k is None or i.k >= window.since]
        if window.until is not None:
            found = [i for i in found if i.t < window.end or i.k is None or i.k < window.until]
        return found

    def content_at(self, lens, t):
        """The panel content up at t (None: none yet)."""
        times, shas = self.shown[lens]
        n = bisect.bisect_right(times, t)
        return shas[n - 1] if n else None

    def contents(self, lens, start, end):
        """The contents up at any time from start to end, in order: the one at start, then each change."""
        times, shas = self.shown[lens]
        return [self.content_at(lens, start)] + shas[bisect.bisect_right(times, start):bisect.bisect_right(times, end)]

    def sha(self, name):
        return sha_of(name, self.frames)

    def resolve(self, anchor):
        """An anchor (anchor_of()) -> (first, last, lens, since): the time its input began, the time its last
        part was sent, the lens it went to, and how many items had come to the trace before it."""
        if isinstance(anchor, (int, float)):
            return float(anchor), float(anchor), None, None
        label, lens, n = (anchor, None, None) if isinstance(anchor, str) else (anchor + (None, None))[:3]
        if isinstance(lens, int):
            lens, n = None, lens
        found = self.anchors.get(label)
        if not found:
            raise RefError("no input labelled %r; labels: %s" % (label, ", ".join(sorted(self.anchors))))
        if lens is not None:
            found = [o for o in found if o[2] in (lens, B)]
            if not found:
                raise RefError("no input labelled %r on %s" % (label, lens))
        if n is None:
            if len(found) > 1:
                raise RefError("%r happens %d times (%s): give (%r, n), n from 1, or use each()" % (
                    label, len(found), ", ".join("%s %s" % (fmt(o[0]), o[2]) for o in found), label))
            n = 1
        if not 1 <= n <= len(found):
            raise RefError("%r happens %d times, not %d" % (label, len(found), n))
        return found[n - 1]

    def offset(self, after):
        """The time of an after= argument (after_of())."""
        return self.resolve(after[0])[1] + after[1] if isinstance(after, tuple) else after

    def covers(self, seeds):
        """Whether a known failure under those seeds (None: every seed) is expected in this run."""
        return seeds is None or self.seed in seeds

    def lines(self, items, limit=LIMIT):
        """Trace items for a failure report, the first limit of them."""
        width = self.proto.WIDTH
        out = ["      %8s %-6s %s" % (fmt(i.t), i.lens, self.proto.show(i.text)[:width]) for i in items[:limit]]
        return out + (["      ... %d more" % (len(items) - limit)] if len(items) > limit else [])

    def describe(self, lens, window):
        """The items of the lens in the window, for a failure report."""
        return self.lines(self.inside(lens, window)) or ["      (nothing from %s in %s)" % (lens, window)]


def sha_of(name, frames):
    """The sha12 of a panel content written as its sha12 or by its name (frames.txt; "blank" is built in)."""
    if name == "blank":
        return BLANK
    if isinstance(name, str) and re.fullmatch(r"[0-9a-f]{12}", name):
        return name
    if name not in frames:
        raise RefError("no frame named %r (frames.txt)" % (name,))
    return frames[name]


class Window:
    """Seconds start to end of a run, both included. lens: the lens of the anchor (for A). open: the window is
    still running (a look before a line's time is up, an after() that has not closed: close says when it
    will), so what speaks about the whole of it is not known yet. at: where a panel's state is judged, when
    that is not the end (the look a line went on at). since, until: how many items had come to the trace when
    the window opened and when the line after it began (Item.k): an item with the time of the start that was
    already there is not in the window, nor is one with the time of the end that came after it."""

    def __init__(self, start, end, lens=None, open=False, at=None, close=None, since=None, until=None):
        self.start = start
        self.end = end
        self.lens = lens
        self.open = open
        self.at = at
        self.close = end if close is None else close
        self.since = since
        self.until = until

    def __str__(self):
        return "%s..%s" % (fmt(self.start), fmt(self.end))


class Result:
    """What an assert found. what: the assert (or statement) the result is of, with its .text and .where.
    pending: not ok only because it is not known yet. known: the known failures that failed as expected,
    [(mark, its assert's Result)]; passed: the known-failure marks whose assert held."""

    def __init__(self, what, ok, detail=(), matched=(), pending=False, known=(), passed=()):
        self.what = what
        self.ok = ok
        self.detail = list(detail)
        self.matched = list(matched)
        self.pending = pending
        self.known = list(known)
        self.passed = list(passed)

    def under(self, heading):
        """The result with a line before its detail that says where it was found."""
        return Result(self.what, self.ok, [heading] + self.detail, self.matched, self.pending, self.known, self.passed)


def gathered(what, ok, results, detail=(), pending=False):
    """The Result of an assert made of others: their matched items and known failures with it."""
    return Result(what, ok, detail, [m for r in results for m in r.matched], pending,
                  [k for r in results for k in r.known], [p for r in results for p in r.passed])


# --------------------------------------------------------------------------------------------------- asserts

class Assert:
    text = "?"
    where = None                    # "g1/name.ref:12": the loader sets it

    def evaluate(self, ctx, window):
        raise NotImplementedError

    def inner(self):
        """The asserts this one is made of."""
        return ()

    def anchors(self):
        """The anchors this assert itself names (not those of its inner asserts)."""
        return ()

    def frames(self):
        """The panel contents this assert itself names, as written (a sha12 or a name of frames.txt)."""
        return ()

    def run(self, ctx, window):
        try:
            return self.evaluate(ctx, window)
        except RefError as e:
            if ctx.strict:
                raise
            return Result(self, False, ["      " + str(e)])

    def whole(self):
        """Whether the assert says something about the whole of its window, or about its end: it is known
        only once the window has closed (the run goes on until a stand-alone after() of such an assert has)."""
        return any(c.whole() for c in self.inner())

    def display(self):
        """Whether the assert looks at the panels (their contents are known SETTLE late)."""
        return any(c.display() for c in self.inner())

    def least(self, within):
        """The seconds a line that waits within seconds for the assert takes at least (the --times report):
        an assert about the whole window takes all of it, the state of a panel takes HOLD."""
        return max([c.least(within) for c in self.inner()], default=0.0)

    def settles(self):
        """Whether the assert's result over a window that has closed is final: nothing later in the run
        changes it."""
        return all(c.settles() for c in self.inner())

    def violated(self, ctx, end):
        """The failing Result when the assert, standing alone in a file, is violated for good by the time end of
        a run that stopped there (no later part of the run could have mended it), else None. ctx is strict: a
        RefError (a label the run has not reached) comes out of it and means "not known yet"."""
        return None

    def __str__(self):
        return self.text


class Event(Assert):
    """Items matching pattern from lens in the window: exactly count of them (a number) or at_least(n)."""

    def __init__(self, lens, pattern, count=1, kind="event", text=None):
        self.lens = lens_of(lens, kind + "()")
        self.kind = kind
        self.pattern = pattern_of(pattern, kind + "()")
        self.count = count_of(count, kind + "()")
        self.text = text or "%s(%s, %s%s)" % (kind, lens, self.pattern, count_text(self.count))

    def whole(self):
        return not isinstance(self.count, AtLeast)

    def least(self, within):
        return within if not isinstance(self.count, AtLeast) and self.count == 0 else 0.0

    def found(self, ctx, lens, window):
        return ctx.inside(lens, window, self.pattern)

    def explain(self, ctx, lens, window, got):
        """Lines for a failure report after the count line: what the window held."""
        return ["      the window held:"] + ctx.describe(lens, window)

    def evaluate(self, ctx, window):
        detail, matched, ok = [], [], True
        for lens in ctx.lenses(self.lens, window):
            got = self.found(ctx, lens, window)
            matched += got
            if not enough(self.count, len(got)):
                ok = False
                detail.append("      %s: %d match(es) in %s, wanted %s" % (lens, len(got), window, wanted(self.count)))
                detail += self.explain(ctx, lens, window, got)
        if ok and window.open and self.count == 0 and not isinstance(self.count, AtLeast):
            return Result(self, False, ["      nothing so far; the window is open until %s" % fmt(window.close)],
                          matched, pending=True)
        return Result(self, ok, detail, matched)


def stream_end(ctx, start):
    """The "audio end" item of the stream that starts with the item start, or None while it runs."""
    return next((e for e in ctx.between(start.lens, start.t, ctx.now) if e.text == "audio end"), None)


def stream_stop(ctx, start):
    """(the time the stream that starts with the item start has lasted to, its "audio end" item or None). A
    stream that still runs has lasted to its last packet so far: that is where it ends if no other comes, so
    a line that waits for a stream of some length does not go on a packet early."""
    end = stream_end(ctx, start)
    return (end.t if end else ctx.running.get(start.lens, ctx.now)), end


class Audio(Assert):
    """An audio stream starting in the window and lasting at least min_seconds (to the run's end if it never
    ends); with during=True, a stream active at some point in the window, whenever it started, of that length."""

    def __init__(self, lens, min_seconds=0, during=False):
        self.lens = lens_of(lens, "audio()")
        self.min = seconds(min_seconds, "audio()")
        self.during = bool(during)
        self.text = "audio(%s%s%s)" % (lens, ", %g s" % self.min if self.min else "", ", during" if during else "")

    def settles(self):
        return not self.min                  # a stream still running may yet reach its length

    def evaluate(self, ctx, window):
        detail, matched, ok = [], [], True
        for lens in ctx.lenses(self.lens, window):
            starts = [s for s in (ctx.between(lens, 0.0, window.end) if self.during else ctx.inside(lens, window))
                      if s.text == "audio start"]
            good = False
            for s in starts:
                stop, end = stream_stop(ctx, s)
                if stop - s.t >= self.min - EPS and (not self.during or stop >= window.start):
                    good = True
                    matched += [s] + ([end] if end else [])
            if not good:
                ok = False
                detail += ["      %s: no audio stream of %g s or more %s %s; the window held:" % (
                    lens, self.min, "active in" if self.during else "starting in", window)]
                detail += ctx.describe(lens, window)
        return Result(self, ok, detail, matched)


class NoAudio(Assert):
    """No audio stream is running at any time in the window (one that started before it counts), from after
    (a time, or (anchor, offset)) on."""

    def __init__(self, lens=B, after=0):
        self.lens = lens_of(lens, "no_audio()")
        self.after = after_of(after, "no_audio()")
        self.text = "no_audio(%s%s)" % (lens, "" if after == 0 else ", after=%s" % (after,))

    def anchors(self):
        return [self.after[0]] if isinstance(self.after, tuple) else []

    def whole(self):
        return True

    def least(self, within):
        return within

    def evaluate(self, ctx, window):
        start, detail = max(ctx.offset(self.after), window.start), []
        for lens in ctx.lenses(self.lens, window):
            for s in [i for i in ctx.between(lens, 0.0, window.end) if i.text == "audio start"]:
                end = stream_end(ctx, s)
                stop = end.t if end else ctx.now
                if stop >= start:
                    detail.append("      %s: an audio stream from %s to %s%s, in %s..%s" % (
                        lens, fmt(s.t), fmt(stop), "" if end else " (still running)", fmt(start), fmt(window.end)))
        if not detail and window.open:
            return Result(self, False, ["      none so far; the window is open until %s" % fmt(window.close)],
                          pending=True)
        return Result(self, not detail, detail)

    def violated(self, ctx, end):
        r = self.evaluate(ctx, Window(0.0, end))            # a stream that ran stays one that ran
        return None if r.ok else r


class Never(Assert):
    """No item matching pattern from after (a time, or (anchor, offset)) on."""

    def __init__(self, pattern, after=0, lens=B, text=None):
        self.pattern = pattern_of(pattern, "never()")
        self.after = after_of(after, "never()")
        self.lens = lens_of(lens, "never(): lens=")
        self.text = text or "never(%s%s)" % (self.pattern, "" if after == 0 else ", after=%s" % (after,))

    def anchors(self):
        return [self.after[0]] if isinstance(self.after, tuple) else []

    def whole(self):
        return True

    def least(self, within):
        return within

    def evaluate(self, ctx, window):
        start = max(ctx.offset(self.after), window.start)
        got = [i for lens in ctx.lenses(self.lens, window) for i in ctx.inside(lens, window, self.pattern, start)]
        if not got and window.open:
            return Result(self, False, ["      nothing so far; the window is open until %s" % fmt(window.close)],
                          pending=True)
        return Result(self, not got, ctx.lines(got), got)

    def violated(self, ctx, end):
        r = self.evaluate(ctx, Window(0.0, end))            # a hit stays a hit
        return None if r.ok else r


class Ignore(Assert):
    """Items matching pattern need no assert (accounting only)."""

    def __init__(self, pattern, lens=B, text=None):
        self.pattern = pattern_of(pattern, "ignore()")
        self.lens = lens_of(lens, "ignore(): lens=")
        self.text = text or "ignore(%s)" % self.pattern

    def evaluate(self, ctx, window):
        return Result(self, True, [], [i for lens in ctx.lenses(self.lens, window)
                                       for i in ctx.inside(lens, window, self.pattern)])


class State(Assert):
    """Every output of the query (a monitor command of check()) in the window equals the text, or contains it."""

    def __init__(self, query, equals=None, contains=None, lens=B):
        for name, value in (("the query", query), ("equals=", equals), ("contains=", contains)):
            if not isinstance(value, str) and not (value is None and name != "the query"):
                raise RefError("state(): %s is text, not %r" % (name, value))
        if (equals is None) == (contains is None):
            raise RefError("state() takes equals= or contains=, one of them")
        self.query = query.lstrip("?").strip()
        self.equals = equals
        self.contains = contains
        self.lens = lens_of(lens, "state(): lens=")
        self.text = "state(%s %s %r)" % (self.query, "=" if equals is not None else "~",
                                         equals if equals is not None else contains)

    def judge(self, lens, got, where):
        """(detail lines, empty when good) for the lens's outputs of the query."""
        if not got:
            return ["      %s: no query %r %s" % (lens, self.query, where)]
        detail = []
        for i in got:
            value = i.text.split(" = ", 1)[1]
            if value != self.equals if self.equals is not None else self.contains not in value:
                detail.append("      %s at %s: %s" % (lens, fmt(i.t), i.text[:150]))
        return detail

    def evaluate(self, ctx, window):
        detail, matched, prefix = [], [], "?%s = " % self.query
        for lens in ctx.lenses(self.lens, window):
            got = [i for i in ctx.inside(lens, window) if i.text.startswith(prefix)]
            detail += self.judge(lens, got, "in %s" % window)
            matched += got
        return Result(self, not detail, detail, matched)


# ---------------------------------------------------------------------------------------------------- panels

class Shown:
    """What a lens's panel showed in a window, for a predicate: during, the contents from the start of the
    window to at (where the state is judged); last, those of the HOLD seconds before at."""

    def __init__(self, ctx, lens, window, at):
        self.ctx = ctx
        self.lens = lens
        self.start = window.start
        self.at = at
        self.during = ctx.contents(lens, window.start, at)
        self.last = ctx.contents(lens, max(window.start, at - HOLD), at)

    def text(self):
        changes = self.during[1:LIMIT + 1]
        more = ["... %d more" % (len(self.during) - 1 - LIMIT)] if len(self.during) - 1 > LIMIT else []
        return "      %s: %s at %s, then %s, %s at %s" % (
            self.lens, self.during[0], fmt(self.start), ", ".join("%s" % f for f in changes + more) or "no change",
            self.last[-1], fmt(self.at))


class Predicate:
    """A predicate on a panel's content, for display(). state: about the content at the end of the window,
    which must have been so for HOLD seconds. whole: about the whole window. both: about the two panels."""
    state = whole = both = False
    label = None                    # the anchor the predicate names
    frame = None                    # the content it names

    def __init__(self, name, arg=None):
        self.name, self.arg = name, arg

    def holds(self, shown):
        raise NotImplementedError

    def __str__(self):
        return self.name + ("(%s)" % (self.arg,) if self.arg is not None else "")


def is_blank(content):
    return content in (None, BLANK)


class Changes(Predicate):
    def holds(self, shown):
        return any(f != shown.during[0] for f in shown.during[1:])


class Unchanged(Predicate):
    whole = True

    def holds(self, shown):
        return all(f == shown.during[0] for f in shown.during[1:])


class Shows(Predicate):
    def __init__(self, name, arg):
        super().__init__(name, arg)
        self.frame = arg

    def holds(self, shown):
        return shown.ctx.sha(self.arg) in shown.during[1:]


class Blank(Predicate):
    state = True

    def holds(self, shown):
        return all(is_blank(f) for f in shown.last)


class Nonblank(Predicate):
    state = True

    def holds(self, shown):
        return not any(is_blank(f) for f in shown.last)


class SettlesTo(Shows):
    state = True

    def holds(self, shown):
        return shown.last == [shown.ctx.sha(self.arg)]


class SameAs(Predicate):
    """The content at the end of the window is the one that was up at a labelled input or mark."""
    state = True

    def __init__(self, name, arg):
        super().__init__(name, arg)
        self.label = anchor_of(arg, name + "()")

    def then(self, shown):
        return shown.ctx.content_at(shown.lens, shown.ctx.resolve(self.label)[0])

    def holds(self, shown):
        return len(shown.last) == 1 and shown.last[0] == self.then(shown)


class DiffersFrom(SameAs):
    def holds(self, shown):
        return len(shown.last) == 1 and shown.last[0] != self.then(shown)


class SameOnBoth(Predicate):
    state = both = True

    def holds(self, shown):
        raise NotImplementedError       # Display compares the two panels


class Display(Assert):
    """A predicate on the panel content: changes, unchanged, shows(x), nonblank, blank, settles_to(x),
    same_on_both, differs_from("label") and same_as("label"); what each says about time is at the top of
    this file."""

    def __init__(self, lens, predicate):
        if not isinstance(predicate, Predicate):
            raise RefError("display() takes changes, unchanged, nonblank, blank, shows(x), settles_to(x), "
                           "same_on_both, differs_from(label) or same_as(label), not %r" % (predicate,))
        self.lens = lens_of(lens, "display()")
        self.pred = predicate
        if predicate.both and lens != B:
            raise RefError("display(%s, %s): %s is about both panels, write display(B, %s)" % (
                lens, predicate, predicate, predicate))
        self.text = "display(%s, %s)" % (lens, predicate)

    def anchors(self):
        return [self.pred.label] if self.pred.label is not None else []

    def frames(self):
        return [self.pred.frame] if self.pred.frame is not None else []

    def whole(self):
        return self.pred.whole or self.pred.state

    def display(self):
        return True

    def least(self, within):
        return within if self.pred.whole else min(within, HOLD + SETTLE) if self.pred.state else 0.0

    def evaluate(self, ctx, window):
        at = min(window.end, ctx.seen) if window.at is None else window.at
        if window.open and (self.pred.whole or self.pred.state and at - window.start < HOLD - EPS):
            return Result(self, False, ["      not known yet; the window is open until %s" % fmt(window.close)],
                          pending=True)
        if not window.open and window.at is None and at < window.end - EPS:
            return Result(self, False, ["      the panels are known up to %s only" % fmt(at)], pending=True)
        lenses = (M, S) if self.pred.both else ctx.lenses(self.lens, window)
        shown = [Shown(ctx, lens, window, at) for lens in lenses]
        if self.pred.both:
            bad = shown if not (len(shown[0].last) == len(shown[1].last) == 1
                                and shown[0].last == shown[1].last) else []
        else:
            bad = [s for s in shown if not self.pred.holds(s)]
        detail = [s.text() for s in bad]
        if bad and self.pred.label is not None:
            detail += ["      %s: %s at %r" % (s.lens, self.pred.then(s), self.pred.arg) for s in bad]
        if bad and self.pred.state and any(len(s.last) > 1 for s in bad):
            detail.append("      (the state is taken over the last %g s: %s)" % (
                HOLD, "; ".join("%s %s" % (s.lens, ", ".join("%s" % f for f in s.last)) for s in bad)))
        matched = [i for lens in lenses for i in ctx.between(lens, window.start, at, FRAME)]
        return Result(self, not bad, detail, matched)


FRAME = Starts("frame ")


# ---------------------------------------------------------------------------------------------------- groups

def all_asserts(asserts, what):
    asserts = list(asserts)
    for c in asserts:
        if not isinstance(c, Assert):
            raise RefError("%s takes asserts, not %r" % (what, c))
    if not asserts:
        raise RefError("%s takes at least one assert" % what)
    return asserts


def indented(results):
    return [d for r in results for d in ["    %s" % r.what.text] + r.detail]


class Group(Assert):
    kind = "?"

    def __init__(self, asserts):
        self.asserts = all_asserts(asserts, self.kind + "()")
        self.text = "%s(%s)" % (self.kind, ", ".join(c.text for c in self.asserts))

    def inner(self):
        return self.asserts


class Together(Group):
    """All of the asserts hold."""
    kind = "together"

    def evaluate(self, ctx, window):
        results = [c.run(ctx, window) for c in self.asserts]
        bad = [r for r in results if not r.ok]
        return gathered(self, not bad, results, indented(bad), any(r.pending for r in bad))

    def violated(self, ctx, end):
        bad = [r for r in (c.violated(ctx, end) for c in self.asserts) if r is not None]
        return gathered(self, False, bad, indented(bad)) if bad else None


class AnyOf(Group):
    """One of the asserts holds."""
    kind = "any_of"

    def least(self, within):
        return min(c.least(within) for c in self.asserts)

    def evaluate(self, ctx, window):
        results = [c.run(ctx, window) for c in self.asserts]
        good = next((r for r in results if r.ok), None)
        if good:
            return gathered(self, True, [good])
        return Result(self, False, indented(results), pending=any(r.pending for r in results))

    def violated(self, ctx, end):
        found = [c.violated(ctx, end) for c in self.asserts]
        if any(r is None for r in found):                   # every alternative must be lost
            return None
        return gathered(self, False, found, indented(found))


class Sequence(Assert):
    """Items in order: each step (a recv(), an event() ...) takes its first count matches that come after the
    ones the step before took, in the order of the trace. A step with an exact count has no further match
    before the next step's first one (the last step: before the window ends). A step on both lenses: the
    order holds on each lens by itself."""

    def __init__(self, steps):
        self.steps = list(steps)
        for step in self.steps:
            if not isinstance(step, Event):
                raise RefError("sequence() takes recv(), event() and the like, in the order they must come, not %r"
                               % (step,))
            if not isinstance(step.count, AtLeast) and step.count == 0:
                raise RefError("sequence(): a step cannot be count=0 (%s)" % step.text)
        if len(self.steps) < 2:
            raise RefError("sequence() takes two steps or more")
        self.text = "sequence(%s)" % ", ".join(c.text for c in self.steps)

    def inner(self):
        return self.steps

    def whole(self):
        return any(c.whole() for c in self.steps)

    def chain(self, ctx, window, one):
        """(detail lines, matched items) of the steps on the lens one (where a step says both)."""
        after, start, taken, matched = -1, window.start, [], []
        for step in self.steps:
            lens = ctx.lenses(step.lens, window)
            lens = one if len(lens) > 1 else lens[0]
            found = [i for i in ctx.inside(lens, window, step.pattern) if i.n > after]
            need = step.count.n if isinstance(step.count, AtLeast) else step.count
            if len(found) < need:
                detail = ["    %s: %d match(es) on %s after %s, wanted %s; the window held:" % (
                    step.text, len(found), lens, fmt(start), wanted(step.count))]
                return detail + ctx.describe(lens, window), matched
            taken.append(found)
            after, start = found[need - 1].n, found[need - 1].t
        for n, (step, found) in enumerate(zip(self.steps, taken)):
            limit = taken[n + 1][0].n if n + 1 < len(taken) else float("inf")
            mine = [i for i in found if i.n < limit]
            matched += mine
            if not isinstance(step.count, AtLeast) and len(mine) != step.count:
                return (["    %s: %d match(es) before %s, wanted %s:" % (
                    step.text, len(mine), "the next step" if n + 1 < len(taken) else "the window ends",
                    wanted(step.count))] + ctx.lines(mine), matched)
        return [], matched

    def evaluate(self, ctx, window):
        both = any(len(ctx.lenses(step.lens, window)) > 1 for step in self.steps)
        detail, matched = [], []
        for one in (M, S) if both else (None,):
            bad, found = self.chain(ctx, window, one)
            detail += bad
            matched += found
        return Result(self, not detail, detail, matched)


class After(Assert):
    """The asserts within a window after an anchor: from the time its input began to within seconds after its
    last part was sent. each: the asserts must hold after every occurrence of the label."""

    def __init__(self, anchor, asserts, within=DEFAULT_WITHIN, every=False):
        kind = "each" if every else "after"
        if every and not (isinstance(anchor, str) and anchor):
            raise RefError("each() takes a label, not %r" % (anchor,))
        self.anchor = anchor_of(anchor, kind + "()")
        self.every = every
        self.within = seconds(within, "%s(): within=" % kind)
        asserts = all_asserts(asserts, kind + "()")
        self.assert_ = Together(asserts) if len(asserts) != 1 else asserts[0]
        self.text = "%s(%r, within=%g): %s" % (kind, anchor, within, self.assert_.text)

    def inner(self):
        return [self.assert_]

    def anchors(self):
        return [self.anchor]

    def whole(self):
        return False                         # its windows are its own (statements.last_close())

    def least(self, within):
        return 0.0

    def windows(self, ctx):
        """The window after each occurrence of the anchor, open while the run has not reached its end."""
        found = ctx.anchors.get(self.anchor) if self.every else [ctx.resolve(self.anchor)]
        if not found:
            raise RefError("no input labelled %r" % (self.anchor,))
        return [Window(first, min(last + self.within, ctx.now), lens, last + self.within > ctx.now + EPS,
                       close=last + self.within, since=since) for first, last, lens, since in found]

    def evaluate(self, ctx, window, closed_by=None):
        """closed_by: only the windows that had closed by then (violated())."""
        detail, results, ok, pending = [], [], True, False
        for w in self.windows(ctx):
            if closed_by is not None and w.close > closed_by:
                continue
            r = self.assert_.run(ctx, w)
            results.append(r)
            if not r.ok:
                ok, pending = False, pending or r.pending
                detail += ["    at %s (%s):" % (w, w.lens or "both")] + r.detail
                if r.pending and w.open:
                    detail.append("      this window closes at %s, %s s after %s" % (
                        fmt(w.close), fmt(w.close - ctx.now), "the run's end" if ctx.final else "this look"))
        return gathered(self, ok, results, detail, pending)

    def settles(self):
        return False                         # inside another assert: its windows are its own

    def violated(self, ctx, end):
        """Violated for good: the asserts fail in a window that had closed by end (SETTLE earlier: a panel content
        is in the trace once it has stayed up that long)."""
        if not self.assert_.settles():
            return None
        r = self.evaluate(ctx, None, end - SETTLE)
        return None if r.ok else r


class KnownFailure(Assert):
    """known_failure("reason", assert, seeds=(...)): the assert is known to fail, for that reason, under those
    seeds (without seeds=: under every seed). Where it stands the run goes on as if it had held, once its
    window has closed (a line waits its whole time for it); the run is then reported KNOWN, which does not
    fail the suite, if nothing else failed. Every other failure of the test is a failure. If the assert
    holds, the run is reported as an unexpected pass, which does fail the suite: the mark is to be taken out.
    Under a seed that is not named the assert is an assert like any other."""

    def __init__(self, reason, assert_, seeds=None):
        if not isinstance(reason, str) or not reason.strip():
            raise RefError("known_failure() takes the reason first, as text, then the assert that fails: "
                           "known_failure(\"why\", recv(M, ...))")
        if not isinstance(assert_, Assert) or isinstance(assert_, (KnownFailure, Ignore)):
            raise RefError("known_failure(%r, ...) takes the one assert that is known to fail, not %r" % (
                reason, assert_))
        if seeds is not None:
            if not isinstance(seeds, tuple) or not seeds:
                raise RefError("known_failure(): seeds= is a list of seeds, seeds=(3, 7)")
            seeds = tuple(whole(s, "known_failure(): a seed") for s in seeds)
        self.reason = reason
        self.assert_ = assert_
        self.seeds = seeds
        self.text = "known_failure(%r, %s%s)" % (
            reason, assert_.text, "" if seeds is None else ", seeds=(%s)" % ", ".join(map(str, seeds)))

    def inner(self):
        return [self.assert_]

    def least(self, within):
        return within if self.seeds is None else self.assert_.least(within)

    def evaluate(self, ctx, window):
        r = self.assert_.run(ctx, window)
        if not ctx.covers(self.seeds):
            return gathered(self, r.ok, [r], r.detail, r.pending)
        if r.ok:
            return Result(self, True, [], r.matched, known=r.known, passed=r.passed + [self])
        if window.open or r.pending:
            return Result(self, False, r.detail, r.matched, pending=True)
        return Result(self, True, [], r.matched, known=r.known + [(self, r)], passed=r.passed)


class Accounting(Assert):
    """Every item the protocol says needs an assert was matched by one (statements.run())."""

    def __init__(self, text):
        self.text = text


class Booted(Assert):
    """Both cores of each lens started at power-on. This is also the check that the firmware's console is
    being read at all: without it never("fatal") and never("boot", after=1) would hold of a log that is not
    understood."""
    text = "power-on: every lens boots its application and network core in the first second"

    def evaluate(self, ctx, window):
        missing = [(lens, boot) for lens in (M, S) for boot in BOOTS
                   if not any(i.text == boot for i in ctx.between(lens, 0.0, 1.0))]
        detail = ["      no %r from %s (the firmware did not start, or its console is not read: trace.UART)"
                  % (boot, lens) for lens, boot in missing]
        return Result(self, not missing, detail)


# ---------------------------------------------------------------------------------------------- constructors

def any_of(*xs):
    """Patterns: one of them matches. Asserts: one of them holds."""
    if xs and all(isinstance(x, Assert) for x in xs):
        return AnyOf(xs)
    if not xs or any(isinstance(x, Assert) for x in xs):
        raise RefError("any_of() takes asserts, or patterns, or values of a field: one kind, at least one")
    return OneOf(xs)


def event(lens, pattern, count=1):
    """An item that is not a packet from the glasses: the phone's link events, boots, audio start and end."""
    values = pattern.values if isinstance(pattern, OneOf) else (pattern,)
    if values and all(isinstance(p, str) and (is_hex(p) or p.startswith("rx ")) for p in values):
        raise RefError("event() is for what is not a packet from the glasses (link events, boots, audio); "
                       "a packet is recv()")
    return Event(lens, pattern, count)


def quiet(lens, pattern):
    """No such item from the lens in the whole window."""
    return Event(lens, pattern, 0, "quiet", "quiet(%s, %s)" % (lens, pattern))


def display(lens, predicate):
    return Display(lens, predicate)


def audio(lens, min_seconds=0, during=False):
    return Audio(lens, min_seconds, during)


def state(query, equals=None, contains=None, lens=B):
    return State(query, equals, contains, lens)


def no_audio(lens=B, after=0):
    return NoAudio(lens, after)


def never(pattern, after=0, lens=B):
    return Never(pattern, after, lens)


def ignore(pattern, lens=B):
    return Ignore(pattern, lens)


def together(*asserts):
    return Together(asserts)


def sequence(*steps):
    return Sequence(steps)


def after(anchor, *asserts, within=DEFAULT_WITHIN):
    return After(anchor, asserts, within)


def each(label, *asserts, within=DEFAULT_WITHIN):
    return After(label, asserts, within, every=True)


def known_failure(reason, assert_=None, seeds=None):
    return KnownFailure(reason, assert_, seeds)


def frame_name(x, what):
    if not isinstance(x, str) or not x:
        raise RefError("%s takes a panel content: its sha12 or a name of frames.txt, not %r" % (what, x))
    return x


def shows(x):
    return Shows("shows", frame_name(x, "shows()"))


def settles_to(x):
    return SettlesTo("settles_to", frame_name(x, "settles_to()"))


def differs_from(label):
    return DiffersFrom("differs_from", label)


def same_as(label):
    return SameAs("same_as", label)


changes, unchanged, nonblank, blank, same_on_both = (
    Changes("changes"), Unchanged("unchanged"), Nonblank("nonblank"), Blank("blank"), SameOnBoth("same_on_both"))


NAMES = {n: globals()[n] for n in (
    "event", "quiet", "display", "audio", "no_audio", "state", "never", "ignore", "together", "sequence", "after",
    "each", "known_failure", "shows", "settles_to", "changes", "unchanged", "differs_from", "same_as", "nonblank",
    "blank", "same_on_both", "any_of", "at_least", "M", "S", "B", "A")}
