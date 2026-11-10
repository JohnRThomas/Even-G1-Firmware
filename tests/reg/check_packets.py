#!/usr/bin/env python3
"""Checks that the packet buffers of the suites are what their generators write: runs every packets/gen_*.py of a
suite into a temporary directory and compares the result with the .hex files of the suite's packets directory.

    python3 tests/reg/check_packets.py [SUITE_DIRECTORY ...]     (default: every directory here with packets/gen_*.py)

Prints a line for each buffer that differs from what its generator writes, that a generator writes and is not
there, or that is there and no generator writes, then a line for each suite, and exits 1 when there is such a buffer.
A buffer made by hand belongs in HAND_MADE with where its content comes from: it is listed with that and not
compared. Each generator runs as its own process (python3 gen_x.py DIRECTORY), so those of two suites may have the
same file name."""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HAND_MADE = {}      # "g1/packets/NAME.hex": where its content comes from. None so far: every buffer has a generator


def suites(args):
    """The suite directories to check: those given, or every directory here that has packet generators."""
    found = [os.path.abspath(a) for a in args] or sorted(
        {os.path.dirname(os.path.dirname(p)) for p in glob.glob(os.path.join(HERE, "*", "packets", "gen_*.py"))})
    if not found:
        sys.exit("%s: no suite with packets/gen_*.py there" % os.path.relpath(HERE))
    for suite in found:
        if not glob.glob(os.path.join(suite, "packets", "gen_*.py")):
            sys.exit("%s: no packets/gen_*.py there" % os.path.relpath(suite))
    return found


def generate(packets, tmp):
    """Runs every generator of the packets directory into its own directory under tmp. Returns what they wrote,
    {file name: (generator, path)}, and the problems there were, as lines."""
    made, problems = {}, []
    for path in sorted(glob.glob(os.path.join(packets, "gen_*.py"))):
        generator = os.path.basename(path)
        out_dir = os.path.join(tmp, generator[:-3])
        os.mkdir(out_dir)
        # -B: a generator must not leave a __pycache__ among the buffers
        run = subprocess.run([sys.executable, "-B", path, out_dir], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, errors="replace")
        if run.returncode:
            problems.append("%s: failed with exit code %d\n%s" % (generator, run.returncode, run.stdout.rstrip()))
        for name in sorted(os.listdir(out_dir)):
            if name in made:
                problems.append("%s: written by both %s and %s" % (name, made[name][0], generator))
            made[name] = (generator, os.path.join(out_dir, name))
    return made, problems


def first_difference(a, b):
    """The number of the first line in which the two files differ."""
    a, b = a.split(b"\n"), b.split(b"\n")
    return next((n for n, (x, y) in enumerate(zip(a, b), 1) if x != y), min(len(a), len(b)) + 1)


def problem(packets, name, there, made):
    """What is wrong with the buffer NAME of the packets directory, or None when it is as generated."""
    if name not in made:
        return "no generator writes it (a buffer made by hand belongs in HAND_MADE of check_packets.py)"
    generator, written_path = made[name]
    if name not in there:
        return "%s writes it, but it is not there" % generator
    with open(os.path.join(packets, name), "rb") as f, open(written_path, "rb") as g:
        committed, written = f.read(), g.read()
    if committed == written:
        return None
    return "not what %s writes, from line %d on (to rewrite it: python3 %s)" % (
        generator, first_difference(committed, written), os.path.relpath(os.path.join(packets, generator)))


def check(suite):
    """Checks the buffers of one suite, prints what is wrong and the summary line. Returns the number of problems."""
    label, packets = os.path.basename(suite), os.path.join(suite, "packets")
    there = sorted(os.path.basename(p) for p in glob.glob(os.path.join(packets, "*.hex")))
    with tempfile.TemporaryDirectory() as tmp:
        made, problems = generate(packets, tmp)
        for name in sorted(set(there) | set(made)):
            path = "%s/packets/%s" % (label, name)
            if path in HAND_MADE and name not in made:
                print("%s: made by hand, not checked (%s)" % (path, HAND_MADE[path]))
                continue
            what = problem(packets, name, there, made)
            if what:
                problems.append("%s: %s" % (name, what))
    for line in problems:
        print("%s/packets/%s" % (label, line))
    print("%s: %d packet buffers, %s" % (
        label, len(there), "%d not as generated" % len(problems) if problems else "all as generated"))
    return len(problems)


def main(args):
    return 1 if sum([check(suite) for suite in suites(args)]) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
