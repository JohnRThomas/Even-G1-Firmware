#!/usr/bin/env python3
"""Unit tests of the regression engine itself (tests/reg/engine, g1/suite.py, run.py): no simulator.

    python3 tests/reg/test_engine.py            (or: python3 -m unittest discover -s tests/reg -p "test_*.py")

A test feeds the engine a scripted trace (Context over a list of items) or a scripted session (Sim, which
writes the log and frame files a Renode session would) and looks at the verdict. Each case that a code review
found the engine to get wrong is here with the letter of the finding (reg_review.md, 2026-11-19).
"""
import argparse
import contextlib
import glob
import io
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))       # on sys.path already: this file's directory
sys.dont_write_bytecode = True

import run as runner                                                          # noqa: E402
from engine import asserts, driver, loader, statements, trace                 # noqa: E402
from engine.asserts import Context, Window                                    # noqa: E402
from engine.base import RefError, fmt                                         # noqa: E402
from engine.inputs import A, B, M, S                                          # noqa: E402
from engine.statements import PRESS, RELEASE, TAP                             # noqa: E402
from engine.trace import BLANK, Item                                          # noqa: E402

G1 = loader.Suite(os.path.join(HERE, "g1"))
g1 = G1.names
X, Y = "a" * 12, "b" * 12                                                     # two panel contents


def items(*rows):
    return [Item(t, lens, text, n) for n, (t, lens, text) in enumerate(sorted(rows, key=lambda r: r[0]))]


def ctx(rows, now=10.0, proto=G1.proto, anchors=None, final=True, seed=1, frames=None):
    return Context(items(*rows), anchors or {}, now, proto, frames or {"x": X, "y": Y}, seed, final)


def ok(assert_, rows, start=0.0, end=10.0, **kw):
    return assert_.run(ctx(rows, **kw), Window(start, end)).ok


class Sim:
    """A scripted simulator session for statements.run(): events = [(t, lens, kind, text)], kind "phone" (a
    line of the virtual phone), "uart" (a line of the firmware's console) or "frame" (a panel content). The
    two lenses boot at 0.08 s unless boots=False. Commands are recorded in .sent; answers: {command: output}."""

    def __init__(self, events=(), boots=True, answers=None):
        self.out = tempfile.mkdtemp(prefix="reg-unit-")
        self.now, self.sent, self.answers = 0.0, [], answers or {}
        self.events = sorted(events, key=lambda e: e[0])
        if boots:
            self.events = [(0.08, lens, "uart", "*** Booting nRF Connect SDK ***", core)
                           for lens in (M, S) for core in ("0", "_net")] + self.events
        self.frames = 0

    def run_for(self, seconds):
        self.now = round(self.now + seconds, 6)
        while self.events and self.events[0][0] <= self.now:
            t, lens, kind, text = self.events[0][:4]
            machine = "g1" if lens == M else "g1_slave"
            if kind == "frame":
                self.frames += 1
                with open(os.path.join(self.out, lens + ".frames"), "a") as f:
                    f.write("%.6f %d 0 0 640 480 %s AAAA\n" % (t, self.frames, text + "0" * 28))
            else:
                where = "vcentral: %.3fs  %s" % (t, text) if kind == "phone" else \
                    "uart%s: [host: 1s (+1ms)|virt: %gms (+1ms)] %s" % (self.events[0][4] if len(self.events[0]) > 4
                                                                         else "0", t * 1000, text)
                with open(os.path.join(self.out, "log.txt"), "a") as f:
                    f.write("[00:00:00.0000]  [INFO] %s/%s\n" % (machine, where))
            self.events.pop(0)
        return []

    def command(self, command, lens=M):
        self.sent.append((self.now, lens, command))
        return []

    def query(self, command, lens=M):
        return [(one, [self.answers.get(command, "")]) for one in ((M, S) if lens == B else (lens,))]


def play(lines, events=(), suite=G1, seed=1, step=0.05, **kw):
    """Run the statements and asserts against a scripted session: (Outcome, the Sim)."""
    sim = Sim(events, **kw)
    real, statements.STEP = statements.STEP, step
    try:
        live = trace.LiveTrace(sim.out, suite.proto.item)
        return statements.run(list(lines), sim, live, suite.proto, suite.frames(), seed), sim
    finally:
        statements.STEP = real
        shutil.rmtree(sim.out, ignore_errors=True)


def phone(t, lens, text):
    return (t, lens, "phone", text)


def frame(t, lens, sha):
    return (t, lens, "frame", sha)


def reason(outcome):
    f = outcome.failure
    return None if f is None else "%s | %s" % (f.what.text, " | ".join(f.detail))


class Accounting(unittest.TestCase):
    def test_a1_one_assert_does_not_account_for_every_such_item(self):
        events = [phone(t, M, "rx F5 1E") for t in (0.5, 1.2, 4.0, 4.5, 5.0)]
        lines = [g1["at"](1), g1["action"](M, TAP, g1["recv"](M, "F5 1E")), g1["action"](M, TAP), g1["wait"](5)]
        out, _ = play(lines, events)
        self.assertIn("accounting", reason(out))
        self.assertEqual([i.t for i in out.failure.matched], [0.5, 4.0, 4.5, 5.0])

    def test_a1_an_action_on_one_lens_does_not_account_for_the_other(self):
        events = [phone(1.2, M, "rx F5 1E"), phone(1.2, S, "rx F5 1E")]
        out, _ = play([g1["at"](1), g1["action"](M, TAP, g1["recv"](A, "F5 1E")), g1["action"](M, TAP)], events)
        self.assertIn("accounting", reason(out))
        self.assertEqual([i.lens for i in out.failure.matched], [S])

    def test_a1_one_audio_assert_accounts_for_its_stream_only(self):
        rows = [(1.0, M, "audio start"), (2.0, M, "audio end"), (5.0, M, "audio start"), (6.0, M, "audio end")]
        r = asserts.audio(M).run(ctx(rows), Window(0.5, 1.5))
        self.assertEqual([i.t for i in r.matched], [1.0, 2.0])

    def test_ignore_accounts(self):
        events = [phone(t, M, "rx F5 1E") for t in (0.5, 1.2)]
        out, _ = play([g1["wait"](2), g1["ignore"]("F5 1E")], events)
        self.assertIsNone(out.failure)

    def test_power_on_boots_need_no_assert_and_later_ones_do(self):
        out, _ = play([g1["wait"](2)])
        self.assertIsNone(out.failure)
        out, _ = play([g1["wait"](3)], [(2.0, M, "uart", "*** Booting nRF Connect SDK ***")])
        self.assertIn("accounting", reason(out))


