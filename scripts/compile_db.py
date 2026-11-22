"""The west build's compile databases, for the scripts that run an analyser over the sources.

Sources under net_core/ are in <build>/net_core/compile_commands.json, everything else in
<build>/compile_commands.json.  <build> is $G1_BUILD_DIR, or build/ in the repository root.
"""

import json
import os
import shlex
import subprocess

CORES = ("app", "net")


def repo_root():
	out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
			     text=True, check=True)
	return out.stdout.strip()


def build_dir(root):
	return os.path.abspath(os.path.join(root, os.environ.get("G1_BUILD_DIR", "build")))


def db_dir(build, core):
	return os.path.join(build, "net_core") if core == "net" else build


def core_of(src, root):
	return "net" if os.path.relpath(src, root).startswith("net_core" + os.sep) else "app"


def load(build, core, root):
	"""The compile commands of the repository's own sources in one core's database.

	Returns {source path: (directory, argument list)}, or None if there is no database.
	"""
	path = os.path.join(db_dir(build, core), "compile_commands.json")
	if not os.path.isfile(path):
		return None
	with open(path, encoding="utf-8") as f:
		entries = json.load(f)
	out = {}
	for e in entries:
		src = os.path.normpath(os.path.join(e["directory"], e["file"]))
		if src.startswith(root + os.sep) and not src.startswith(build + os.sep):
			args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
			out[src] = (e["directory"], args)
	return out


def select(argv, root, build):
	"""The compile commands of the sources named in argv, or of every source if argv is empty.

	Returns {core: {source path: (directory, argument list)}}, or None (after saying why) if
	a database that is needed does not exist.
	"""
	dbs = {}

	def db(core):
		if core not in dbs:
			dbs[core] = load(build, core, root)
			if dbs[core] is None:
				print(f"{db_dir(build, core)}/compile_commands.json not found.\n"
				      "Run `west build -b nrf5340dk_nrf5340_cpuapp` first or point "
				      "G1_BUILD_DIR at an existing build.")
		return dbs[core]

	if not argv:
		selected = {core: db(core) for core in CORES}
		return None if None in selected.values() else selected
	selected = {}
	for rel in argv:
		src = os.path.abspath(rel)
		core = core_of(src, root)
		if db(core) is None:
			return None
		if src not in dbs[core]:
			print(f"note: {rel} is not in the {core} compile database "
			      "(not built in this configuration), skipped")
			continue
		selected.setdefault(core, {})[src] = dbs[core][src]
	return selected
