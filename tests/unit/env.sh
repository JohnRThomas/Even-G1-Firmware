# Source before run.sh: host native_sim env from the NCS toolchain bundle.
# LD_LIBRARY_PATH is cut to the bundle's libpython: its old libmpfr breaks host cc1.

_g1_unit_env() {
	local here repo ver ncs_root tc py exports

	if [ -n "${BASH_SOURCE:-}" ]; then
		here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
	else
		here=$(cd "$(dirname "${(%):-%x}")" && pwd)
	fi
	repo=$(cd "$here/../.." && pwd)

	ver=$(sed -n 's/^[[:space:]]*set(REQIURED_NCS_VERSION[[:space:]]\{1,\}\([0-9.]*\)).*/\1/p' \
		"$repo/CMakeLists.txt" | head -n 1)
	if [ -z "$ver" ]; then
		echo "env.sh: REQIURED_NCS_VERSION not found in $repo/CMakeLists.txt" >&2
		return 1
	fi

	ncs_root=${NCS_ROOT:-$HOME/ncs}

	# -I: ignore PYTHONHOME/PYTHONPATH from an earlier bundle env
	py=/usr/bin/python3
	[ -x "$py" ] || py=python3

	if [ -n "${NCS_TOOLCHAIN:-}" ]; then
		tc=$NCS_TOOLCHAIN
	else
		tc=$("$py" -I -c '
import json, sys
ver = "v" + sys.argv[2]
for entry in json.load(open(sys.argv[1])):
    for t in entry.get("toolchains", []):
        if ver in t.get("ncs_versions", []):
            print(t["identifier"]["bundle_id"])
            sys.exit(0)
sys.exit(1)
' "$ncs_root/toolchains/toolchains.json" "$ver") || {
			echo "env.sh: no toolchain bundle for NCS v$ver in $ncs_root/toolchains/toolchains.json" >&2
			return 1
		}
		tc=$ncs_root/toolchains/$tc
	fi
	if [ ! -f "$tc/environment.json" ]; then
		echo "env.sh: $tc/environment.json missing" >&2
		return 1
	fi
	if [ ! -d "$ncs_root/v$ver/zephyr" ]; then
		echo "env.sh: stock NCS tree $ncs_root/v$ver/zephyr missing" >&2
		return 1
	fi

	exports=$("$py" -I -c '
import json, os, shlex, sys
tc = sys.argv[1]
skip = {"LD_LIBRARY_PATH", "ZEPHYR_TOOLCHAIN_VARIANT", "ZEPHYR_SDK_INSTALL_DIR"}
for e in json.load(open(os.path.join(tc, "environment.json")))["env_vars"]:
    key = e["key"]
    if key in skip:
        continue
    if e["type"] == "relative_paths":
        val = ":".join(os.path.join(tc, v) for v in e["values"])
    else:
        val = e["value"]
    if e.get("existing_value_treatment") == "prepend_to" and os.environ.get(key):
        val = val + ":" + os.environ[key]
    print("export %s=%s" % (key, shlex.quote(val)))
' "$tc") || return 1
	eval "$exports"

	export LD_LIBRARY_PATH="$tc/usr/local/lib"

	# Bundle python needs libffi.so.7 for ctypes; link only that, not nanopb's old libs.
	if ! python3 -c 'import ctypes' >/dev/null 2>&1 &&
		[ -f "$tc/opt/nanopb/generator-bin/libffi.so.7" ]; then
		local ffi_dir="${XDG_CACHE_HOME:-$HOME/.cache}/g1_unit_env/$(basename "$tc")"
		mkdir -p "$ffi_dir" &&
			ln -sf "$tc/opt/nanopb/generator-bin/libffi.so.7" "$ffi_dir/libffi.so.7" &&
			export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$ffi_dir"
		if ! python3 -c 'import ctypes' >/dev/null 2>&1; then
			echo "env.sh: the bundle's python cannot import ctypes (libffi.so.7)" >&2
			return 1
		fi
	fi

	export ZEPHYR_TOOLCHAIN_VARIANT=host
	export ZEPHYR_BASE="$ncs_root/v$ver/zephyr"
	export G1_UNIT_NCS_VERSION="$ver"
	export G1_UNIT_TOOLCHAIN="$tc"
	echo "unit test env: NCS v$ver, bundle $tc, ZEPHYR_BASE $ZEPHYR_BASE"
}

_g1_unit_env
unset -f _g1_unit_env