class Counts(unittest.TestCase):
    def test_a2_a_second_packet_while_the_file_waits_is_counted(self):
        events = [phone(1.2, M, "rx F5 1E"), phone(1.4, M, "rx F5 1E")]
        out, _ = play([g1["at"](1), g1["action"](M, TAP, g1["recv"](M, "F5 1E")), g1["wait"](2)], events)
        self.assertIn("2 match(es) in 1..3.2, wanted 1", reason(out))
        self.assertIn("gone on at 1.2", reason(out))

    def test_a2_the_window_ends_where_the_next_line_begins(self):
        events = [phone(1.2, M, "rx 22 05"), phone(1.4, M, "rx 22 05")]
        lines = [g1["at"](1), g1["action"](M, g1["send"]("22 05"), g1["recv"](M, "22 05")),
                 g1["action"](M, g1["send"]("22 05"), g1["recv"](M, "22 05")), g1["wait"](1)]
        out, _ = play(lines, events)
        self.assertIsNone(out.failure, reason(out))

    def test_a2_an_item_on_the_second_a_line_begins_was_there_before_it(self):
        # the first answer comes at 1.2 s, the very time the second request goes: it is not the second's answer
        events = [phone(1.2, M, "rx 22 05")]
        lines = [g1["at"](1), g1["action"](M, g1["send"]("22 05"), g1["recv"](M, "22 05")),
                 g1["action"](M, g1["send"]("22 05"), g1["recv"](M, "22 05"), within=1)]
        out, sim = play(lines, events)
        self.assertIn("0 match(es) in 1.2..2.2, wanted 1", reason(out))
        self.assertEqual(sim.now, 2.2)

    def test_a2_at_least_takes_the_later_ones(self):
        events = [phone(1.2, M, "rx F5 1E"), phone(1.4, M, "rx F5 1E")]
        lines = [g1["at"](1), g1["action"](M, TAP, g1["recv"](M, "F5 1E", count=g1["at_least"](1))), g1["wait"](2)]
        out, _ = play(lines, events)
        self.assertIsNone(out.failure, reason(out))

    def test_a2_the_same_assert_beside_a_quiet(self):
        events = [phone(1.2, M, "rx F5 1E"), phone(1.4, M, "rx F5 1E")]
        lines = [g1["at"](1), g1["action"](M, TAP, g1["recv"](M, "F5 1E"), g1["quiet"](M, "F5 02"), within=2)]
        out, sim = play(lines, events)
        self.assertIn("2 match(es) in 1..3, wanted 1", reason(out))
        self.assertEqual(sim.now, 3.0)

    def test_a17_texts(self):
        self.assertEqual(asserts.event(M, "boot", asserts.at_least(1)).text, "event(master, boot, count=at_least(1))")
        self.assertEqual(g1["recv"](M, "F5 11", asserts.at_least(1)).text, "recv(master, F5 11, count=at_least(1))")
        self.assertEqual(g1["recv"](M, "F5 11").text, "recv(master, F5 11)")
        self.assertEqual(fmt(1234.567), "1234.567")
        self.assertEqual(fmt(5.0), "5")

    def test_counts_are_validated(self):
        for bad in (-1, "x", True, 1.5):
            self.assertRaises(RefError, asserts.event, M, "boot", bad)
        for bad in (0, "x"):
            self.assertRaises(RefError, asserts.at_least, bad)


class KnownFailures(unittest.TestCase):
    MARK = "known"

    def lines(self, seeds=None):
        return [g1["at"](1),
                g1["action"](M, TAP, g1["known_failure"](self.MARK, g1["recv"](M, "F5 1E"), seeds=seeds), within=1),
                g1["action"](M, TAP, g1["recv"](M, "F5 02"), within=1)]

    def test_a3_the_marked_assert_fails_and_the_run_goes_on(self):
        out, sim = play(self.lines(), [phone(2.5, M, "rx F5 02")])
        self.assertIsNone(out.failure, reason(out))
        self.assertEqual([m.reason for m, _ in out.known], [self.MARK])
        self.assertEqual(out.passed, [])
        self.assertEqual([t for t, _, c in sim.sent], [1.0, 2.0])      # the line waited its whole second

    def test_a3_another_failure_of_the_test_is_a_failure(self):
        out, _ = play(self.lines(), [])
        self.assertIn("recv(master, F5 02)", reason(out))

    def test_a3_the_marked_assert_holds(self):
        out, _ = play(self.lines(), [phone(1.5, M, "rx F5 1E"), phone(2.5, M, "rx F5 02")])
        self.assertIsNone(out.failure)
        self.assertEqual(out.known, [])
        self.assertEqual([m.reason for m in out.passed], [self.MARK])

    def test_a3_seeds(self):
        out, _ = play(self.lines(seeds=(7,)), [phone(2.5, M, "rx F5 02")], seed=7)
        self.assertEqual(len(out.known), 1)
        out, _ = play(self.lines(seeds=(7,)), [phone(2.5, M, "rx F5 02")], seed=8)
        self.assertIn("known_failure", reason(out))                   # under another seed: an assert like any other

    def test_a3_standing_alone(self):
        mark = g1["known_failure"]("no events", g1["never"]("F5"))
        out, _ = play([g1["wait"](2), mark, g1["ignore"]("F5")], [phone(1.0, M, "rx F5 1E")])
        self.assertEqual(len(out.known), 1)
        out, _ = play([g1["wait"](2), mark], [])
        self.assertEqual(len(out.passed), 1)

    def test_a3_the_mark_names_its_assert(self):
        self.assertRaises(RefError, g1["known_failure"], "a reason alone")
        self.assertRaises(RefError, g1["known_failure"], "why", g1["ignore"]("F5"))
        self.assertRaises(RefError, g1["known_failure"], "why", g1["never"]("F5"), seeds=("a",))
        self.assertRaises(RefError, g1["known_failure"], g1["never"]("F5"), "why")

    def test_a3_the_runner_reports_it(self):
        job = runner.Job.__new__(runner.Job)
        job.error, job.items = None, []
        job.outcome, _ = play(self.lines(), [phone(2.5, M, "rx F5 02")])
        self.assertEqual(job.finish()[0], "KNOWN")
        job.outcome, _ = play(self.lines(), [phone(1.5, M, "rx F5 1E"), phone(2.5, M, "rx F5 02")])
        self.assertEqual(job.finish()[0], "XPASS")
        job.outcome, _ = play(self.lines(), [])
        job.suite = G1
        self.assertEqual(job.finish()[0], "FAIL")


