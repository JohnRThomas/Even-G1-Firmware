#!/usr/bin/env python3
"""Run cppcheck on the firmware's C sources.

    scripts/cppcheck.py [-j N] [--style] [--misra [--rule-texts FILE]] [source ...]

Without sources, every source of the application and network cores is checked; cppcheck
then also follows values across source files, and only then are the MISRA rules about the
whole program (unused and doubly used names, linkage) reported.  The exit status is 1 if
anything was reported.

cppcheck's own preprocessor cannot read the Zephyr headers (they put #if inside the
arguments of __ASSERT()).  So the cross compiler does that part: each source is run through
the command the west build used for it (see scripts/compile_db.py) with
`-E -fdirectives-only`, which resolves the includes and conditionals for the build's exact
configuration and leaves the macros, the compiler's predefined ones included, for cppcheck
to expand.  The results and cppcheck's cache are kept in <build>/cppcheck/.

Only findings located in the repository's sources are printed; the SDK's headers are not
ours to fix.  The exception is an error that made cppcheck give up on a source, wherever it
is.

--misra adds the MISRA C:2012 addon and the style findings.  The rules listed in
scripts/misra_deviations.txt are not checked; each has its reason there.  The message of a
finding is this project's one-line reminder of the rule (scripts/misra_rules.txt); the
official headlines are MISRA's to license, and --rule-texts takes a file with them (see
"MISRA" in the cppcheck manual for the format).
"""

import argparse
import fcntl
import fnmatch
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

# The compiler names a header the way the #include reached it ("src/ble/../main.h").
# cppcheck compares the names as text when it looks for one thing declared in two places.
LINEMARKER_RE = re.compile(r'^(# \d+ ")([^"<][^"]*)(")', re.MULTILINE)

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
MISRA_DEVIATIONS = os.path.join(SCRIPTS, "misra_deviations.txt")
MISRA_RULES = os.path.join(SCRIPTS, "misra_rules.txt")
MISRA_RULE_RE = re.compile(r"^(\d+\.\d+)(?:\s+in\s+(\S+))?\s")
# Rules about the program as a whole (unused or doubly used names, linkage).  They are only
# reported when every source is checked: for a part of the sources they are wrong.
MISRA_WHOLE_PROGRAM = {"misra-c2012-" + rule for rule in (
	"2.3", "2.4", "2.5", "5.6", "5.7", "5.8", "5.9", "8.5", "8.6", "8.7")}


LOCATION_RE = re.compile(r"^.+:(\d+):(\d+): ")
MACRO_DEF_RE = re.compile(r'^# \d+ "([^"]*)"|^#define (\w+)', re.MULTILINE)
IDENTIFIER_RE = re.compile(r"[A-Za-z_]\w*")
BEFORE_PAREN_RE = re.compile(r"([A-Za-z_]\w*)\s*$")
ARRAY_INIT_RE = re.compile(r"\bchar\s+\w+\s*\[[^\]]*\]\s*=")


def sdk_macros(preprocessed, root, build):
	"""The names of the macros that the given preprocessed sources define outside the
	repository's own files (SDK and compiler headers, the build's generated ones)."""
	theirs, ours = set(), set()
	for path in preprocessed:
		with open(path, encoding="utf-8", errors="surrogateescape") as f:
			text = f.read()
		into = theirs
		for m in MACRO_DEF_RE.finditer(text):
			if m.group(2):
				into.add(m.group(2))
			else:
				into = (ours if m.group(1).startswith(root + os.sep)
					and not m.group(1).startswith(build + os.sep) else theirs)
	return theirs - ours


def at_macro(text, column, macros):
	"""Whether the column of a source line is at the name of one of the macros, or at the
	parenthesis after it: where cppcheck locates what the macro expands to."""
	i = column - 1
	if i < len(text) and text[i] == "(":
		m = BEFORE_PAREN_RE.search(text[:i])
		return bool(m) and m.group(1) in macros
	m = IDENTIFIER_RE.match(text, i)
	return bool(m) and m.group(0) in macros


def misra_deviations():
	"""scripts/misra_deviations.txt: the rules not checked at all, and (rule id, path pattern)
	for the ones not checked in some files."""
	everywhere, in_files = [], []
	with open(MISRA_DEVIATIONS, encoding="utf-8") as f:
		for m in filter(None, map(MISRA_RULE_RE.match, f)):
			if m.group(2):
				in_files.append(("misra-c2012-" + m.group(1), m.group(2)))
			else:
				everywhere.append(m.group(1))
	return everywhere, in_files


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
	# Without the fortify wrappers: they turn memcpy() and the like into compiler builtins,
	# which have no declaration for cppcheck to find.
	return res + ["-U_FORTIFY_SOURCE", "-E", "-fdirectives-only"]


