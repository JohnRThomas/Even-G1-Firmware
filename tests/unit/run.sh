#!/usr/bin/env bash
# Builds and runs the unit suites (tests/unit/<suite>/) with twister on native_sim (32-bit).
#
#   . tests/unit/env.sh                   once per shell (run.sh sources it itself if needed)
#   tests/unit/run.sh                     every suite
#   tests/unit/run.sh -s proto_session    one suite (a directory under tests/unit); -s repeats.
#                                         A twister test id (util/unit.util) works too.
#   tests/unit/run.sh --coverage          gcov coverage of src/ per source file, printed and
#                                         written to <out>/coverage/summary.txt
#                                         (tools/coverage.py; --functions lists untested ones)
#   tests/unit/run.sh -O build_unit_me    output directory (default: $G1_UNIT_OUT, else
#                                         <repo>/build_unit). Give each concurrent run its own
#                                         directory; it is deleted and rebuilt on every run.
#   tests/unit/run.sh -j 4                parallel builds (twister -j)
#   tests/unit/run.sh -- <args>           everything after -- goes to twister unchanged
#
# Output directories must be named build* (covered by .gitignore) and may not be the
# firmware's build/ directory. Exit status is twister's: 0 when every suite passed.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)

usage() {
	sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit "${1:-0}"
}

out=${G1_UNIT_OUT:-$repo/build_unit}
roots=()
scenarios=()
extra=()
coverage=0
jobs=()

while [ $# -gt 0 ]; do
	case "$1" in
	-s | --suite)
		[ $# -ge 2 ] || usage 2
		if [ -d "$here/$2" ] && [ -f "$here/$2/testcase.yaml" ]; then
			roots+=("$here/$2")
		else
			scenarios+=("$2")
		fi
		shift 2
		;;
	-O | --outdir)
		[ $# -ge 2 ] || usage 2
		out=$2
		shift 2
		;;
	-j | --jobs)
		[ $# -ge 2 ] || usage 2
		jobs=(-j "$2")
		shift 2
		;;
	--coverage)
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

case "$out" in
/*) ;;
*) out="$PWD/$out" ;;
esac
out=$(realpath -m "$out")
case "$(basename "$out")" in
build_* | build-* | build[0-9]*) ;;
*)
	echo "run.sh: output directory '$out' must be named build_<something> (.gitignore: build*/)" >&2
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

if [ ${#roots[@]} -eq 0 ]; then
	roots=("$here")
fi

# -W: host gcc warns on picolibc and src/
args=(-p native_sim --inline-logs --clobber-output -W -O "$out")
for r in "${roots[@]}"; do
	args+=(-T "$r")
done
for s in "${scenarios[@]+"${scenarios[@]}"}"; do
	args+=(-s "$s")
done
# twister's gcovr step fails on this tree; coverage.py runs gcov itself
if [ $coverage -eq 1 ]; then
	args+=(--enable-coverage)
fi

cd "$repo"
if [ $coverage -eq 0 ]; then
	set -x
	exec west twister "${args[@]}" "${jobs[@]+"${jobs[@]}"}" "${extra[@]+"${extra[@]}"}"
fi
status=0
(set -x; west twister "${args[@]}" "${jobs[@]+"${jobs[@]}"}" "${extra[@]+"${extra[@]}"}") || status=$?
python3 "$here/tools/coverage.py" "$out" || [ $status -ne 0 ] || status=1
exit $status