class Time(unittest.TestCase):
    def test_a4_no_audio_waits_for_its_window(self):
        out, sim = play([g1["expect"](g1["no_audio"](B), within=0.4)])
        self.assertIsNone(out.failure, reason(out))
        self.assertEqual(sim.now, 0.4)

    def test_a4_a_content_that_was_up_for_a_moment_has_not_settled(self):
        events = [frame(1.1, M, X), frame(1.2, M, Y)]
        out, _ = play([g1["at"](1), g1["action"](M, TAP, g1["display"](M, g1["settles_to"](X)), within=1)], events)
        self.assertIn("settles_to", reason(out))
        out, sim = play([g1["at"](1), g1["action"](M, TAP, g1["display"](M, g1["settles_to"](Y)), within=1)], events)
        self.assertIsNone(out.failure, reason(out))
        self.assertAlmostEqual(sim.now, 1.2 + asserts.HOLD + trace.SETTLE, places=6)

    def test_a4_the_state_of_a_panel_takes_hold_seconds(self):
        rows = [(0.5, M, "frame " + X)]
        late = Context(items(*rows), {}, 5.0 + asserts.HOLD + trace.SETTLE, G1.proto, final=False)
        early = Context(items(*rows), {}, 5.1, G1.proto, final=False)
        for pred in (asserts.nonblank, asserts.settles_to(X)):
            a = asserts.display(M, pred)
            self.assertTrue(a.run(early, Window(5.0, 5.1, open=True)).pending)
            self.assertTrue(a.run(late, Window(5.0, late.now, open=True)).ok)
        self.assertFalse(asserts.display(M, asserts.blank).run(late, Window(5.0, late.now, open=True)).ok)

    def test_a4_shows_is_about_what_comes_up_in_the_window(self):
        rows = [(0.5, M, "frame " + X), (6.0, M, "frame " + Y)]
        self.assertFalse(ok(asserts.display(M, asserts.shows(X)), rows, 5.0, 7.0))
        self.assertTrue(ok(asserts.display(M, asserts.shows(Y)), rows, 5.0, 7.0))
        self.assertTrue(ok(asserts.display(M, asserts.changes), rows, 5.0, 7.0))
        self.assertFalse(ok(asserts.display(M, asserts.changes), rows, 6.5, 7.0))

    def test_a4_same_as_and_differs_from(self):
        rows, anchors = [(0.5, M, "frame " + X), (6.0, M, "frame " + Y)], {"then": [(1.0, 1.0, B, 0)]}
        self.assertTrue(ok(asserts.display(M, asserts.differs_from("then")), rows, 5.0, 7.0, anchors=anchors))
        self.assertFalse(ok(asserts.display(M, asserts.same_as("then")), rows, 5.0, 7.0, anchors=anchors))
        self.assertTrue(ok(asserts.display(M, asserts.same_as("then")), rows, 2.0, 5.0, anchors=anchors))

    def test_a13_a_change_at_the_very_end_of_a_window_is_seen(self):
        rows = [(0.5, M, "frame " + X), (6.98, M, "frame " + Y)]
        self.assertFalse(ok(asserts.display(M, asserts.unchanged), rows, 5.0, 7.0, now=7.0))
        live = Context(items(*rows[:1]), {}, 7.0, G1.proto, final=False)     # the change is not in the trace yet
        self.assertTrue(asserts.display(M, asserts.unchanged).run(live, Window(5.0, 7.0)).pending)

    def test_a17_same_on_both_is_about_both(self):
        self.assertRaises(RefError, asserts.display, M, asserts.same_on_both)
        rows = [(0.5, M, "frame " + X), (0.5, S, "frame " + X)]
        self.assertTrue(ok(asserts.display(B, asserts.same_on_both), rows))
        self.assertFalse(ok(asserts.display(B, asserts.same_on_both), rows + [(9.0, S, "frame " + Y)]))

    def test_a17_blank_is_one_value(self):
        self.assertEqual(asserts.sha_of("blank", {}), BLANK)
        self.assertTrue(ok(asserts.display(M, asserts.settles_to("blank")), [(0.5, M, "frame " + BLANK)]))
        self.assertTrue(ok(asserts.display(M, asserts.blank), []))

    def test_a5_the_run_goes_on_until_a_stand_alone_window_has_closed(self):
        lines = [g1["at"](1), g1["action"](M, TAP, label="tap"), g1["wait"](1),
                 g1["after"]("tap", g1["quiet"](M, "F5 1E"), within=30)]
        out, sim = play(lines, [phone(20.0, M, "rx F5 1E")])
        self.assertEqual(sim.now, 31.0)
        self.assertIn("quiet(master, F5 1E)", reason(out))
        self.assertIn("went on for 29 s", out.notes[0])

    def test_a5_a_window_that_is_open_for_a_positive_assert_is_noted(self):
        lines = [g1["at"](1), g1["action"](M, TAP, label="tap"), g1["wait"](1),
                 g1["after"]("tap", g1["recv"](M, "F5 1E", count=g1["at_least"](1)), within=20)]
        out, sim = play(lines, [phone(1.5, M, "rx F5 1E")])
        self.assertIsNone(out.failure, reason(out))
        self.assertEqual(sim.now, 2.0)
        self.assertIn("closes 19 s after the run's end", out.notes[0])
        out, _ = play(lines[:3] + [g1["after"]("tap", g1["ignore"]("F5 1E", lens=M), within=20)])
        self.assertEqual(out.notes, [])

    def test_a5_a_window_inside_a_line_must_have_closed(self):
        lines = [g1["at"](1), g1["action"](M, TAP, label="tap"), g1["wait"](1),
                 g1["holds"](g1["after"]("tap", g1["quiet"](M, "F5 1E"), within=54))]
        out, _ = play(lines)
        self.assertIn("this window closes at 55", reason(out))

    def test_a7_an_at_in_the_past_is_noted(self):
        out, _ = play([g1["wait"](2), g1["at"](1.5), g1["at"](3)])
        self.assertEqual(len(out.notes), 1)
        self.assertIn("at(1.5) had passed 0.5 s before", out.notes[0])

    def test_a8_a_label_is_the_input_from_its_start_to_its_last_part(self):
        sim = Sim()
        r = statements.Runner(sim, trace.LiveTrace(sim.out, G1.proto.item), G1.proto)
        g1["at"](1).execute(r)
        g1["action"](M, [PRESS, 2.0, RELEASE], label="hold").execute(r)
        self.assertEqual(r.anchors["hold"], [(1.0, 3.0, M, 4)])            # after the four boots
        w = asserts.after("hold", asserts.quiet(M, "x"), within=3).windows(Context([], r.anchors, 10.0, G1.proto))[0]
        self.assertEqual((w.start, w.end), (1.0, 6.0))
        shutil.rmtree(sim.out)

    def test_a12_check_off_the_millisecond_grid(self):
        lines = [g1["wait"](1.23456), g1["check"](M, "model Value", equals="7")]
        out, _ = play(lines, answers={"model Value": "7"}, step=0.01234)
        self.assertIsNone(out.failure, reason(out))
        out, _ = play(lines, answers={"model Value": "8"}, step=0.01234)
        self.assertIn("?model Value = 8", reason(out))

    def test_holds_takes_states_and_closed_windows_only(self):
        self.assertRaises(RefError, g1["holds"], g1["recv"](M, "F5"))
        self.assertRaises(RefError, g1["holds"], g1["display"](M, g1["changes"]))
        self.assertRaises(RefError, g1["holds"])
        self.assertRaises(RefError, g1["expect"], g1["display"](B, g1["blank"]), within=0)
        out, sim = play([g1["wait"](2), g1["holds"](g1["display"](B, g1["blank"]))])
        self.assertIsNone(out.failure, reason(out))
        self.assertEqual(sim.now, 2.0)
        out, _ = play([g1["wait"](2), g1["holds"](g1["display"](M, g1["blank"]))], [frame(1.9, M, X)])
        self.assertIn("display(master, blank)", reason(out))

    def test_timeline(self):
        lines = [g1["at"](5), g1["action"](M, [PRESS, 2.0, RELEASE], g1["recv"](M, "F5")),
                 g1["action"](M, TAP, g1["quiet"](M, "F5"), within=4), g1["wait"](1), g1["at"](8),
                 g1["expect"](g1["display"](B, g1["blank"]), within=6)]
        got = [(round(a, 3), round(b, 3)) for _, a, b in statements.timeline(lines)]
        self.assertEqual(got, [(0, 0), (5, 5), (7, 10), (11, 14), (12, 15), (12, 15), (12.35, 21.05)])


