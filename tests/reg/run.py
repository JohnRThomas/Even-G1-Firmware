#!/usr/bin/env python3
"""Regression tests: run each on the simulator and check what the glasses do against what the app expects.

    python3 tests/reg/run.py ZIP [--suite g1] [-k KEY,...] [-j N] [--seed N | --seeds N,M,...]
                    [--out DIR] [--keep] [--trace]
    python3 tests/reg/run.py --parse [--times] [--suite g1] [-k KEY,...]

--suite names the directory of tests to run, tests/reg/g1 (the default). A test is a .ref file in a
feature directory of it, and its name is the path below the suite: link/pair is tests/reg/g1/link/pair.ref. Beside
the tests the directory holds what they read (common/ for include(), frames.txt and frames/, packets/), the suite's
suite.py and its notes. g1 is the G1 app's protocol. The engine (tests/reg/engine) knows no protocol: the one the
simulator's virtual phone speaks is the directory's suite.py (engine/protocol.py), which says what a test sends and
what it asserts of the answers. A copy of a suite elsewhere runs too (--suite /path/to/g1).

-k selects tests: link/ is every test of that directory, =link/pair that test, anything else every test whose
name contains it. A key that selects nothing is an error.

ZIP, the positional argument (before or after the options), names the update package under test (a
dfu_application_*.zip); there is no default, so every result says which build it is about. Only --parse, which
does not start the simulator, runs without it.

A test file (engine/loader.py) is statements that drive the simulator line by line (send an input to a lens, run
until the asserts about what comes back hold; engine/statements.py, engine/driver.py), and asserts over the whole
run (engine/asserts.py). A run ends in one of four ways:

    PASS    every assert held
    FAIL    an assert did not hold, or the run could not be made (an error of the file, the simulator or the
            engine; the engine's own errors come with their traceback). The run stops at the first line whose
            asserts do not hold; the asserts over the whole run that were violated by then as well are listed
            under it ("also violated")
    KNOWN   the only asserts that did not hold are marked known_failure("reason", assert) in the file. This
            does not fail the suite
    XPASS   an assert marked as a known failure held: the mark is out of date. This fails the suite

Under a result there can be "note:" lines: an at() that had already passed when the file reached it, a run that
went on after its last line because a window was still open, a window the run did not cover.

Each run draws a random seed for Renode's pseudo-random number generator, which decides the radio schedule;
the seed is on every result line, --seed N repeats a run and --seeds N,M,... runs each test under each.
Every run has a directory, reg-SUITE-TEST-SEED: under --out DIR, else in the temporary directory. A run that
did not pass keeps it: Renode's log (log.txt), the monitor's console (monitor.txt), the frames, trace.txt and a
PNG of each panel content the failing assert looked at; --keep keeps the directories of passed runs too,
--trace prints each run's trace. $REG_STEP sets the seconds the emulation runs between two looks at the trace
(0.05).

--parse only loads the test files and reports their errors and the labels no assert uses, without the simulator;
--times adds when each line of each file begins at the earliest and at the latest, to hold a file's comments
against its statements.
"""
import argparse
import os
import secrets
import shutil
import sys
import tempfile
import threading
import time
import traceback

sys.dont_write_bytecode = True

from engine import driver, loader, statements, trace              # noqa: E402
from engine.base import RefError, fmt                             # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
LIMIT = 1800                                # seconds of real time a run may take


def length(loaded):
    """Simulated seconds a test runs at least, for the progress estimate."""
    return max(statements.timeline(loaded)[-1][1], 10.0)


