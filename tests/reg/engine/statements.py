"""Statements: the lines of a test file, run one after the other against a live simulator session
(driver.Session). Each one sends inputs, advances the emulation, or both, and may carry asserts (asserts.py) that
must hold before the file goes on:

    action(lens, input, *asserts, within=3, label=None)   send the input to the lens, then run until the asserts
                                             hold (or within seconds pass: the asserts fail); with no asserts,
                                             go on at once; label: the name after("label", ...) refers to
    expect(*asserts, within=3)               no input: run until the asserts hold
    holds(*asserts)                          no input and no waiting: the asserts hold now. For the state of
                                             the panels (blank, nonblank, settles_to() ...: as it has been for
                                             the last asserts.HOLD seconds) and for after()/each()
    mark("label")                            name the current time as an anchor
    wait(seconds)                            run that long
    at(seconds)                              run until that emulated time; if it has passed already (an earlier
                                             line ran long) the run goes on at once and says so in a note
    check(lens, "monitor command", equals=, contains=)   ask the lens's model and compare its answer now;
                                             without equals= or contains=, only ask (for a state() later)

The window of a line is from the moment its input starts to within seconds after the input has been sent. A
line goes on as soon as its asserts hold, but what it says about counts is about the whole window: when the run
is over every line is evaluated again on the complete trace, from its start to the end of its window or to the
start of the next action or expect line, whichever comes first. So recv(M, "F5 1E") fails when a second F5 1E
follows while the file is still waiting (a wait(), a check(), the rest of a quiet()), and the items a line
accounts for are the ones in that window, not every such item of the run. What was in the trace before a line
began is not in its window, also when it carries the very time the line began at (the phone's times have 10 ms):
the answer to one request does not pass for the answer to the next.

An input is a monitor command (str), bytes the phone writes as they are, or a list of those and of numbers
(seconds to wait between them): taps(3) is [TAP, 0.2, TAP, 0.2, TAP]. What the phone writes is its protocol's:
send() and phone_on() of the suite's suite.py. The packets of a bulk transfer are kept in a buffer, one a line of
packets/NAME.hex in the suite's directory: buf("NAME", 3) is its third packet, buf("NAME", 2, 24) the list of
its packets 2 to 24. paced(packets, gap) puts gap seconds between the items of a list.

A label names the input of its line for after("label", ...), each(), never(after=("label", s)) and the like:
the time the input began and the time its last part was sent ([PRESS, 2.0, RELEASE]: 2 s later).

Asserts whose window is the run so far (never, ignore, state over queries, after(t, ...)) can stand on their
own and are evaluated at the end of the run. The run goes on after the last line until every stand-alone
after() or each() window that says something about its whole length (quiet, an exact count, the state of a
panel at its end) has closed, and says so in a note; a window that is still open at the end for another reason
is named in a note as well. When a line fails, the run stops there and the stand-alone asserts are not
evaluated; those that were violated for good by then (a never() with a hit, an after() or each() whose window
had closed) are reported with the failure all the same, as "also violated": often the cause of what the line
then missed.
"""
import os
import re

from . import asserts, inputs
from .asserts import Assert, Context, Result, Window, DEFAULT_WITHIN, HOLD
from .base import RefError, fmt, hexbytes
from .inputs import B, M, S
from .trace import Item, SETTLE

STEP = 0.05                                 # seconds the emulation runs between two looks at the trace
EPS = 1e-9
TAP, PRESS, RELEASE = inputs.TOUCH + "Tap 80", inputs.TOUCH + "Press", inputs.TOUCH + "Release"


class Statement:
    text = "?"
    where = None                            # "g1/name.ref:12": the loader sets it
    included = False                        # from an include(): the loader sets it

    def execute(self, run):
        """run: the Runner; returns a list of Results, possibly empty."""
        raise NotImplementedError

    def inner(self):
        """The asserts the statement carries."""
        return ()

    def labels(self):
        """The labels the statement defines."""
        return ()

    def __str__(self):
        return self.text


