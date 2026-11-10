#!/usr/bin/env python3
"""Reference pictures of the panel contents a suite's tests name: SUITE/frames/NAME.png for a name of
SUITE/frames.txt, so that a person can see what settles_to("NAME") expects, and hold the picture of a failing
run against it.

    python3 tests/reg/frames.py [--suite g1] RUN_DIR ...     take the pictures from kept run directories
    python3 tests/reg/frames.py [--suite g1]                 list the names that have no picture yet

A run directory (run.py --keep, or a run that did not pass) holds a PNG of each panel content of its trace,
<lens>_<sha12>.png. Every one of them whose content has a name in frames.txt is copied to SUITE/frames/NAME.png.
A picture that is there already is left alone: a name whose picture changed has a new sha12 in frames.txt, and
its old picture is then reported as stale (the unit tests fail on it too) until it is removed and taken again.
Look at a picture before committing it: it becomes what the tests are read against.
"""
import argparse
import glob
import os
import shutil
import sys

sys.dont_write_bytecode = True

from engine import loader, trace              # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--suite", default="g1", metavar="g1", help="the suite whose frames.txt names the pictures")
    parser.add_argument("runs", nargs="*", metavar="RUN_DIR", help="kept run directories (or a directory of them)")
    args = parser.parse_args()
    suite = loader.Suite(os.path.join(HERE, args.suite))
    names = {sha: name for name, sha in suite.frames().items()}
    pictures = os.path.join(suite.dir, "frames")
    os.makedirs(pictures, exist_ok=True)
    stale = 0
    for path in sorted(glob.glob(os.path.join(pictures, "*.png"))):
        name = os.path.basename(path)[:-4]
        if suite.frames().get(name) != trace.sha12(trace.read_png(path)):
            stale += 1
            print("stale: %s is not the content frames.txt names %s" % (suite.relative(path), name))
    taken = 0
    for run in args.runs:
        for path in sorted(glob.glob(os.path.join(run, "*.png")) + glob.glob(os.path.join(run, "*", "*.png"))):
            sha = trace.sha12(trace.read_png(path))
            target = os.path.join(pictures, "%s.png" % names.get(sha))
            if sha in names and not os.path.exists(target):
                shutil.copyfile(path, target)
                taken += 1
                print("%s: from %s" % (suite.relative(target), path))
    missing = sorted(name for name in suite.frames() if not os.path.isfile(os.path.join(pictures, name + ".png")))
    for name in missing:
        print("no picture: %s (%s)" % (name, suite.frames()[name]))
    print("%s: %d name(s), %d picture(s) taken, %d without a picture, %d stale" % (
        suite.name, len(names), taken, len(missing), stale))
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
