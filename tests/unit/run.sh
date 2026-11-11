#!/usr/bin/env bash
# Builds and runs the unit suites (tests/unit/<suite>/) with twister on native_sim (32-bit).
#
#   . tests/unit/env.sh                   once per shell (run.sh sources it itself if needed)
#   tests/unit/run.sh                     every suite
#   tests/unit/run.sh -s proto_session    one suite of testcase.yaml, named as its directory or
#                                         as its twister scenario (unit.proto_session); -s repeats
#   tests/unit/run.sh --coverage          gcov coverage of src/ per source file, printed and
#                                         written to <out>/coverage/summary.txt
#                                         (tools/coverage.py; --functions lists untested ones)
#   tests/unit/run.sh -O build_unit_me    output directory (default: $G1_UNIT_OUT, else
#                                         <repo>/build_unit). Give each concurrent run its own
#                                         directory; it is deleted and rebuilt on every run.
#   tests/unit/run.sh -j 4                parallel builds (twister -j)
#   tests/unit/run.sh -- <args>           everything after -- goes to twister unchanged
#
# Output directories must be named build_*, build-* or build<digit>* (all in .gitignore's build*/)
# and may not be the firmware's build/ directory. Exit status is twister's (0 when every suite
# passed); with --coverage also 1 when coverage.py fails.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)

usage() {
  sed -n '2,/^set /s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
  exit "${1:-0}"
}

out=${G1_UNIT_OUT:-$repo/build_unit}
# -W: host gcc warns on picolibc and src/
args=(-p native_sim --inline-logs --clobber-output -W -T "$here")
extra=()
coverage=0

while [ $# -gt 0 ]; do
  case "$1" in
  -s | --suite)
    [ $# -ge 2 ] || usage 2
    suite=${2#unit.}
    suite=${suite%/}
    if ! grep -qF "  unit.$suite:" "$here/testcase.yaml"; then
      echo "run.sh: no unit suite '$2' in tests/unit/testcase.yaml" >&2
      usage 2
    fi
    args+=(-s "unit.$suite")
    shift 2
    ;;
  -O | --outdir)
    [ $# -ge 2 ] || usage 2
    out=$2
    shift 2
    ;;
  -j | --jobs)
    [ $# -ge 2 ] || usage 2
    args+=(-j "$2")
    shift 2
    ;;
  --coverage)
    # twister's gcovr step fails on this tree; coverage.py runs gcov itself
    args+=(--enable-coverage)
    coverage=1
    shift
    ;;
  -h | --help)
    usage 0
    ;;
  --)
    shift
    extra=("$@")
    break
    ;;
  *)
    echo "run.sh: unknown argument '$1' (twister options go after --)" >&2
    usage 2
    ;;
  esac
done

out=$(realpath -m -- "$out")
case "$(basename "$out")" in
build_* | build-* | build[0-9]*) ;;
*)
  echo "run.sh: output directory '$out' must be named build_*, build-* or build<digit>*" >&2
  exit 2
  ;;
esac
if [ "$out" = "$repo/build" ] || [[ "$out" == "$repo/build/"* ]]; then
  echo "run.sh: refusing to use the firmware build directory $repo/build" >&2
  exit 2
fi
# --clobber-output deletes it
if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null)" ] && [ ! -f "$out/twister.log" ] &&
  [ ! -f "$out/testplan.json" ]; then
  echo "run.sh: $out exists and is not twister output; not deleting it" >&2
  exit 2
fi

if [ "${ZEPHYR_TOOLCHAIN_VARIANT:-}" != host ] || ! command -v west >/dev/null 2>&1; then
  # shellcheck source=env.sh
  . "$here/env.sh"
fi

cd "$repo"
status=0
(set -x; west twister "${args[@]}" -O "$out" "${extra[@]+"${extra[@]}"}") || status=$?
if [ $coverage -eq 1 ]; then
  python3 "$here/tools/coverage.py" "$out" || [ $status -ne 0 ] || status=1
fi
exit $status