def describe_input(x):
    if isinstance(x, bytes):
        return x.hex(" ").upper() if len(x) <= 12 else x[:12].hex(" ").upper() + " ... (%d B)" % len(x)
    if isinstance(x, (int, float)):
        return "%g s" % x
    if isinstance(x, (list, tuple)):
        return "[" + ", ".join(describe_input(i) for i in x) + "]"
    return str(x)


def flatten(x):
    """An input as a list of packets, commands and waits."""
    if isinstance(x, (list, tuple)):
        return [i for e in x for i in flatten(e)]
    if isinstance(x, (bytes, str)) and x:
        return [x]
    if isinstance(x, (int, float)) and not isinstance(x, bool) and x >= 0:
        return [x]
    raise RefError("not an input: %r (a monitor command, a packet of send(), seconds to wait, or a list of those)"
                   % (x,))


def lens_of(lens, what):
    return asserts.lens_of(lens, what, anchor=False)


class Line:
    """A line with asserts that ran: its window, for the evaluation when the run is over. end: where the
    window closes (its deadline, or earlier where the next action or expect line began); at: up to where the
    panels were known at the look the line went on at."""

    def __init__(self, statement, start, end, lens, since=None):
        self.statement = statement
        self.start = start
        self.end = end
        self.lens = lens
        self.since = since
        self.at = None
        self.left = None
        self.until = None

    def window(self):
        return Window(self.start, self.end, self.lens, at=self.at, since=self.since, until=self.until)


class Action(Statement):
    def __init__(self, lens, x, asserts_, within=DEFAULT_WITHIN, kind="action", label=None):
        self.lens = lens_of(lens, kind + "()")
        self.kind = kind
        self.input = flatten(x) if x is not None else []
        self.asserts = [] if not asserts_ else asserts.all_asserts(asserts_, kind + "()")
        self.within = asserts.seconds(within, "%s(): within=" % kind)
        if self.asserts and self.within <= 0:
            raise RefError("%s(..., within=%g) has no time to wait in: what must hold at once is holds(...)"
                           % (kind, within))
        if label is not None and not (isinstance(label, str) and label):
            raise RefError("%s(): label= is a name, not %r" % (kind, label))
        self.label = label
        self.text = "%s(%s%s%s, within=%g)" % (kind, "" if x is None else "%s, %s" % (lens, describe_input(x)),
                                              ", " if x is not None and self.asserts else "",
                                              ", ".join(c.text for c in self.asserts), within)

    def inner(self):
        return self.asserts

    def labels(self):
        return [self.label] if self.label else []

    def execute(self, run):
        start, since = run.now, run.live.came()  # the window opens before the input goes: what happens during
        run.close_line()                         # a multi-step input ([PRESS, 2.0, RELEASE]) is in it
        last = run.send(self.lens, self.input)
        if self.label:
            run.mark(self.label, start, last, self.lens, since)
        if not self.asserts:
            return []
        deadline = run.now + self.within
        lens = None if self.lens == B else self.lens
        line = run.open_line(self, start, deadline, lens, since)
        patience = SETTLE if any(a.display() for a in self.asserts) else 0.0   # the panels are known that late
        while True:
            window = Window(start, min(run.now, deadline), lens, run.now < deadline - EPS, close=deadline,
                            since=since)
            ctx = run.context()
            results = [a.run(ctx, window) for a in self.asserts]
            if all(r.ok for r in results) or run.now >= deadline + patience - EPS:
                break
            run.step(min(run.step_size, (deadline if window.open else deadline + patience) - run.now))
        line.at = min(window.end, ctx.seen)
        line.left = run.now
        return [r if r.ok else r.under("    at %s (%s)" % (window, self.lens)) for r in results]