def preprocess(job):
	"""Returns the compiler's complaint, or None if the preprocessed source was written."""
	directory, args, out = job
	os.makedirs(os.path.dirname(out), exist_ok=True)
	res = subprocess.run(preprocess_args(args, out), cwd=directory, capture_output=True, text=True)
	if res.returncode != 0:
		return res.stdout + res.stderr
	with open(out, encoding="utf-8", errors="surrogateescape") as f:
		text = f.read()
	text = TMCVT_DIVISOR_RE.sub(TMCVT_DIVISOR_SUB, text)
	text = LINEMARKER_RE.sub(lambda m: m.group(1) + os.path.normpath(m.group(2)) + m.group(3), text)
	with open(out, "w", encoding="utf-8", errors="surrogateescape") as f:
		f.write(text)
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
	os.makedirs(out_dir, exist_ok=True)
	# One run at a time: the cache and the report are shared.
	with open(os.path.join(out_dir, "lock"), "w", encoding="utf-8") as lock:
		fcntl.flock(lock, fcntl.LOCK_EX)
		return check_core_locked(cppcheck, out_dir, sources, root, build, opts)


def check_core_locked(cppcheck, out_dir, sources, root, build, opts):
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
	not_in_files = []
	if opts.misra:
		not_checked, not_in_files = misra_deviations()
		addon_args = ["--suppress-rules=" + ",".join(not_checked),
			      "--rule-texts=" + os.path.abspath(opts.rule_texts or MISRA_RULES)]
		addon = os.path.join(out_dir, "misra.json")
		with open(addon, "w", encoding="utf-8") as f:
			json.dump({"script": "misra.py", "args": addon_args}, f)
		cmd.append(f"--addon={addon}")
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
	macros = None
	source_lines = {}
	for line in (res.stdout + res.stderr).splitlines():
		m = DIAG_RE.match(line)
		if m:
			# Not ours either: the build directory's generated files, and the compiler's
			# "<built-in>" and "<command-line>" macro definitions, which are no files.
			path = os.path.normpath(os.path.join(root, m.group(1)))
			ours = (path.startswith(root + os.sep) and not path.startswith(build + os.sep)
				and os.path.isfile(path))
			if not ours and m.group(2) not in critical:
				continue
			# The addon's note that it could not type what an SDK macro (CONTAINER_OF,
			# K_MSGQ_DEFINE, UINT32_MAX) expanded to.  Nothing in our sources changes that.
			if m.group(2) == "misra-config":
				continue
			if opts.sources and m.group(2) in MISRA_WHOLE_PROGRAM:
				continue
			rel = os.path.relpath(path, root)
			if any(m.group(2) == rule and fnmatch.fnmatch(rel, pattern)
			       for rule, pattern in not_in_files):
				continue
			# A MISRA finding inside what an SDK macro expands to (the arithmetic of
			# K_MSEC(), the shift of BIT()) is the SDK's as much as one in its headers.
			if ours and m.group(2).startswith("misra-"):
				if macros is None:
					macros = sdk_macros([job[2] for job in jobs], root, build)
				if path not in source_lines:
					with open(path, encoding="utf-8", errors="replace") as f:
						source_lines[path] = f.read().splitlines()
				where = LOCATION_RE.match(line)
				number, column = int(where.group(1)), int(where.group(2))
				text = source_lines[path][number - 1] if 0 < number <= len(source_lines[path]) else ""
				if at_macro(text, column, macros):
					continue
				# Three slips of the addon.  Its pattern for a constant with an 'l' suffix
				# also fits identifiers spelled in hex digits (del, accel, cell); a
				# #define inside a system header is laid at the #include that reached it;
				# and a char array initialised from a string literal is taken for a
				# pointer to non-const that is given one.
				if m.group(2) == "misra-c2012-7.3" and not text[column - 1:column].isdigit():
					continue
				if m.group(2) == "misra-c2012-21.1" and text.lstrip().startswith("#include"):
					continue
				if m.group(2) == "misra-c2012-7.4" and ARRAY_INIT_RE.search(text):
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
	parser.add_argument("--misra", action="store_true",
			    help="also check MISRA C:2012 rules (implies --style)")
	parser.add_argument("--rule-texts", metavar="FILE",
			    help="MISRA's rule headlines, to print them instead of scripts/misra_rules.txt")
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
