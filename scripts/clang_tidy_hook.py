#!/usr/bin/env python3
"""Run clang-tidy on the given C sources using the west build's compile databases.

Sources under net_core/ use <build>/net_core/compile_commands.json, everything else
<build>/compile_commands.json.  <build> is $G1_BUILD_DIR, or build/ in the repository
root.  The databases are copied to <build>/clang-tidy/ with the GCC-only options clang
rejects removed.  Warnings from headers shared by several sources are printed once.
"""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

DIAG_RE = re.compile(r"^.+:\d+:\d+: (warning|error): ")
NOISE_RE = re.compile(r"^\d+ (warning|error)s?( and \d+ (warning|error)s?)? generated\.$")

GCC_ONLY_PREFIXES = (
	"-fno-reorder-functions",
	"-fno-defer-pop",
	"-fno-printf-return-value",
	"-mfp16-format=",
	"-fdiagnostics-color",
	"--param",
	"-specs=",
	"--specs=",
)


def repo_root():
	out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
			     text=True, check=True)
	return out.stdout.strip()


def clang_args(entry):
	args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
	args = [a for a in args if not a.startswith(GCC_ONLY_PREFIXES)]
	# GCC for arm-zephyr-eabi defaults to soft float; clang assumes the core's FPU.
	if not any(a.startswith("-mfloat-abi=") for a in args):
		args.append("-mfloat-abi=soft")
	return args


def load_db(src_dir, out_dir, root):
	"""Write a clang-compatible copy of src_dir's database to out_dir.

	Returns the set of source files it covers, or None if src_dir has no database.
	"""
	src = os.path.join(src_dir, "compile_commands.json")
	if not os.path.isfile(src):
		return None
	with open(src, encoding="utf-8") as f:
		entries = json.load(f)
	out = []
	for e in entries:
		path = os.path.normpath(os.path.join(e["directory"], e["file"]))
		if path.startswith(root + os.sep):
			out.append({"directory": e["directory"], "file": path,
				    "arguments": clang_args(e)})
	os.makedirs(out_dir, exist_ok=True)
	with open(os.path.join(out_dir, "compile_commands.json"), "w", encoding="utf-8") as f:
		json.dump(out, f, indent=1)
	return {e["file"] for e in out}


def split_diagnostics(text):
	blocks = []
	for line in text.splitlines():
		if NOISE_RE.match(line):
			continue
		if DIAG_RE.match(line) or not blocks:
			blocks.append([line])
		else:
			blocks[-1].append(line)
	return ["\n".join(b) for b in blocks if any(s.strip() for s in b)]


def main(argv):
	root = repo_root()
	build = os.path.abspath(os.path.join(root, os.environ.get("G1_BUILD_DIR", "build")))
	db_dirs = {"app": build, "net": os.path.join(build, "net_core")}
	tidy_dirs = {core: os.path.join(build, "clang-tidy", core) for core in db_dirs}
	clang_tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
	if not clang_tidy:
		print("clang-tidy not found on PATH (set CLANG_TIDY to override)")
		return 1

	dbs = {}
	jobs = []
	for rel in argv:
		src = os.path.normpath(os.path.join(root, rel))
		core = "net" if os.path.relpath(src, root).startswith("net_core" + os.sep) else "app"
		if core not in dbs:
			dbs[core] = load_db(db_dirs[core], tidy_dirs[core], root)
			if dbs[core] is None:
				print(f"{db_dirs[core]}/compile_commands.json not found.\n"
				      "Run `west build -b nrf5340dk_nrf5340_cpuapp` first, point "
				      "G1_BUILD_DIR at an existing build, or commit with "
				      "SKIP=clang-tidy.")
				return 1
		if src not in dbs[core]:
			print(f"note: {rel} is not in the {core} compile database "
			      "(not built in this configuration), skipped")
			continue
		jobs.append((tidy_dirs[core], src))

	header_filter = "^" + re.escape(root) + "/(src|net_core/src)/"

	def run(job):
		db_dir, src = job
		cmd = [clang_tidy, "-p", db_dir, "--quiet", f"--header-filter={header_filter}", src]
		res = subprocess.run(cmd, capture_output=True, text=True)
		return res.returncode, res.stdout + res.stderr

	failed = False
	seen = set()
	with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
		for code, output in pool.map(run, jobs):
			failed |= code != 0
			for block in split_diagnostics(output):
				if block not in seen:
					seen.add(block)
					print(block)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