class Holds(Statement):
    """holds(*asserts): they hold now, without the emulation going on."""

    def __init__(self, asserts_):
        self.asserts = asserts.all_asserts(asserts_, "holds()")
        for a in self.asserts:
            self.allowed(a)
        self.text = "holds(%s)" % ", ".join(a.text for a in self.asserts)

    def allowed(self, a):
        if isinstance(a, (asserts.Group, asserts.KnownFailure)):
            for c in a.inner():
                self.allowed(c)
        elif not (isinstance(a, asserts.After) or isinstance(a, asserts.Display) and a.pred.state):
            raise RefError("holds() takes the state of the panels (blank, nonblank, settles_to(), same_as() ...) and "
                           "after() or each(); %s needs a window to happen in: expect(...) or after(...)" % a.text)

    def inner(self):
        return self.asserts

    def execute(self, run):
        ctx = run.context()
        line = run.open_line(self, ctx.seen - HOLD, run.now, None, counts=False)
        line.at = ctx.seen
        line.left = run.now
        results = [a.run(ctx, line.window()) for a in self.asserts]
        return [r if r.ok else r.under("    at %s" % fmt(run.now)) for r in results]


class Mark(Statement):
    def __init__(self, label):
        if not isinstance(label, str) or not label:
            raise RefError("mark() takes a label, not %r" % (label,))
        self.label, self.text = label, "mark(%r)" % label

    def labels(self):
        return [self.label]

    def execute(self, run):
        run.mark(self.label, run.now, run.now, B, run.live.came())
        return []


class Wait(Statement):
    """wait(seconds), or at(seconds): until that time of the run."""

    def __init__(self, seconds=None, until=None):
        self.seconds = seconds
        self.until = until
        self.text = "wait(%g)" % seconds if until is None else "at(%g)" % until

    def execute(self, run):
        if self.until is None:
            run.until(run.now + self.seconds)
        elif self.until < run.now - 1e-6:
            run.notes.append("%s: at(%g) had passed %s s before, the lines before it took that long: the run "
                             "went on at once, off the times the file was written for" % (
                                 self.where, self.until, fmt(run.now - self.until)))
        else:
            run.until(self.until)
        return []


class Check(Statement):
    """A monitor query now, compared as a state() assert."""

    def __init__(self, lens, command, equals=None, contains=None):
        self.lens = lens_of(lens, "check()")
        if not isinstance(command, str) or not command.strip():
            raise RefError("check() takes a lens and a monitor command, not %r" % (command,))
        self.command = command.strip()
        self.state = None                       # without equals= or contains=: only asked
        if equals is not None or contains is not None:
            self.state = asserts.State(self.command, equals, contains, self.lens)
        self.text = "check(%s, %s%s)" % (lens, self.command, "" if self.state is None else " %s %r" % (
            "=" if equals is not None else "~", equals if equals is not None else contains))

    def execute(self, run):
        got = run.query(self.lens, self.command)
        if self.state is None:
            return []
        detail = [d for lens in ((M, S) if self.lens == B else (self.lens,))
                  for d in self.state.judge(lens, [i for i in got if i.lens == lens], "now")]
        return [Result(self, not detail, detail, got)]


# ---------------------------------------------------------------------------------------------- constructors

def action(lens, x, *asserts_, within=DEFAULT_WITHIN, label=None):
    return Action(lens, x, asserts_, within, label=label)


def expect(*asserts_, within=DEFAULT_WITHIN):
    if not asserts_:
        raise RefError("expect() takes at least one assert; to let time pass, wait(seconds)")
    return Action(B, None, asserts_, within, "expect")


def holds(*asserts_):
    return Holds(asserts_)


def mark(label):
    return Mark(label)


def wait(seconds):
    return Wait(seconds=asserts.seconds(seconds, "wait()"))


def at_time(seconds):
    return Wait(until=asserts.seconds(seconds, "at()"))


def check(lens, command, equals=None, contains=None):
    return Check(lens, command, equals, contains)


# the input vocabulary: monitor commands and packets

def gesture(name):
    if not isinstance(name, str) or not re.fullmatch(r"\w+", name):
        raise RefError("gesture() takes the name of a gesture of the IMU model, not %r" % (name,))
    return inputs.IMU + 'PlayGesture "%s"' % name


