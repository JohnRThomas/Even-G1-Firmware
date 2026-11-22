#!/usr/bin/env python3
"""Run cppcheck on the firmware's C sources.

    scripts/cppcheck.py [-j N] [--style] [--misra [--rule-texts FILE]] [source ...]

Without sources, every source of the application and network cores is checked; cppcheck
then also follows values across source files.  The exit status is 1 if anything was
reported.

cppcheck's own preprocessor cannot read the Zephyr headers (they put #if inside the
arguments of __ASSERT()).  So the cross compiler does that part: each source is run through
the command the west build used for it (see scripts/compile_db.py) with
`-E -fdirectives-only`, which resolves the includes and conditionals for the build's exact
configuration and leaves the macros, the compiler's predefined ones included, for cppcheck
to expand.  The results and cppcheck's cache are kept in <build>/cppcheck/.

Only findings located in the repository's sources are printed; the SDK's headers are not
ours to fix.  The exception is an error that made cppcheck give up on a source, wherever it
is.

--misra adds the MISRA C:2012 addon.  cppcheck names only the rule number unless
--rule-texts gives it a file with the rule headlines, which MISRA licenses and cppcheck
cannot ship (see "MISRA" in the cppcheck manual for the format).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import compile_db

TEMPLATE = "{file}:{line}:{column}: {severity}: {message} [{id}]"
DIAG_RE = re.compile(r"^(.+):\d+:\d+: \w+: .* \[([\w.-]+)\]$")
CRITICAL_RE = re.compile(r"^There were critical errors \((.*)\)", re.MULTILINE)

# Zephyr's timeout macros (K_MSEC() and the like) contain a division that is by zero in the
# branch the clock rates do not select.  Zephyr substitutes 1 there for clang, which warns
# about it as cppcheck does; this is that substitution.
TMCVT_DIVISOR_RE = re.compile(r"^(#define z_tmcvt_divisor\(a, ?b\)) \(\(a\) / \(b\)\)$", re.MULTILINE)
TMCVT_DIVISOR_SUB = r"\1 ((a) / (b) ? (a) / (b) : 1)"


def preprocess_args(args, out):
	res = []
	skip = False
	for a in args:
		if skip:
			skip = False
		elif a == "-o":
			res += ["-o", out]
			skip = True
		elif a != "-c":
			res.append(a)
	return res + ["-E", "-fdirectives-only"]


def preprocess(job):
	"""Returns the compiler's complaint, or None if the preprocessed source was written."""
	directory, args, out = job
	os.makedirs(os.path.dirname(out), exist_ok=True)
	res = subprocess.run(preprocess_args(args, out), cwd=directory, capture_output=True, text=True)
	if res.returncode != 0:
		return res.stdout + res.stderr
	with open(out, encoding="utf-8", errors="surrogateescape") as f:
		text = f.read()
	with open(out, "w", encoding="utf-8", errors="surrogateescape") as f:
		f.write(TMCVT_DIVISOR_RE.sub(TMCVT_DIVISOR_SUB, text))
	return None


def c_standard(sources):
	for _, args in sources.values():
		for a in args:
			if a.startswith("-std="):
				return a[len("-std="):].replace("gnu", "c")
	return "c99"


def check_core(cppcheck, core, sources, root, build, opts):
	"""Returns the lines to print for one core's sources."""
	out_dir = os.path.join(build, "cppcheck", core)
	cache_dir = os.path.join(out_dir, "cache")
	report = os.path.join(out_dir, "checkers.txt")
	os.makedirs(cache_dir, exist_ok=True)

	jobs = [(directory, args, os.path.join(out_dir, os.path.relpath(src, root)))
		for src, (directory, args) in sources.items()]
	with ThreadPoolExecutor(max_workers=opts.jobs) as pool:
		failures = [f for f in pool.map(preprocess, jobs) if f is not None]
	if failures:
		return failures

	cmd = [cppcheck, "--quiet", "--language=c", f"--std={c_standard(sources)}",
	       # Cortex-M33 with arm-zephyr-eabi-gcc: 32 bit, unsigned char, 4-byte wchar_t.
	       "--platform=arm32-wchar_t4", "--library=zephyr",
	       "--enable=" + ("warning,performance,portability,style" if opts.style or opts.misra
			      else "warning,performance,portability"),
	       "--inline-suppr", f"--template={TEMPLATE}", f"--cppcheck-build-dir={cache_dir}",
	       f"--checkers-report={report}", f"-j{opts.jobs}"]
	if opts.misra and opts.rule_texts:
		addon = os.path.join(out_dir, "misra.json")
		with open(addon, "w", encoding="utf-8") as f:
			json.dump({"script": "misra.py",
				   "args": ["--rule-texts=" + os.path.abspath(opts.rule_texts)]}, f)
		cmd.append(f"--addon={addon}")
	elif opts.misra:
		cmd.append("--addon=misra")
	if os.path.exists(report):
		os.remove(report)
	res = subprocess.run(cmd + [job[2] for job in jobs], cwd=root, capture_output=True, text=True)

	# The ids of the errors that ended the analysis of a source early, by cppcheck's own account.
	critical = set()
	if os.path.exists(report):
		with open(report, encoding="utf-8") as f:
			m = CRITICAL_RE.search(f.read())
		if m:
			critical = {s.strip() for s in m.group(1).split(",")}

	lines = []
	for line in (res.stdout + res.stderr).splitlines():
		m = DIAG_RE.match(line)
		if m:
			path = os.path.normpath(os.path.join(root, m.group(1)))
			ours = path.startswith(root + os.sep) and not path.startswith(build + os.sep)
			if not ours and m.group(2) not in critical:
				continue
		if line.strip():
			lines.append(line)
	if res.returncode != 0 and not lines:
		lines.append(f"cppcheck exited with status {res.returncode}")
	return lines


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
	parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4,
			    help="processes to run at once (default: one per CPU)")
	parser.add_argument("--style", action="store_true",
			    help="also report style findings (unused variables, redundant code, ...)")
	parser.add_argument("--misra", action="store_true", help="also check MISRA C:2012 rules")
	parser.add_argument("--rule-texts", metavar="FILE",
			    help="MISRA rule headlines, to print them instead of the bare rule number")
	parser.add_argument("sources", nargs="*", help="C sources (default: all)")
	opts = parser.parse_args(argv)

	cppcheck = os.environ.get("CPPCHECK") or shutil.which("cppcheck")
	if not cppcheck:
		print("cppcheck not found on PATH (set CPPCHECK to override)")
		return 1
	root = compile_db.repo_root()
	build = compile_db.build_dir(root)
	selected = compile_db.select(opts.sources, root, build)
	if selected is None:
		return 1

	failed = False
	for core, sources in selected.items():
		for line in check_core(cppcheck, core, sources, root, build, opts):
			failed = True
			print(line)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