class Windows(unittest.TestCase):
    ANCHORS = {"tap": [(1.0, 1.0, M, 0), (5.0, 5.0, S, 0)]}

    def test_each_holds_after_every_occurrence_on_its_lens(self):
        rows = [(1.5, M, "rx F5 1E"), (5.5, S, "rx F5 1E")]
        each = asserts.each("tap", g1["recv"](A, "F5 1E"), within=1)
        self.assertTrue(ok(each, rows, anchors=self.ANCHORS))
        self.assertFalse(ok(each, rows[:1], anchors=self.ANCHORS))
        self.assertTrue(ok(asserts.after(("tap", 2), g1["recv"](A, "F5 1E"), within=1), rows, anchors=self.ANCHORS))
        self.assertTrue(ok(asserts.after(("tap", S), g1["recv"](S, "F5 1E"), within=1), rows, anchors=self.ANCHORS))
        self.assertFalse(ok(asserts.after("tap", g1["recv"](A, "F5 1E")), rows, anchors=self.ANCHORS))   # which one?
        self.assertFalse(ok(g1["recv"](A, "F5 1E"), rows))                                # A without an anchor

    def test_never_and_no_audio_from_a_label_on(self):
        rows = [(0.5, M, "rx F5 1E"), (2.0, M, "audio start"), (3.0, M, "audio end")]
        anchors = {"x": [(1.0, 1.0, B, 0)]}
        self.assertTrue(ok(asserts.never("F5 1E", after=("x", 0)), rows, anchors=anchors))
        self.assertFalse(ok(asserts.never("F5 1E"), rows))
        self.assertFalse(ok(asserts.no_audio(M, after=("x", 0)), rows, anchors=anchors))
        self.assertTrue(ok(asserts.no_audio(M, after=("x", 2.5)), rows, anchors=anchors))
        self.assertTrue(ok(asserts.no_audio(S), rows))
        self.assertFalse(ok(asserts.no_audio(M, after=4), rows[:2]))                    # still running at the end

    def test_audio_streams(self):
        rows = [(2.0, M, "audio start"), (5.0, M, "audio end")]
        self.assertTrue(ok(asserts.audio(M, 3), rows, 1.0, 2.5))
        self.assertFalse(ok(asserts.audio(M, 4), rows, 1.0, 2.5))
        self.assertFalse(ok(asserts.audio(M), rows, 3.0, 4.0))
        self.assertTrue(ok(asserts.audio(M, during=True), rows, 3.0, 4.0))
        self.assertFalse(ok(asserts.audio(M, during=True), rows, 6.0, 7.0))
        self.assertTrue(ok(asserts.any_of(asserts.audio(M), asserts.audio(S)), rows, 1.0, 3.0))

    def test_a_stream_has_lasted_to_its_last_packet(self):
        # a line that waits for 1 s of audio must not go on when the last packet is 0.9 s after the first:
        # the stream may end there, and the evaluation after the run would then find it too short
        events = [phone(1.05 + n / 20, M, "rx F1 00") for n in range(19)]           # packets from 1.05 to 1.95 s
        out, _ = play([g1["at"](1), g1["expect"](g1["audio"](M, 1), within=2), g1["wait"](2)], events)
        self.assertIn("no audio stream of 1 s or more", reason(out))
        self.assertNotIn("once the run was over", reason(out))
        out, sim = play([g1["at"](1), g1["expect"](g1["audio"](M, 0.9), within=2), g1["wait"](2)], events)
        self.assertIsNone(out.failure, reason(out))

    def test_what_is_about_a_whole_window_waits_for_its_end(self):
        live = Context(items((0.5, M, "frame " + X)), {}, 2.0, G1.proto)
        open_window = Window(1.0, 2.0, open=True, close=4.0)
        lit = asserts.display(M, asserts.nonblank)
        for a in (asserts.quiet(M, "F5"), asserts.never("F5"), asserts.no_audio(M),
                  asserts.display(M, asserts.unchanged), asserts.together(asserts.quiet(M, "F5"), lit),
                  asserts.known_failure("why", asserts.event(M, "boot app"))):
            r = a.run(live, open_window)
            self.assertTrue(r.pending and not r.ok, a.text)
            self.assertEqual(a.least(3.0), 3.0, a.text)
        self.assertTrue(asserts.any_of(asserts.quiet(M, "F5"), lit).run(live, open_window).ok)
        for a in (asserts.quiet(M, "F5"), asserts.never("F5"), asserts.no_audio(M)):
            self.assertTrue(a.run(live, Window(1.0, 2.0)).ok, a.text)

    def test_also_violated_only_what_is_lost_for_good(self):
        rows = [(1.5, M, "rx F5 1E"), (2.0, M, "fatal: HardFault")]
        anchors = {"tap": [(1.0, 1.0, M, 0)]}
        c = ctx(rows, now=3.0, anchors=anchors, final=False)
        silent = asserts.quiet(M, "F5 1E")
        alone = [asserts.never("fatal"), asserts.never("boot", after=1), asserts.after("tap", silent, within=1),
                 asserts.after("tap", silent, within=9), asserts.after("later", silent)]
        self.assertEqual([r.what.text for r in statements.also_violated(alone, c, 3.0)],
                         ["never(fatal)", "after('tap', within=1): quiet(master, F5 1E)"])