class Job:
    """A run of a test under a seed, in a thread: the statements drive a driver.Session."""

    def __init__(self, suite, name, options):
        self.suite = suite
        self.name = name
        self.seed = options.seed
        self.options = options
        stem = "reg-%s-%s-%d" % (suite.name, name.replace("/", "-"), self.seed)
        if options.out:
            self.out = os.path.join(options.out, stem)
            shutil.rmtree(self.out, ignore_errors=True)
            os.makedirs(self.out)
        else:
            self.out = tempfile.mkdtemp(prefix=stem + "-")
        self.began = time.time()
        self.outcome = None
        self.error = None                       # why the run could not be made
        self.stalled = False                    # Renode did not answer: the run can be made again
        self.items = []
        self.session = None
        self.live = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            loaded, proto = self.suite.load(self.name), self.suite.proto
            self.session = driver.Session(self.out, self.options.firmware, self.options.renode, self.seed,
                                          statements.needs_phone(loaded, proto), proto.SETUP)
            self.session.start()
            self.live = trace.LiveTrace(self.out, proto.item)
            self.outcome = statements.run(loaded, self.session, self.live, proto, self.suite.frames(), self.seed)
            self.items = self.outcome.items
        except driver.Stalled as e:
            self.error, self.stalled = str(e), True
            self.session.kill()
        except (loader.LoadError, RefError, driver.DriverError, trace.Unread) as e:
            self.error = "%s: %s" % (self.name, e) if isinstance(e, RefError) else str(e)
        except Exception:                       # a bug of the engine: reported with where it is
            self.error = traceback.format_exc().rstrip()
        finally:
            if self.session:
                self.session.stop()
            if self.outcome is None and self.live:      # what the run had seen when it broke off
                try:
                    self.items = self.live.items(self.session.now, final=True)
                except Exception:
                    pass

    def done(self):
        return not self.thread.is_alive()

    def kill(self):
        if self.session:
            self.session.kill()
        self.thread.join(timeout=60)

    def finish(self):
        """(status, lines, items looked at) of the run."""
        notes = ["      note: " + n for n in (self.outcome.notes if self.outcome else ())]
        if self.error is not None:
            return "FAIL", ["      " + line for line in self.error.split("\n")] + notes, self.items
        failure = self.outcome.failure
        if failure is not None:
            lines = ["      at %s" % located(failure, self.outcome.line)] + failure.detail
            for a in self.outcome.also:         # the other asserts violated by then: often the cause
                lines += ["      also violated: %s" % located(a)] + a.detail
            looked = failure.matched + [m for a in self.outcome.also for m in a.matched]
            return "FAIL", lines + self.pictures(failure.what) + notes, looked or self.items
        if self.outcome.passed:
            lines = ["      %s: holds, and is marked a known failure: take the mark out (%s)" % (m.where, m.text[:90])
                     for m in self.outcome.passed]
            return "XPASS", lines + notes, self.items
        if self.outcome.known:
            lines, looked = [], []
            for mark, result in self.outcome.known:
                lines.append("      known: %s (%s: %s)" % (mark.reason, mark.where, mark.assert_.text[:90]))
                lines += result.detail
                looked += result.matched
            return "KNOWN", lines + notes, looked or self.items

        return "PASS", notes, self.items

    def pictures(self, failed):
        """Lines that say where the reference pictures of the panel contents a failing assert names are."""
        lines = []
        for name in sorted({frame for a in loader.walk(failed) for frame in a.frames()}):
            path = os.path.join(self.suite.dir, "frames", name + ".png")
            if os.path.isfile(path):
                lines.append("      %s as it should look: %s" % (name, os.path.relpath(path)))
        return lines

    def trace_text(self):
        """The trace as text, for authoring a test file (the protocol's show() says how an item is written out)."""
        return "\n".join(["# %s, seed %d" % (self.name, self.seed)] + [
            "%8.3f %-6s %s" % (i.t, i.lens, self.suite.proto.show(i.text)) for i in self.items]) + "\n"


def located(result, line=None):
    """A failing result's assert, with the file and line it is at and the line of the file it stands in."""
    what = result.what
    text = "%s: %s" % (what.where, what.text) if what.where else what.text
    if line is not None and line is not what:
        text += "  (in %s%s)" % (line.text[:70], "" if line.where in (None, what.where) else ", %s" % line.where)
    return text


# -------------------------------------------------------------------------------------------------- progress


def clock(seconds):
    return "%d:%02d" % divmod(int(seconds), 60)