def pose(g):
    if not isinstance(g, tuple) or len(g) != 3 or not all(isinstance(v, (int, float)) for v in g):
        raise RefError("pose() takes the accelerometer's (x, y, z) in g: LEVEL, UP45 ..., not %r" % (g,))
    return ["%sAcceleration%s %s" % (inputs.IMU, axis, v) for axis, v in zip("XYZ", g)]


def paced(packets, gap):
    """A list of inputs sent gap seconds apart: paced(buf('nav_panoramic', 2, 24), 0.04)."""
    gap = asserts.seconds(gap, "paced(): the gap")
    return [x for p in flatten(packets) for x in (p, gap)][:-1]


def taps(n=1, gap=0.2):
    return paced([TAP] * asserts.whole(n, "taps()", 1), gap)


class Buffers:
    """The packet buffers of a suite: its packets/NAME.hex files, one packet a line as hex bytes; # starts a
    comment."""

    def __init__(self, directory):
        self.directory, self.loaded = directory, {}

    def buffer(self, name):
        if name not in self.loaded:
            path = os.path.join(self.directory, "%s.hex" % name)
            if not isinstance(name, str) or not re.fullmatch(r"\w+", name) or not os.path.isfile(path):
                raise RefError("no packet buffer %r (%s)" % (name, os.path.relpath(path, inputs.REPO)))
            with open(path, encoding="utf-8") as f:
                lines = [line.split("#")[0].strip() for line in f]
            self.loaded[name] = [hexbytes(x, "packets/%s.hex:%d" % (name, n)) for n, x in enumerate(lines, 1) if x]
        return self.loaded[name]

    def buf(self, name, first, last=None):
        """buf("NAME", 3): packet 3 (from 1) of the buffer; buf("NAME", 2, 24): the list of its packets 2 to 24."""
        got, end = self.buffer(name), first if last is None else last
        if not all(isinstance(i, int) and not isinstance(i, bool) for i in (first, end)):
            raise RefError("buf(%r, ...) takes the number of a packet, or of the first and the last, not %r" % (
                name, first if last is None else (first, last)))
        if not 1 <= first <= end <= len(got):
            raise RefError("buffer %r has packets 1 to %d, not %s" % (
                name, len(got), first if last is None else "%d to %d" % (first, last)))
        return got[first - 1] if last is None else got[first - 1:last]


NAMES = {"action": action, "expect": expect, "holds": holds, "wait": wait, "at": at_time, "check": check,
         "mark": mark, "gesture": gesture, "pose": pose, "taps": taps, "paced": paced,
         "TAP": TAP, "PRESS": PRESS, "RELEASE": RELEASE}
NAMES.update((n, getattr(inputs, n)) for n in (
    "IMU", "TOUCH", "NPM", "OPT", "NFC", "JBD", "PDM", "case", "LEVEL", "UP45", "UP70", "UP81", "FLAT"))


def timeline(loaded):
    """When each statement begins, without running: [(statement, earliest, latest)] in emulated seconds. The
    earliest takes every line as going on the moment it can (an assert about a whole window still takes the
    window, the state of a panel HOLD), the latest takes every line as using all of its within."""
    out, early, late = [], 0.0, 0.0
    for st in loaded:
        if not isinstance(st, Statement):
            continue
        out.append((st, early, late))
        if isinstance(st, Wait):
            if st.until is None:
                early, late = early + st.seconds, late + st.seconds
            else:
                early, late = max(early, st.until), max(late, st.until)
        elif isinstance(st, Action):
            sent = sum(x for x in st.input if isinstance(x, (int, float)))
            patience = SETTLE if any(a.display() for a in st.asserts) else 0.0
            early += sent + max([a.least(st.within) for a in st.asserts], default=0.0)
            late += sent + (st.within + patience if st.asserts else 0.0)
    return out + [(None, early, late)]


def needs_phone(loaded, proto):
    """Whether a file sends something to the virtual phone: its session needs the phone's time quantum."""
    return any(isinstance(i, bytes) or isinstance(i, str) and i.startswith(proto.PH)
               for x in loaded if isinstance(x, Action) for i in x.input)


# --------------------------------------------------------------------------------------------------- running