class Arguments(unittest.TestCase):
    def test_a6_the_lens_is_checked(self):
        self.assertRaises(RefError, asserts.quiet, "F5 11", M)
        self.assertRaises(RefError, asserts.never, "F5 11", lens="left")
        self.assertRaises(RefError, asserts.never, "F5 11", M)
        self.assertRaises(RefError, asserts.display, "left", asserts.blank)
        self.assertRaises(RefError, g1["action"], A, TAP)
        self.assertRaises(RefError, g1["check"], "F5", "x")

    def test_a15_nothing_to_assert_is_a_mistake(self):
        for make in (lambda: asserts.after("x"), lambda: asserts.each("x"), asserts.together, asserts.any_of,
                     asserts.sequence, lambda: g1["expect"]()):
            self.assertRaises(RefError, make)

    def test_statements_check_their_arguments(self):
        self.assertRaises(RefError, g1["wait"], -1)
        self.assertRaises(RefError, g1["wait"], "1")
        self.assertRaises(TypeError, g1["wait"])
        self.assertRaises(TypeError, g1["wait"], until=3)
        self.assertRaises(RefError, g1["at"], None)
        self.assertRaises(RefError, g1["mark"], 3)
        self.assertRaises(RefError, g1["action"], M, TAP, within="soon")
        self.assertRaises(RefError, g1["action"], M, TAP, label=3)
        self.assertRaises(RefError, g1["action"], M, [TAP, -1])
        self.assertRaises(RefError, g1["after"], "x", g1["quiet"](M, "F5"), within=-1)
        self.assertRaises(RefError, g1["never"], "F5", after=("x",))

    def test_d5_g1(self):
        self.assertRaises(RefError, g1["recv"], M, "")
        self.assertRaises(RefError, g1["recv"], M, "'")
        self.assertRaises(RefError, g1["phone_on"], os=300)
        self.assertRaises(RefError, g1["send"], "")
        self.assertRaises(RefError, g1["event"], M, "F5 11")


