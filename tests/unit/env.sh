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

  # tc=<bundle> ($NCS_TOOLCHAIN or the one toolchains.json lists for v$ver), then the
  # exports of its environment.json
  exports=$("$py" -I - "$ncs_root" "$ver" "${NCS_TOOLCHAIN:-}" <<'EOF'
import json, os, shlex, sys

ncs_root, version, bundle = sys.argv[1:]
if not bundle:
    index = os.path.join(ncs_root, "toolchains", "toolchains.json")
    try:
        entries = json.load(open(index))
    except (OSError, ValueError):
        entries = []
    bundles = [toolchain["identifier"]["bundle_id"] for entry in entries
               for toolchain in entry.get("toolchains", [])
               if "v" + version in toolchain.get("ncs_versions", [])]
    if not bundles:
        sys.exit("env.sh: no toolchain bundle for NCS v%s in %s" % (version, index))
    bundle = os.path.join(ncs_root, "toolchains", bundles[0])
try:
    env_vars = json.load(open(os.path.join(bundle, "environment.json")))["env_vars"]
except OSError:
    sys.exit("env.sh: %s/environment.json missing" % bundle)
print("tc=" + shlex.quote(bundle))
skip = {"LD_LIBRARY_PATH", "ZEPHYR_TOOLCHAIN_VARIANT", "ZEPHYR_SDK_INSTALL_DIR"}
for var in env_vars:
    key = var["key"]
    if key in skip:
        continue
    if var["type"] == "relative_paths":
        value = ":".join(os.path.join(bundle, path) for path in var["values"])
    else:
        value = var["value"]
    if var.get("existing_value_treatment") == "prepend_to" and os.environ.get(key):
        value += ":" + os.environ[key]
    print("export %s=%s" % (key, shlex.quote(value)))
EOF
  ) || return 1
  if [ ! -d "$ncs_root/v$ver/zephyr" ]; then
    echo "env.sh: stock NCS tree $ncs_root/v$ver/zephyr missing" >&2
    return 1
  fi
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
  echo "unit test env: NCS v$ver, bundle $tc, ZEPHYR_BASE $ZEPHYR_BASE"
}

_g1_unit_env
unset -f _g1_unit_env