class Progress:
    """An ASCII progress bar on the last line of a terminal: the runs done, passed and failed, the runs going, the
    time so far and an estimate of the time left. Nothing is drawn when stdout is not a terminal."""

    def __init__(self, seconds, slots):
        self.tty = sys.stdout.isatty()
        self.seconds = seconds                                     # {job: simulated seconds of the run}
        self.total = len(seconds)
        self.slots = slots
        self.began = time.time()
        self.done = 0
        self.failed = 0
        self.drawn = 0
        self.wall = 0.0                                            # real seconds of the runs done
        self.simulated = 0.0                                       # their simulated seconds

    def finish(self, key, began, failed):
        self.done += 1
        self.failed += failed
        self.wall += time.time() - began
        self.simulated += self.seconds[key]

    def left(self, queued, running):
        """Seconds until the last run ends, at the real time per simulated second of the runs done so far."""
        if not self.simulated:
            return None
        rate, now = self.wall / self.simulated, time.time()
        going = [max(0.0, rate * self.seconds[key] - (now - job.began)) for key, job in running.items()]
        return max(max(going, default=0.0), (rate * sum(self.seconds[key] for key in queued) + sum(going)) / self.slots)

    def draw(self, queued, running):
        if not self.tty:
            return
        left = self.left(queued, running)
        text = " %d/%d  %d passed  %d failed  %d running  %s%s" % (
            self.done, self.total, self.done - self.failed, self.failed, len(running), clock(time.time() - self.began),
            "  ~%s left" % clock(left) if left is not None else "")
        width = shutil.get_terminal_size().columns - 1
        size = max(10, width - len(text) - 2)
        fill = size * self.done // max(self.total, 1)
        line = ("[" + "#" * fill + "-" * (size - fill) + "]" + text)[:width]
        sys.stdout.write("\r" + line.ljust(self.drawn))
        sys.stdout.flush()
        self.drawn = len(line)

    def clear(self):
        if self.drawn:
            sys.stdout.write("\r" + " " * self.drawn + "\r")
            sys.stdout.flush()
            self.drawn = 0


# ------------------------------------------------------------------------------------------------------ main


def select(names, keys):
    """The tests the -k keys name: (names, the keys that name none)."""
    if not keys:
        return names, []

    def hit(key, name):
        if not key.strip("="):
            return False
        if key.startswith("="):
            return name == key[1:]
        return name.startswith(key) if key.endswith("/") else key in name

    return [n for n in names if any(hit(k, n) for k in keys)], [k for k in keys if not any(hit(k, n) for n in names)]


def parse(suite, names, times):
    """--parse: load every test file without running it; 1 if any has an error or a label no assert uses."""
    bad = unused = 0
    width = max(map(len, names))
    for name in names:
        try:
            loaded = suite.load(name)
        except (loader.LoadError, RefError) as e:
            bad += 1
            print("ERROR %-*s %s" % (width, name, e))
            continue
        line = statements.timeline(loaded)
        print("ok    %-*s %3d line(s), %4.0f to %4.0f s" % (width, name, len(loaded), line[-1][1], line[-1][2]))
        for where, label in suite.unused_labels(loaded):
            unused += 1
            print("      %s: the label %r is not used by any assert" % (where, label))
        for st, early, late in line[:-1] if times else ():
            print("      %8s %8s  %-28s %s" % (fmt(early), fmt(late), st.where, st.text[:90]))
    print("%s: %d file(s), %d with errors%s" % (suite.name, len(names), bad,
                                               ", %d label(s) not used" % unused if unused else ""))
    return 1 if bad or unused else 0


def arguments():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--suite", default="g1", metavar="g1",
                        help="the directory of tests to run, tests/reg/SUITE (default g1), or a path to a copy of one")
    parser.add_argument("-k", metavar="KEY,...", help="only the tests whose name contains one of these (=NAME: "
                                                      "exactly that test, DIR/: the tests of that directory)")
    parser.add_argument("-j", type=int, default=1, metavar="N", help="simulators at once (default 1)")
    parser.add_argument("firmware", nargs="?", metavar="ZIP", help="update package to test (required, except with --parse)")
    parser.add_argument("--seed", type=int, metavar="N", help="Renode's PRNG seed for every run (default: random per run)")
    parser.add_argument("--out", metavar="DIR", help="put the runs' directories under DIR (default: the temporary "
                                                     "directory), named reg-SUITE-TEST-SEED")
    parser.add_argument("--keep", action="store_true", help="keep the directories of passed runs too")
    parser.add_argument("--trace", action="store_true", help="print each run's trace (and keep its directory)")
    parser.add_argument("--parse", action="store_true",
                        help="only load the test files and report their errors (no simulator)")
    parser.add_argument("--times", action="store_true", help="with --parse: when each line begins, at the earliest "
                                                             "and at the latest")
    args = parser.parse_args()
    try:
        suite = loader.Suite(os.path.normpath(os.path.join(HERE, args.suite)))
        suite.frames()
        names, missed = select(suite.tests(), args.k.split(",") if args.k else [])
    except loader.LoadError as e:
        parser.error("--suite %s: %s" % (args.suite, e))
    if missed or not names:
        parser.error("-k %s: no test of %s matches %s" % (args.k, suite.name, ", ".join(map(repr, missed)) or "it"))
    if args.parse:
        return args, suite, names
    if args.times:
        parser.error("--times goes with --parse")
    if not args.firmware or not os.path.isfile(args.firmware):
        parser.error("ZIP is required: the update package to test%s" % (
            " (no file %s)" % args.firmware if args.firmware else ""))
    local = os.path.join(driver.SIM, ".renode", "renode.exe" if os.name == "nt" else "renode")
    args.renode = os.environ.get("RENODE") or (local if os.path.isfile(local) else shutil.which("renode"))

    if not args.renode:
        parser.error("no Renode: run tools/simulator/setup.sh or setup.bat, or set $RENODE")

    if not args.seed:
        args.seed = secrets.randbelow(2 ** 31 - 1) + 1

    if args.j < 1:
        parser.error("-j must be more than 0")

    args.firmware = os.path.abspath(args.firmware)

    if args.out:
        args.out = os.path.abspath(args.out)
        os.makedirs(args.out, exist_ok=True)
    return args, suite, names


