#!/usr/bin/env python3
"""Command audit: command ids in the phone app logs vs. vectors and unit tests."""

import argparse
import collections
import hashlib
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_LOGS = REPO.parent / "reference_material" / "logs" / "even_logs"

TX_RE = re.compile(r"BleManager send +cmd: ([lLrR])([0-9a-fA-F]+), len: (\d+), data = ([0-9a-fA-F ]*)")
RX_RE = re.compile(r"BleManager receive cmd: ([lLrR])([0-9a-fA-F]+), len: (\d+), rssi: -?\d+, data = ([0-9a-fA-F ]*)")
NATIVE_RE = re.compile(r"ble_([LR])-> send 0x([0-9a-fA-F]{1,2}) (\d+)")
TIMEOUT_RE = re.compile(r"send Timeout ([lLrR])([0-9a-fA-F]{2}) of (\d+)")
UNIT_RE = re.compile(r"test_0x([0-9a-fA-F]{2})_")
# a macro NAME(name, ...) whose body starts with ZTEST(<suite>, test_##name): its rows
# NAME(0xNN_...) define test_0xNN_ cases
ROW_MACRO_RE = re.compile(r"#define\s+(\w+)\((\w+)[^)]*\)[\s\\]*ZTEST\(\w+,\s*test_##\2\)")
VECTOR_TAG_RE = re.compile(r"^#\s*([a-z0-9_]+)\s*$")

# tx: app -> glasses, rx: glasses -> app
DIRECTIONS = ("tx", "rx")

# u16 LE length at bytes 1..2, sub-command at byte 4
LEN16 = {0x06, 0x08, 0x09, 0x0A, 0x10, 0x1E, 0x21, 0x22, 0x24, 0x25, 0x26, 0x39, 0x3D, 0x50,
         0x51, 0x52, 0x53, 0x54, 0x55, 0x56}

EXTRA_NAMES = {
    0x21: "quick-note record announce (glasses)",
    0x23: "'#' debug string",
    0x6E: "'#t' reply (ASCII 'net build...')",
    0xF6: "whitelist_app_add (glasses)",
}


def command_names():
    names = dict(EXTRA_NAMES)
    hdr = REPO / "src" / "ble" / "ble_cmd.h"
    try:
        text = hdr.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return names
    for match in re.finditer(r"\bBLE_CMD_([A-Z0-9_]+)\s*=\s*0x([0-9a-fA-F]+)", text):
        names.setdefault(int(match.group(2), 16), match.group(1))
    return names


def shape(data, length):
    cid = data[0]
    size = f"({length} B)" if length <= 24 else "(>24 B)"
    if cid == 0x23 and len(data) > 1:
        return "#" + chr(data[1]) if 0x20 < data[1] < 0x7F else f"23 {data[1]:02X}"
    if cid in LEN16 and len(data) >= 5:
        return f"{cid:02X} <len16> <seq> {data[4]:02X} {size}"
    if length == 1 or len(data) < 2:
        return f"{cid:02X} {size}"
    return f"{cid:02X} {data[1]:02X} .. {size}" if length > 2 else f"{cid:02X} {data[1]:02X} {size}"


class Ids:
    """Per direction: how often each command id was seen and its shapes (count, first place)."""

    def __init__(self):
        self.count = {kind: collections.Counter() for kind in DIRECTIONS}
        self.shapes = {kind: collections.defaultdict(dict) for kind in DIRECTIONS}
        self.lens = collections.defaultdict(set)
        self.timeout_only = set()

    def add(self, kind, cid, lens, form, where):
        self.count[kind][cid] += 1
        self.lens[cid].add(lens.upper())
        entry = self.shapes[kind][cid].setdefault(form, [0, where])
        entry[0] += 1


def decode_line(line):
    """(kind, match, command id, shape) of a protocol log line, None for other lines and for a
    logged packet without data bytes. The first pattern that matches decides."""
    for kind, pattern in (("tx", TX_RE), ("rx", RX_RE)):
        match = pattern.search(line)
        if match:
            data = bytes.fromhex("".join(match.group(4).split()))
            return (kind, match, data[0], shape(data, int(match.group(3)))) if data else None
    match = NATIVE_RE.search(line)
    if match:
        cid = int(match.group(2), 16)
        return "tx", match, cid, f"{cid:02X} {int(match.group(3)) & 0xFF:02X} (2 B, native)"
    match = TIMEOUT_RE.search(line)
    return ("timeout", match, int(match.group(2), 16), None) if match else None


def parse_log(raw, name):
    """(kind, lens, cid, shape, where, matched text) of every protocol line of a log."""
    events = []
    # LF only: the logs contain lone CRs
    for lineno, line in enumerate(raw.decode("utf-8", errors="replace").split("\n"), 1):
        decoded = decode_line(line)
        if decoded:
            kind, match, cid, form = decoded
            events.append((kind, match.group(1), cid, form, f"{name}:{lineno}", match.group(0)))
    return events


def read_logs(logdir):
    files = sorted(log for log in logdir.iterdir() if log.is_file() and log.suffix in (".txt", ".log"))
    seen, used, dups = {}, [], []
    ids = Ids()
    timeouts = collections.defaultdict(list)
    for path in files:
        events = parse_log(path.read_bytes(), path.name)
        if not events:
            continue
        # Copies of a session differ outside the protocol lines.
        digest = hashlib.sha1("\n".join(event[5] for event in events).encode()).hexdigest()
        if digest in seen:
            dups.append((path.name, seen[digest]))
            continue
        seen[digest] = path.name
        used.append(path.name)
        for kind, lens, cid, form, where, _text in events:
            if kind == "timeout":
                timeouts[cid].append((lens, where))
            else:
                ids.add(kind, cid, lens, form, where)
    # A timeout proves a send even without a send line.
    for cid, items in timeouts.items():
        if cid not in ids.count["tx"]:
            ids.timeout_only.add(cid)
            for lens, where in items:
                ids.add("tx", cid, lens, f"{cid:02X} (timeout only, bytes not logged)", where)
    return ids, used, dups