class Patterns(unittest.TestCase):
    def test_a10_a_text_ends_where_a_word_ends(self):
        self.assertTrue(asserts.Starts("fatal").match("fatal: HardFault"))
        self.assertTrue(asserts.Starts("frame ").match("frame abc"))
        self.assertFalse(asserts.Starts("boot").match("booting"))
        self.assertFalse(asserts.Starts("connected").match("disconnected: x"))

    def test_a10_a_query_is_its_whole_command(self):
        rows = [(1.0, M, "?foo bar = 1")]
        self.assertFalse(ok(asserts.state("foo", equals="1", lens=M), rows))
        self.assertTrue(ok(asserts.state("foo bar", equals="1", lens=M), rows))
        self.assertRaises(RefError, asserts.state, "foo")
        self.assertRaises(RefError, asserts.state, "foo", equals="1", contains="1")

    def test_a10_a_stream_and_its_end(self):
        rows = [(1.0, M, "audio start"), (3.0, M, "audio end")]
        self.assertTrue(ok(asserts.event(M, "audio start"), rows))
        self.assertEqual(len(asserts.ignore("audio").run(ctx(rows), Window(0, 10)).matched), 2)

    def test_hex_patterns(self):
        p = asserts.pattern_of("2b 69 .. 06", "x")
        self.assertTrue(p.match("rx 2B 69 00 06 01"))
        self.assertTrue(p.match("rx 2B 69 .. 06"))
        self.assertFalse(p.match("rx 2B 69 00 07"))
        self.assertFalse(p.match("tx 2B 69 00 06"))
        self.assertFalse(asserts.pattern_of("F5 1", "x").match("rx F5 11"))
        self.assertTrue(asserts.pattern_of(asserts.any_of("F5 11", "boot"), "x").match("boot app"))
        self.assertRaises(RefError, asserts.pattern_of, 5, "x")
        self.assertRaises(RefError, asserts.pattern_of, " ", "x")

    def test_a11_a_g1_packet_is_text_only_when_it_is_a_text_reply(self):
        item = G1.proto.item
        self.assertEqual(item("rx 2B 69 00"), "rx 2B 69 00")
        self.assertEqual(item("rx 25 00 00 00"), "rx 25 00 00 00")
        self.assertEqual(item("rx 32 6D 00 00"), "rx 32 6D 00 00")
        self.assertEqual(item("rx 37 37 28 00 00 00"), "rx 37 37 .. .. .. ..")
        self.assertEqual(item("rx 62 61 73 65 3A 20 30 00 00"), "rx 'base: 0' (9 B)")
        self.assertEqual(item("rx 42 3A 38 37 2C 20 56 3A 33 38 39"), "rx 'B:.., V:389' (11 B)")
        self.assertEqual(item("rx F1 01 02"), "audio")
        self.assertIsNone(item("rx F5 0A 64"))
        self.assertEqual(item("pairing: started"), "pairing: started")
        self.assertTrue(ok(g1["recv"](M, "37 37"), [(40.0, M, item("rx 37 37 28 00 00 00"))], end=50, now=50))
        self.assertTrue(ok(g1["recv"](M, "'base: 0'"), [(1.0, M, item("rx 62 61 73 65 3A 20 30 00"))]))

    def test_d5_text_packets_are_quoted_one_way(self):
        self.assertEqual(G1.proto.item("rx 62 61 73 65 3A 20 27 22 0D 0A"), "rx 'base: \\'\"\\r\\n' (10 B)")

    def test_a14_a_line_the_protocol_does_not_know_is_an_error(self):
        self.assertRaises(trace.Unread, G1.proto.item, "rx F51E")

    def test_a14_the_protocols_know_every_line_of_their_phone_model(self):
        models = os.path.join(driver.SIM, "models")
        if not os.path.isdir(models):
            self.skipTest("tools/simulator is not checked out")
        said = {}
        for name in ("BLE_VirtualCentral.cs",):
            with open(os.path.join(models, name), encoding="utf-8") as f:
                source = f.read()
            calls = re.findall(r'\bSay\(\s*((?:"(?:[^"\\]|\\.)*"\s*\+?\s*)+)', source)
            said[name] = {"".join(re.findall(r'"((?:[^"\\]|\\.)*)"', call)) for call in calls}
            said[name].discard("{0}{1}")
            self.assertEqual(len(re.findall(r"\bSay\(", source)), len(calls) + (name == "BLE_VirtualCentral.cs"), name)
        self.assertEqual(set(G1.proto.SAYS), said["BLE_VirtualCentral.cs"])

    def test_a14_a_line_of_the_phone_that_is_not_in_its_list(self):
        self.assertRaises(trace.Unread, G1.proto.item, "link lost: a new wording")
        for line in ("disconnected: the lens stopped answering", "Nordic UART Service: write 0x0015, notify 0x0012, "
                     "notifications on", "ATT MTU 251", "tx 4D FB"):
            self.assertEqual(G1.proto.item(line), line)

    def test_a14_a_text_no_item_starts_with(self):
        for text in ("disconnected", "pairing: complete", "boot", "fatal", "frame ", "audio", "rx 'base",
                     "log sync to slave failed", "Nordic UART Service: write", "?sysbus.twim2.touch"):
            self.assertTrue(G1.proto.begins(text), text)
        for text in ("disconected", "boo", "Nordic UART Service", "auth: OK", "ack 09", "booting"):
            self.assertFalse(G1.proto.begins(text), text)

    def test_a14_a_run_must_show_the_power_on_boots(self):
        out, _ = play([g1["wait"](2)], boots=False)
        self.assertIn("no 'boot app' from master", reason(out))


class Order(unittest.TestCase):
    def test_a9_one_item_is_one_step(self):
        rows = [(1.0, M, "rx F5 11")]
        self.assertFalse(ok(asserts.sequence(g1["recv"](M, "F5 11"), g1["recv"](M, "F5 11")), rows))
        self.assertTrue(ok(asserts.sequence(g1["recv"](M, "F5 11"), g1["recv"](M, "F5 11")),
                           rows + [(1.0, M, "rx F5 11")]))

    def test_a9_at_least_takes_the_first(self):
        rows = [(1.0, M, "rx AA"), (2.0, M, "rx BB"), (5.0, M, "rx AA")]
        self.assertTrue(ok(asserts.sequence(g1["recv"](M, "AA", asserts.at_least(1)), g1["recv"](M, "BB")), rows))
        self.assertFalse(ok(asserts.sequence(g1["recv"](M, "BB"), g1["recv"](M, "AA"), g1["recv"](M, "BB")), rows))

    def test_a9_an_exact_count_is_up_to_the_next_step(self):
        rows = [(1.0, M, "rx AA"), (2.0, M, "rx BB"), (5.0, M, "rx AA")]
        seq = asserts.sequence(g1["recv"](M, "AA"), g1["recv"](M, "BB"))
        self.assertTrue(ok(seq, rows))                              # the AA after BB is not this step's
        self.assertFalse(ok(seq, rows + [(1.5, M, "rx AA")]))       # two AA before BB, wanted one
        self.assertFalse(ok(seq, rows + [(6.0, M, "rx BB")]))       # the last step: to the window's end
        r = seq.run(ctx(rows), Window(0, 10))
        self.assertEqual([i.t for i in r.matched], [1.0, 2.0])

    def test_a9_arguments(self):
        self.assertRaises(RefError, asserts.sequence, "x", "y")
        self.assertRaises(RefError, asserts.sequence, g1["recv"](M, "AA"))
        self.assertRaises(RefError, asserts.sequence, g1["recv"](M, "AA"), asserts.quiet(M, "BB"))
        self.assertRaises(RefError, asserts.sequence, g1["recv"](M, "AA"), asserts.display(M, asserts.blank))

    def test_a9_both_lenses_each_in_its_own_order(self):
        rows = [(1.0, M, "rx AA"), (2.0, S, "rx AA"), (3.0, M, "rx BB"), (4.0, S, "rx BB")]
        seq = asserts.sequence(g1["recv"](B, "AA"), g1["recv"](B, "BB"))
        self.assertTrue(ok(seq, rows))
        self.assertFalse(ok(seq, rows[:3]))


