#!/usr/bin/env python3
"""Run GCC's static analyzer (-fanalyzer) on the firmware's C sources.

    scripts/gcc_analyzer.py [-j N] [source ...]

Without sources, every source of the application and network cores is analysed.  Each
source is compiled once more with the command the west build used for it (see
scripts/compile_db.py), with -fanalyzer added and the object file discarded, so the
analyzer sees the code exactly as the firmware build does.  The exit status is 1 if
anything was reported.
"""

import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import compile_db

DIAG_RE = re.compile(r"^.+:\d+:\d+: (warning|error): ")
# Lines GCC prints ahead of a diagnostic to say where it is.
CONTEXT_RE = re.compile(r"^(In file included from |\s+from |.+: (In function |At top level))")


def analyzer_args(args):
	out = []
	skip = False
	for a in args:
		if skip:
			skip = False
		elif a == "-o":
			out += ["-o", os.devnull]
			skip = True
		elif not a.startswith("-fdiagnostics-color"):
			out.append(a)
	return out + ["-fanalyzer", "-fdiagnostics-color=never"]


def split_diagnostics(text):
	"""Split GCC's output into one block per diagnostic, its context lines and path included."""
	blocks = []
	in_context = False
	for line in text.splitlines():
		if CONTEXT_RE.match(line):
			if not in_context:
				blocks.append([])
			in_context = True
		elif DIAG_RE.match(line):
			if not in_context:
				blocks.append([])
			in_context = False
		elif not blocks:
			blocks.append([])
		blocks[-1].append(line)
	return ["\n".join(b) for b in blocks if any(s.strip() for s in b)]


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
	parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4,
			    help="compilers to run at once (default: one per CPU)")
	parser.add_argument("sources", nargs="*", help="C sources (default: all)")
	opts = parser.parse_args(argv)

	root = compile_db.repo_root()
	selected = compile_db.select(opts.sources, root, compile_db.build_dir(root))
	if selected is None:
		return 1
	jobs = [(directory, analyzer_args(args))
		for sources in selected.values() for directory, args in sources.values()]

	def run(job):
		directory, args = job
		res = subprocess.run(args, cwd=directory, capture_output=True, text=True)
		return res.returncode, res.stdout + res.stderr

	failed = False
	seen = set()
	with ThreadPoolExecutor(max_workers=opts.jobs) as pool:
		for code, output in pool.map(run, jobs):
			failed |= code != 0
			for block in split_diagnostics(output):
				failed = True
				# A warning in a header comes once per source that includes it.
				if block not in seen:
					seen.add(block)
					print(block)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