def repeat(args, name, seed):
    """The command line that runs one run again, from where this one was started."""
    return "python3 %s %s --suite %s -k =%s --seed %d%s" % (
        os.path.relpath(os.path.abspath(sys.argv[0])), os.path.relpath(args.firmware), args.suite, name, seed,
        " --out %s" % os.path.relpath(args.out) if args.out else "")


def main():
    args, suite, tests = arguments()

    print(f"Testing {os.path.basename(args.firmware)} against suite {suite.name}, {len(tests)} test(s), {args.j} worker(s)", flush=True)

    if args.parse:
        return parse(suite, tests, args.times)

    seconds = {}
    for test in tests:
        try:
            seconds[test] = length(suite.load(test))
        except (loader.LoadError, RefError):
            seconds[test] = 40.0                  # the run reports the error
    progress, width = Progress(seconds, args.j), max(map(len, tests))
    running = {}                                # {(test, seed): Job}
    count = {"PASS": 0, "KNOWN": 0, "XPASS": 0, "FAIL": 0}
    failed = []                                 # the (test, seed) that fail the suite
    retried = set()
    try:
        while tests or running:
            while tests and len(running) < args.j:
                test = tests.pop(0)
                running[test] = Job(suite, test, args)
            time.sleep(0.5)
            for test, job in list(running.items()):
                if not job.done() and time.time() < job.began + LIMIT:
                    continue
                if not job.done():
                    job.kill()
                    job.error = "the run did not finish within %d minutes" % (LIMIT // 60)
                del running[test]
                if job.stalled and test not in retried:      # Renode stalled (rare): the run once more
                    retried.add(test)
                    tests.insert(0, test)
                    progress.clear()
                    print("RETRY %-*s seed %-10d %s: the run is started again" % (width, test, args.seed, job.error),
                          flush=True)
                    shutil.rmtree(job.out, ignore_errors=True)
                    continue
                status, lines, looked = job.finish()
                count[status] += 1
                keep = status != "PASS" or args.keep or args.trace
                progress.clear()
                print("  %-*s %-5s %4.0fs %s" % (
                    width+4, test, status, time.time() - job.began, "   " + job.out if keep else ""))
                for line in lines:
                    print(line)
                if args.trace:
                    print(job.trace_text(), end="")
                sys.stdout.flush()
                if keep:
                    with open(os.path.join(job.out, "trace.txt"), "w", encoding="utf-8") as f:
                        f.write(job.trace_text())
                    trace.png(job.out, looked)
                else:
                    shutil.rmtree(job.out, ignore_errors=True)
                if status in ("FAIL", "XPASS"):
                    failed.append(test)
                progress.finish(test, job.began, status in ("FAIL", "XPASS"))
            progress.draw(tests, running)
    except KeyboardInterrupt:
        progress.clear()
        print(suite.name + ": interrupted")
        return 130
    finally:
        for job in running.values():
            job.kill()
            if not args.keep:
                shutil.rmtree(job.out, ignore_errors=True)
    progress.clear()
    print("%d run(s) in %s: %d passed, %d known failure(s), %d unexpected pass(es), %d failed, seed: %-10d" % (
        sum(count.values()), clock(time.time() - progress.began), count["PASS"], count["KNOWN"], count["XPASS"],
        count["FAIL"], args.seed))
    for test in sorted(failed):
        print("       " + repeat(args, test, args.seed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