class Loading(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="reg-unit-suite-")
        shutil.copy(os.path.join(HERE, "g1", "suite.py"), self.dir)
        os.makedirs(os.path.join(self.dir, "packets"))
        os.makedirs(os.path.join(self.dir, "common"))
        os.makedirs(os.path.join(self.dir, "link"))
        self.write("packets/two.hex", "01 02  # the first\n03 04\n")
        self.write("frames.txt", "home 0123456789ab\n")

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, name, text):
        with open(os.path.join(self.dir, name), "w", encoding="utf-8") as f:
            f.write(text)

    def load(self, text):
        self.write("link/t.ref", text)
        return loader.Suite(self.dir).load("link/t")

    def error(self, text):
        with self.assertRaises(loader.LoadError) as c:
            self.load(text)
        return str(c.exception)

    def test_names_are_paths_below_the_suite(self):
        self.write("link/pair.ref", "wait(1)\n")
        self.write("top.ref", "wait(1)\n")
        self.write("common/shared.ref", "wait(1)\n")
        suite = loader.Suite(self.dir)
        self.assertEqual(suite.tests(), ["link/pair", "top"])
        self.assertEqual(runner.select(suite.tests(), ["link/"]), (["link/pair"], []))
        self.assertEqual(runner.select(suite.tests(), ["=top"]), (["top"], []))
        self.assertEqual(runner.select(suite.tests(), ["pai"]), (["link/pair"], []))

    def test_b3_a_key_that_selects_nothing(self):
        names = ["boot/init", "link/pair"]
        self.assertEqual(runner.select(names, ["nosuchtest"]), ([], ["nosuchtest"]))
        self.assertEqual(runner.select(names, ["boot", ""]), (["boot/init"], [""]))
        self.assertEqual(runner.select(names, ["="]), ([], ["="]))
        self.assertEqual(runner.select(names, []), (names, []))

    def test_b6_every_assert_knows_its_line(self):
        base = os.path.basename(self.dir)
        loaded = self.load("# a test\naction(M, TAP,\n       recv(M, 'F5 1E'),\n       label='tap')\n"
                           "after('tap', quiet(M, 'F5 02'))\nlink_up()\naction(B, phone_on(), label='phone_on')\n")
        self.assertEqual(loaded[0].where, "%s/link/t.ref:2" % base)
        self.assertEqual(loaded[0].asserts[0].where, "%s/link/t.ref:3" % base)
        self.assertEqual(loaded[1].where, "%s/link/t.ref:5" % base)
        self.assertEqual(loaded[2].inner()[0].where, "%s/link/t.ref:6" % base)       # made inside link_up()

    def test_b6_a_failure_names_the_line(self):
        loaded = self.load("at(1)\naction(M, TAP,\n       recv(M, 'F5 1E'), within=1)\n")
        out, _ = play(loaded)
        self.assertIn("link/t.ref:3: recv(master, F5 1E)  (in action(", runner.located(out.failure, out.line))
        self.assertIn("link/t.ref:2)", runner.located(out.failure, out.line))

    def test_b7_whatever_a_line_does_wrong_is_a_load_error_with_its_line(self):
        for line in ('sequence("x")', 'at_least("x")', 'action(M, send("zz"))', "phone_on(os=300)",
                     'known_failure("r", never("F5"), seeds=("a",))', '-"x"', "include(3)", "wait()", "wait(until=3)",
                     'recv(M, "F5", count=None)', "expect()", "nosuch(1)", "M.x", "1 if 1 else 2", "x = 1",
                     'action(M, b"\\x01")', "wait(*[1])", "{1: 2, 1: 3}", "{[1]: 2}", 'include("nosuch")', "5",
                     'buf("two", 3)', 'buf("none", 1)', 'buf("two", "1")', 'display(M, shows("nosuch"))',
                     'never("disconected")', 'quiet(M, any_of("boot", "fatl"))'):
            message = self.error("wait(1)\n" + line + "\n")
            self.assertIn("link/t.ref:2", message, line)

    def test_b7_parse_goes_on_after_a_bad_file(self):
        self.write("link/bad.ref", 'sequence("x")\n')
        self.write("link/good.ref", "wait(1)\n")
        suite = loader.Suite(self.dir)
        with contextlib.redirect_stdout(io.StringIO()) as said:
            self.assertEqual(runner.parse(suite, suite.tests(), False), 1)
        self.assertIn("ERROR link/bad", said.getvalue())
        self.assertIn("ok    link/good", said.getvalue())

    def test_b8_a_label_must_be_defined(self):
        tap = "action(M, TAP, label='tap')\n"
        self.assertIn("names the label 'tpa'", self.error(tap + "after('tpa', quiet(M, 'F5'))\n"))
        self.assertIn("names the label 'tap'", self.error("expect(after('tap', recv(M, 'F5')))\n" + tap))
        self.assertIn("'phone_on'", self.error("link_up()\n"))
        self.assertIn("occurrence 2", self.error("action(M, TAP, label='tap')\nafter(('tap', 2), quiet(M, 'F5'))\n"))
        self.assertIn("defined 2 times", self.error("mark('m')\nmark('m')\nnever('F5', after=('m', 1))\n"))
        self.assertIn("names the label 'x'", self.error("mark('m')\nholds(display(M, same_as('x')))\n"))
        self.load("mark('m')\nmark('m')\neach('m', quiet(M, 'F5'))\nafter(('m', 2), quiet(M, 'F5'))\n")

    def test_b8_unused_labels(self):
        suite = loader.Suite(self.dir)
        self.write("link/t.ref", "action(M, TAP, label='tap')\nmark('used')\nafter('used', quiet(M, 'F5'))\n")
        self.assertEqual([label for _, label in suite.unused_labels(suite.load("link/t"))], ["tap"])

    def test_includes(self):
        self.write("common/shared.ref", "at(3)\nnever('fatal')\n")
        loaded = self.load("include('common/shared')\nat(5)\n")
        self.assertEqual([getattr(x, "included", None) for x in loaded], [True, None, False])
        self.assertTrue(loaded[0].where.endswith("common/shared.ref:1"))
        self.write("common/loop.ref", "include('common/loop')\n")
        self.assertIn("includes itself", self.error("include('common/loop')\n"))

    def test_b10_two_suites_keep_their_own_buffers(self):
        other = tempfile.mkdtemp(prefix="reg-unit-suite-")
        try:
            shutil.copy(os.path.join(HERE, "g1", "suite.py"), other)
            os.makedirs(os.path.join(other, "packets"))
            with open(os.path.join(other, "packets", "two.hex"), "w") as f:
                f.write("AA\nBB\n")
            first, second = loader.Suite(self.dir), loader.Suite(other)
            self.assertEqual(second.names["buf"]("two", 1), b"\xAA")
            self.assertEqual(first.names["buf"]("two", 1), b"\x01\x02")
            self.assertEqual(first.names["buf"]("two", 1, 2), [b"\x01\x02", b"\x03\x04"])
        finally:
            shutil.rmtree(other)

    def test_named_frames(self):
        self.load("holds(display(M, settles_to('home')))\n")
        for text, why in (("blank 0123456789ab\n", "built in"), ("home 0123\n", "want 'name sha12'"),
                          ("a 0123456789ab\na 0123456789ab\n", "there twice")):
            self.write("frames.txt", text)
            with self.assertRaises(loader.LoadError) as c:
                loader.Suite(self.dir).frames()
            self.assertIn(why, str(c.exception))

    def test_d6_a_protocol_says_which_names_it_replaces(self):
        with open(os.path.join(self.dir, "suite.py"), "a") as f:
            f.write("\nG1.NAMES = dict(G1.NAMES, quiet=recv)\n")
        with self.assertRaises(loader.LoadError) as c:
            loader.Suite(self.dir)
        self.assertIn("quiet", str(c.exception))


