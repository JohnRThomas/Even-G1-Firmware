#!/usr/bin/env python3
"""Line and function coverage of src/ by the unit suites."""

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SRC = os.path.join(REPO, "src")


def find_objects(outdir):
    found = []
    platform_dir = os.path.join(outdir, "native_sim")
    if not os.path.isdir(platform_dir):
        return found
    for suite in sorted(os.listdir(platform_dir)):
        suite_dir = os.path.join(platform_dir, suite)
        for root, _dirs, files in os.walk(suite_dir):
            if os.sep + os.path.join("CMakeFiles", "app.dir") not in root:
                continue
            for name in files:
                if name.endswith(".gcda"):
                    found.append((suite, os.path.join(root, name)))
    return found


def run_gcov(job):
    suite, gcda = job
    with tempfile.TemporaryDirectory(prefix="g1cov-") as tmp:
        proc = subprocess.run(
            ["gcov", "--json-format", "--stdout", "--branch-probabilities",
             "--object-directory", os.path.dirname(gcda), gcda],
            cwd=tmp, capture_output=True, text=True)
    if proc.returncode != 0:
        return suite, [], "%s: gcov exit %d: %s" % (gcda, proc.returncode, proc.stderr.strip()[:200])
    docs = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                docs.append(json.loads(line))
            except ValueError:
                pass
    return suite, docs, None


def firmware_path(doc, entry):
    path = entry.get("file", "")
    if not os.path.isabs(path):
        path = os.path.join(doc.get("current_working_directory", ""), path)
    path = os.path.realpath(path)
    if not path.startswith(SRC + os.sep):
        return None
    return os.path.relpath(path, SRC)


def collect(outdir, jobs):
    lines = {}      # file -> {line: count}
    functions = {}  # file -> {name: (start line, count)}
    suites = {}
    errors = []
    objects = find_objects(outdir)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for suite, docs, error in pool.map(run_gcov, objects):
            if error:
                errors.append(error)
            for doc in docs:
                for entry in doc.get("files", []):
                    rel = firmware_path(doc, entry)
                    if rel is None:
                        continue
                    file_lines = lines.setdefault(rel, {})
                    executed = False
                    for line in entry.get("lines", []):
                        number = line["line_number"]
                        file_lines[number] = file_lines.get(number, 0) + line["count"]
                        executed = executed or line["count"] > 0
                    file_functions = functions.setdefault(rel, {})
                    for function in entry.get("functions", []):
                        name = function.get("demangled_name") or function["name"]
                        start, count = file_functions.get(name, (function["start_line"], 0))
                        file_functions[name] = (start, count + function["execution_count"])
                    if executed:
                        suites.setdefault(rel, set()).add(suite)
    return lines, functions, suites, errors, len(objects)


def all_sources():
    found = []
    for root, _dirs, files in os.walk(SRC):
        for name in files:
            if name.endswith(".c"):
                found.append(os.path.relpath(os.path.join(root, name), SRC))
    return sorted(found)


def percent(hit, total):
    return "%5.1f" % (100.0 * hit / total) if total else "    -"


def report(lines, functions, suites):
    out = []
    width = max([len(f) for f in lines] + [10])
    out.append("%-*s  %13s %6s  %11s  %s" % (width, "file (src/)", "lines", "%", "functions", "suites"))
    total_hit = total_lines = total_fn_hit = total_fn = 0
    for rel in sorted(lines):
        if not rel.endswith(".c"):
            continue
        hit = sum(1 for count in lines[rel].values() if count > 0)
        total = len(lines[rel])
        fn_hit = sum(1 for _start, count in functions.get(rel, {}).values() if count > 0)
        fn_total = len(functions.get(rel, {}))
        total_hit += hit
        total_lines += total
        total_fn_hit += fn_hit
        total_fn += fn_total
        out.append("%-*s  %6d/%-6d %6s  %5d/%-5d  %s" % (
            width, rel, hit, total, percent(hit, total), fn_hit, fn_total,
            " ".join(sorted(suites.get(rel, []))) or "(compiled, never run)"))
    out.append("%-*s  %6d/%-6d %6s  %5d/%-5d" % (
        width, "TOTAL (files some suite compiles)", total_hit, total_lines,
        percent(total_hit, total_lines), total_fn_hit, total_fn))
    missing = [f for f in all_sources() if f not in lines]
    out.append("")
    out.append("Source files no unit suite compiles (%d):" % len(missing))
    for rel in missing:
        out.append("  " + rel)
    return out, (100.0 * total_hit / total_lines if total_lines else 0.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("outdir", help="twister output directory of a coverage run (build_unit)")
    parser.add_argument("--functions", metavar="FILE",
                        help="list the functions of src/FILE that no test executes")
    parser.add_argument("--min-lines", type=float, metavar="PCT",
                        help="exit 1 when total line coverage is below PCT")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 4, help="gcov processes at once")
    args = parser.parse_args()

    outdir = os.path.abspath(args.outdir)
    lines, functions, suites, errors, count = collect(outdir, args.j)
    if count == 0:
        sys.exit("coverage.py: no coverage data under %s/native_sim/*/*/CMakeFiles/app.dir; "
                 "run tests/unit/run.sh --coverage -O %s first" % (outdir, args.outdir))
    for error in errors:
        print("coverage.py: " + error, file=sys.stderr)

    if args.functions:
        rel = os.path.normpath(args.functions)
        if rel.startswith("src" + os.sep):
            rel = rel[4:]
        if rel not in functions:
            sys.exit("coverage.py: no suite compiles src/%s" % rel)
        unused = sorted((start, name) for name, (start, hits) in functions[rel].items() if hits == 0)
        print("Functions of src/%s that no test executes (%d of %d):" % (
            rel, len(unused), len(functions[rel])))
        for start, name in unused:
            print("  %5d  %s" % (start, name))
        return 0

    text, total = report(lines, functions, suites)
    print("\n".join(text))
    summary_dir = os.path.join(outdir, "coverage")
    os.makedirs(summary_dir, exist_ok=True)
    with open(os.path.join(summary_dir, "summary.txt"), "w") as handle:
        handle.write("\n".join(text) + "\n")
    if errors:
        return 1
    if args.min_lines is not None and total < args.min_lines:
        print("coverage.py: total line coverage %.1f %% is below %.1f %%" % (total, args.min_lines),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