def vector_ids(vdir):
    out = collections.defaultdict(list)
    for path in sorted(vdir.glob("*.hex")):
        tag = None
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("#"):
                match = VECTOR_TAG_RE.match(line)
                tag = match and match.group(1)
            elif tag and line.strip():
                # a packet "# <tag>" is named after its first byte: 0x<id>_<tag>
                cid = int(line.replace(",", " ").split()[0], 16)
                out[cid].append(f"0x{cid:02x}_{tag}")
                tag = None
    return out


def unit_ids(udir):
    out = collections.defaultdict(set)
    tools = udir / "tools"
    for path in sorted(udir.rglob("*")):
        if path.suffix not in (".c", ".h") or tools in path.parents:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        rows = [re.compile(r"\b%s\(0x([0-9a-fA-F]{2})_" % macro.group(1))
                for macro in ROW_MACRO_RE.finditer(text)]
        for pattern in [UNIT_RE] + rows:
            for match in pattern.finditer(text):
                out[int(match.group(1), 16)].add(str(path.relative_to(udir)))
    return out


def hex_ids(cids):
    return " ".join(f"{cid:02X}" for cid in cids)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--logs", type=pathlib.Path, default=DEFAULT_LOGS)
    parser.add_argument("--vectors", type=pathlib.Path, default=REPO / "tests" / "unit" / "vectors")
    parser.add_argument("--unit", type=pathlib.Path, default=REPO / "tests" / "unit")
    parser.add_argument("--strict", action="store_true", help="fail on app-sent ids without a unit test")
    parser.add_argument("--no-shapes", action="store_true", help="omit the per-id forms and shapes")
    args = parser.parse_args()

    if not args.logs.is_dir():
        print(f"audit_commands: app log directory not found: {args.logs}\n"
              "  The app logs are not part of the repository; they are expected in\n"
              "  reference_material/logs/even_logs next to it. Pass --logs DIR.", file=sys.stderr)
        return 2
    ids, used, dups = read_logs(args.logs)
    if not used:
        print(f"audit_commands: no app log with BleManager send/receive lines in {args.logs}", file=sys.stderr)
        return 2

    names = command_names()
    vecs = vector_ids(args.vectors)
    units = unit_ids(args.unit)
    sent = ids.count["tx"]
    all_ids = sorted(set(sent) | set(ids.count["rx"]))
    app_sent = [cid for cid in all_ids if sent[cid]]

    print(f"Logs: {args.logs}")
    print(f"  {len(used)} files read: {', '.join(used)}")
    if dups:
        print("  skipped (same protocol lines as an earlier file): "
              + ", ".join(f"{name} (= {first})" for name, first in dups))
    print(f"Vectors: {args.vectors}  ({sum(len(packets) for packets in vecs.values())} packets)")
    print(f"Unit tests: {args.unit}  ({sum(len(files) for files in units.values())} test_0xNN_ mentions)")
    print(f"\n{'id':<3} {'name':<34} {'dir':<6} {'lens':<4} {'tx':>5} {'rx':>5}  {'vectors':>7}  {'unit':>4}")
    for cid in all_ids:
        direction = "+".join(kind for kind in DIRECTIONS if ids.count[kind][cid])
        vec_count = len(vecs.get(cid, ()))
        unit_count = len(units.get(cid, ()))
        vec_mark = str(vec_count) if vec_count else ("MISSING" if sent[cid] else "-")
        unit_mark = str(unit_count) if unit_count else ("none" if sent[cid] else "-")
        print(f"{cid:02X}  {names.get(cid, '?')[:34]:<34} {direction:<6} {''.join(sorted(ids.lens[cid])):<4} "
              f"{sent[cid]:>5} {ids.count['rx'][cid]:>5}  {vec_mark:>7}  {unit_mark:>4}")

    if not args.no_shapes:
        print("\nRequest forms (app -> glasses) and reply / report shapes (glasses -> app), with count and")
        print("first occurrence. <len16> is the u16 LE length field, <seq> the app's sequence byte.")
        for cid in all_ids:
            print(f"\n{cid:02X} {names.get(cid, '?')}")
            for kind in DIRECTIONS:
                for form, (count, where) in sorted(ids.shapes[kind][cid].items()):
                    print(f"   {kind} {form:<40} x{count:<6} {where}")
            if vecs.get(cid):
                print("   vectors: " + ", ".join(vecs[cid]))

    no_vec = [cid for cid in app_sent if not vecs.get(cid)]
    no_unit = [cid for cid in app_sent if not units.get(cid)]
    extra_vec = sorted(cid for cid in vecs if cid not in sent)
    print(f"\nids seen: {len(all_ids)} ({len(app_sent)} sent by the app, "
          f"{len(all_ids) - len(app_sent)} only from the glasses: "
          f"{hex_ids(cid for cid in all_ids if not sent[cid]) or '-'})")
    if ids.timeout_only:
        print("sent but only seen as a timeout: " + hex_ids(sorted(ids.timeout_only)))
    print("app-sent ids without a vector: " + (hex_ids(no_vec) or "none"))
    print("app-sent ids without a unit test: " + (hex_ids(no_unit) or "none")
          + ("" if args.strict else "  (fails only with --strict)"))
    print("vector ids not sent in these logs: " + (hex_ids(extra_vec) or "none"))
    return 1 if no_vec or (args.strict and no_unit) else 0


if __name__ == "__main__":
    sys.exit(main())