class Driver(unittest.TestCase):
    def test_a16_the_two_clocks_must_agree(self):
        session = driver.Session("/nonexistent", "fw.zip", "renode", 1)
        session.rails = lambda: driver.RAILS.match("panel-rails 111 000 1.000000")
        session.now = 1.0
        self.assertEqual(session.look(), [(M, True), (S, False)])
        self.assertEqual(session.look(), [])
        session.now = 1.05
        self.assertRaises(driver.DriverError, session.look)

    def test_b1_a_session_that_never_started_can_be_killed(self):
        session = driver.Session("/nonexistent", "fw.zip", "renode", 1)
        session.kill()
        session.stop()
        self.assertTrue(session.killed.is_set())

    def test_b12_the_console_is_the_monitor_alone(self):
        self.assertTrue(driver.PROMPT.match('(g1) echo "@@1@@"'))
        self.assertTrue(driver.PROMPT.match("(g1_slave) sysbus.twim2.touch ConfigWriteCount"))
        self.assertFalse(driver.PROMPT.match("(empty)"))
        self.assertFalse(driver.PROMPT.match("0x0000002D"))


class Jobs(unittest.TestCase):
    """run.Job with a session that cannot start: what the runner makes of each kind of error."""

    def job(self, error):
        class Broken(driver.Session):
            def start(self):
                raise error

        real, out = driver.Session, tempfile.mkdtemp(prefix="reg-unit-")
        driver.Session = Broken
        try:
            options = argparse.Namespace(out=out, firmware="fw.zip", renode="renode", step=0.05)
            job = runner.Job(G1, "boot/init", 7, options)
            job.thread.join(timeout=30)
            return job, job.finish()
        finally:
            driver.Session = real
            shutil.rmtree(out, ignore_errors=True)

    def test_b2_an_error_of_the_engine_comes_with_its_traceback(self):
        job, (status, lines, _) = self.job(ZeroDivisionError("a bug"))
        self.assertEqual(status, "FAIL")
        self.assertIn("Traceback (most recent call last)", lines[0])
        self.assertIn("ZeroDivisionError: a bug", lines[-1])
        self.assertTrue(any("in start" in line for line in lines))
        self.assertFalse(job.stalled)

    def test_b5_a_stall_is_known_by_its_class(self):
        job, (status, lines, _) = self.job(driver.Stalled("no answer from Renode within 600 s"))
        self.assertTrue(job.stalled)
        self.assertEqual((status, lines), ("FAIL", ["      no answer from Renode within 600 s"]))
        job, (status, lines, _) = self.job(driver.DriverError("Renode exited"))
        self.assertFalse(job.stalled)

    def test_b4_a_run_directory_under_out_is_named_after_the_run(self):
        job, _ = self.job(driver.DriverError("x"))
        self.assertEqual(os.path.basename(job.out), "reg-g1-boot-init-7")


class Pictures(unittest.TestCase):
    def test_c5_a_picture_is_read_back_as_it_was_written(self):
        fb = bytes((n * 7 + n // 320) % 256 for n in range(trace.FB_BYTES))
        with tempfile.TemporaryDirectory() as tmp:
            trace.write_png(os.path.join(tmp, "x.png"), fb)
            self.assertEqual(trace.read_png(os.path.join(tmp, "x.png")), fb)
        self.assertEqual(trace.sha12(bytes(trace.FB_BYTES)), BLANK)

    def test_c5_every_reference_picture_is_the_content_its_name_stands_for(self):
        for suite in (G1,):
            for path in sorted(glob.glob(os.path.join(suite.dir, "frames", "*"))):
                name = os.path.basename(path)[:-4]
                self.assertTrue(path.endswith(".png") and name in suite.frames(), "%s: not in frames.txt" % path)
                self.assertEqual(trace.sha12(trace.read_png(path)), suite.frames()[name], path)


class Style(unittest.TestCase):
    def test_lines_are_120_columns_at_most(self):
        files = [p for pattern in ("*.py", "engine/*.py", "g1/*.py", "g1/packets/*.py")
                 for p in glob.glob(os.path.join(HERE, pattern))]
        self.assertGreater(len(files), 15)
        long = []
        for path in sorted(files):
            with open(path, encoding="utf-8") as f:
                long += ["%s:%d (%d)" % (os.path.relpath(path, HERE), n, len(line.rstrip("\n")))
                         for n, line in enumerate(f, 1) if len(line.rstrip("\n")) > 120]
        self.assertEqual(long, [])


if __name__ == "__main__":
    unittest.main()