class Runner:
    """One run: the session, the live trace, and what the statements have done so far."""

    def __init__(self, session, live, proto, frames=None, seed=None, step=STEP):
        self.session = session
        self.live = live
        self.proto = proto
        self.frames = frames or {}
        self.seed = seed
        self.step_size = step
        self.anchors = {}                       # {label: [(first, last, lens, since)]}
        self.lines = []                         # the lines with asserts that have run
        self.notes = []
        self.counting = None                    # the line whose window is still open for counts
        self.looked = None                      # the Context of the last look, while nothing has changed since

    @property
    def now(self):
        return self.session.now

    def mark(self, label, first, last, lens, since):
        self.anchors.setdefault(label, []).append((first, last, lens, since))
        self.looked = None

    def open_line(self, statement, start, end, lens, since=None, counts=True):
        line = Line(statement, start, end, lens, since)
        self.lines.append(line)
        if counts:
            self.counting = line
        return line

    def close_line(self):
        """The line before stops counting where the next action or expect line begins, or the run ends."""
        if self.counting:
            if self.now <= self.counting.end:
                self.counting.end, self.counting.until = self.now, self.live.came()
            self.counting = None

    def step(self, seconds):
        """Advance the emulation and take in what it wrote."""
        for lens, on in self.session.run_for(seconds):
            self.live.power(lens, self.now, on)
        self.live.read(self.now)
        self.looked = None

    def until(self, target):
        while self.now < target - EPS:
            self.step(min(self.step_size, target - self.now))

    def send(self, lens, items):
        """Send the parts of an input; returns the time the last of them went."""
        last = self.now
        for x in items:
            if isinstance(x, (int, float)):
                self.until(self.now + x)
            else:
                self.session.command(self.proto.write(x) if isinstance(x, bytes) else x, lens)
                last = self.now
        return last

    def query(self, lens, command):
        """Ask the lens's model; the outputs go into the trace and come back as items."""
        got = []
        for one, lines in self.session.query(command, lens):
            text = "?%s = %s" % (command, " | ".join(inputs.query_output(lines)))
            self.live.add(self.now, one, text)
            got.append(Item(self.now, one, text, -1))
        self.looked = None
        return got

    def context(self, final=False):
        if final or self.looked is None:
            ctx = Context(self.live.items(self.now, final), self.anchors, self.now, self.proto, self.frames,
                          self.seed, final, running={} if final else self.live.running())
            if final:
                return ctx
            self.looked = ctx
        return self.looked


class Outcome:
    """What a run came to. failure: the failing Result, or None; line: the statement the failing assert stands
    in; also: the Results of the other asserts known to be violated. known: the known failures that failed as
    expected, [(mark, Result)]; passed: the marks whose assert held although it is marked. notes: what the
    reader of a result should know about the run. items: the trace. lines: the lines with asserts as they
    ran (Line: where each began, went on and stopped counting), for whoever writes a test from a run."""

    def __init__(self, failure, items, notes, results=(), line=None, also=(), lines=()):
        self.failure = failure
        self.lines = list(lines)
        self.line = line
        self.also = list(also)
        self.items = items
        self.notes = notes
        failed = {id(mark): (mark, r) for res in results for mark, r in res.known}
        self.known = list(failed.values())
        held = {id(mark): mark for res in results for mark in res.passed if id(mark) not in failed}
        self.passed = list(held.values())


def afters(assert_):
    """The after() and each() asserts in an assert, nested ones included."""
    if isinstance(assert_, asserts.After):
        yield assert_
    for c in assert_.inner():
        yield from afters(c)


def last_close(alone, ctx):
    """(time, assert) of the stand-alone after() / each() window that closes last among those that say
    something about their whole length, or (0, None)."""
    found = (0.0, None)
    for a in [a for st in alone for a in afters(st) if a.assert_.whole()]:
        try:
            close = max(w.close for w in a.windows(ctx))
        except RefError:
            continue                            # a label that is not there: the evaluation reports it
        if close > found[0]:
            found = (close, a)
    return found


def leaves(assert_):
    """The asserts an assert is made of that are made of no others."""
    inner = list(assert_.inner())
    return [leaf for c in inner for leaf in leaves(c)] if inner else [assert_]


def open_windows(alone, ctx):
    """The stand-alone after() / each() windows that are still open at the run's end, as notes. A window that
    only says what needs no assert (ignore()) is none: it has nothing to miss."""
    out = []
    for a in [a for st in alone for a in afters(st)
              if not all(isinstance(leaf, asserts.Ignore) for leaf in leaves(a.assert_))]:
        try:
            close = max(w.close for w in a.windows(ctx))
        except RefError:
            continue
        if close > ctx.now + 1e-6:
            out.append("%s: %s(%r, within=%g) closes %s s after the run's end (%s): %s was checked up to the end "
                       "only" % (a.where, "each" if a.every else "after", a.anchor, a.within, fmt(close - ctx.now),
                                 fmt(ctx.now), a.assert_.text[:90]))
    return out


def also_violated(alone, ctx, end):
    """The stand-alone asserts violated for good by end, where a run stopped at a failing line: [Result]."""
    strict, out = ctx.strictly(), []
    for st in alone:
        try:
            out.append(st.violated(strict, end))
        except RefError:                      # a label the run did not reach: not known
            pass
    return [r for r in out if r is not None]


def run(loaded, session, live, proto, frames=None, seed=None):
    """Execute the statements in order, stopping at the first one whose asserts fail. If none did: let the run
    go on until the stand-alone windows have closed (last_close()), then evaluate on the complete trace every
    line again over its whole window, the power-on boot, the stand-alone asserts and the accounting, stopping
    at the first failure too. Returns the Outcome. proto: the suite's protocol (protocol.py). seed: of the
    run, for the known failures that name seeds."""
    r = proto.RUNNER(session, live, proto, frames, seed, STEP)
    alone = [st for st in loaded if isinstance(st, Assert)]
    for st in loaded:
        if not isinstance(st, Statement):
            continue
        try:
            bad = next((res for res in st.execute(r) if not res.ok), None)
        except RefError as e:
            bad = Result(st, False, ["      " + str(e)])
        if bad:
            ctx = r.context(final=True)
            return Outcome(bad, ctx.items, r.notes, line=st, also=also_violated(alone, ctx, r.now), lines=r.lines)
    close, last = last_close(alone, r.context())
    if close > r.now + EPS:
        r.notes.append("%s: the run went on for %s s after its last line, until %s(%r, within=%g) had closed" % (
            last.where, fmt(close - r.now), "each" if last.every else "after", last.anchor, last.within))
        r.until(close)
    r.close_line()
    ctx = r.context(final=True)
    r.notes += open_windows(alone, ctx)
    results = []

    def failed(res, line=None, others=()):
        also = [x for x in (c.run(ctx, Window(0.0, r.now)) for c in others) if not x.ok]
        return Outcome(res, ctx.items, r.notes, results, line, also, r.lines)

    for line in r.lines:
        for a in line.statement.inner():
            res = a.run(ctx, line.window())
            results.append(res)
            if not res.ok:
                where = "    at %s (%s), over the line's whole window once the run was over (the line had gone on " \
                        "at %s)" % (line.window(), line.lens or "both", fmt(line.left))
                return failed(res.under(where), line.statement)
    res = asserts.Booted().run(ctx, Window(0.0, r.now))
    if not res.ok:
        return failed(res)
    for n, st in enumerate(alone):
        res = st.run(ctx, Window(0.0, r.now))
        results.append(res)
        if not res.ok:
            return failed(res, others=alone[n + 1:])
    accounted = {i.n for res in results for i in res.matched}
    left = [i for i in ctx.items if proto.needs_assert(i.text) and i.n not in accounted
            and not (i.text.startswith("boot") and i.t < 1)]
    if left:
        return failed(Result(asserts.Accounting(proto.ACCOUNTING), False, ctx.lines(left, asserts.LIMIT * 2), left))
    return Outcome(None, ctx.items, r.notes, results, lines=r.lines)
