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
VECTOR_TAG_RE = re.compile(r"^#\s*([a-z0-9_]+)\s*$")

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
    for m in re.finditer(r"\bBLE_CMD_([A-Z0-9_]+)\s*=\s*0x([0-9a-fA-F]+)", text):
        names.setdefault(int(m.group(2), 16), m.group(1))
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
    def __init__(self):
        self.tx = collections.Counter()
        self.rx = collections.Counter()
        self.lens = collections.defaultdict(set)
        self.forms = collections.defaultdict(dict)
        self.shapes = collections.defaultdict(dict)
        self.timeout_only = set()

    def add(self, table, counter, cid, lens, shp, where):
        counter[cid] += 1
        self.lens[cid].add(lens.upper())
        entry = table[cid].setdefault(shp, [0, where])
        entry[0] += 1


def parse_log(raw, name):
    events = []
    # LF only: the logs contain lone CRs
    for lineno, line in enumerate(raw.decode("utf-8", errors="replace").split("\n"), 1):
        where = f"{name}:{lineno}"
        for kind, rx in (("tx", TX_RE), ("rx", RX_RE)):
            m = rx.search(line)
            if m:
                data = bytes.fromhex("".join(m.group(4).split()))
                if data:
                    events.append((kind, m.group(1), data[0], shape(data, int(m.group(3))), where, m.group(0)))
                break
        else:
            m = NATIVE_RE.search(line)
            if m:
                cid, value = int(m.group(2), 16), int(m.group(3))
                events.append(("tx", m.group(1), cid, f"{cid:02X} {value & 0xFF:02X} (2 B, native)", where,
                               m.group(0)))
                continue
            m = TIMEOUT_RE.search(line)
            if m:
                events.append(("timeout", m.group(1), int(m.group(2), 16), None, where, m.group(0)))
    return events


def read_logs(logdir):
    files = sorted(p for p in logdir.iterdir() if p.is_file() and p.suffix in (".txt", ".log"))
    seen, used, dups = {}, [], []
    ids = Ids()
    timeouts = collections.defaultdict(list)
    for path in files:
        events = parse_log(path.read_bytes(), path.name)
        if not events:
            continue
        # Copies of a session differ outside the protocol lines.
        digest = hashlib.sha1("\n".join(e[5] for e in events).encode()).hexdigest()
        if digest in seen:
            dups.append((path.name, seen[digest]))
            continue
        seen[digest] = path.name
        used.append(path.name)
        for kind, lens, cid, shp, where, _ in events:
            if kind == "tx":
                ids.add(ids.forms, ids.tx, cid, lens, shp, where)
            elif kind == "rx":
                ids.add(ids.shapes, ids.rx, cid, lens, shp, where)
            else:
                timeouts[cid].append((lens, where))
    # A timeout proves a send even without a send line.
    for cid, items in timeouts.items():
        if cid not in ids.tx:
            ids.timeout_only.add(cid)
            for lens, where in items:
                ids.add(ids.forms, ids.tx, cid, lens, f"{cid:02X} (timeout only, bytes not logged)", where)
    return ids, used, dups


def vector_ids(vdir):
    out = collections.defaultdict(list)
    if vdir.is_dir():
        for p in sorted(vdir.glob("*.hex")):
            tag = None
            for line in p.read_text(encoding="utf-8").splitlines():
                if line.startswith("#"):
                    m = VECTOR_TAG_RE.match(line)
                    tag = m and m.group(1)
                elif tag and line.strip():
                    # a packet "# <tag>" is named after its first byte: 0x<id>_<tag>
                    cid = int(line.replace(",", " ").split()[0].removeprefix("0x"), 16)
                    out[cid].append(f"0x{cid:02x}_{tag}")
                    tag = None
    return out


def unit_ids(udir):
    out = collections.defaultdict(set)
    if not udir.is_dir():
        return out
    tools = udir / "tools"
    for p in sorted(udir.rglob("*")):
        if p.suffix not in (".c", ".h") or tools in p.parents:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in UNIT_RE.finditer(text):
            out[int(m.group(1), 16)].add(str(p.relative_to(udir)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--logs", type=pathlib.Path, default=DEFAULT_LOGS)
    ap.add_argument("--vectors", type=pathlib.Path, default=REPO / "tests" / "cases")
    ap.add_argument("--unit", type=pathlib.Path, default=REPO / "tests" / "unit")
    ap.add_argument("--strict", action="store_true", help="fail on app-sent ids without a unit test")
    ap.add_argument("--no-shapes", action="store_true", help="omit the per-id forms and shapes")
    args = ap.parse_args()

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
    all_ids = sorted(set(ids.tx) | set(ids.rx))
    app_sent = [c for c in all_ids if ids.tx[c]]

    print(f"Logs: {args.logs}")
    print(f"  {len(used)} files read: {', '.join(used)}")
    if dups:
        print("  skipped (same protocol lines as an earlier file): " + ", ".join(f"{a} (= {b})" for a, b in dups))
    print(f"Vectors: {args.vectors}  ({sum(len(v) for v in vecs.values())} packets)")
    print(f"Unit tests: {args.unit}  ({sum(len(v) for v in units.values())} test_0xNN_ mentions)")
    print()
    print(f"{'id':<3} {'name':<34} {'dir':<6} {'lens':<4} {'tx':>5} {'rx':>5}  {'vectors':>7}  {'unit':>4}")
    for c in all_ids:
        direction = ("tx+rx" if ids.rx[c] else "tx") if ids.tx[c] else "rx"
        vec_n = len(vecs.get(c, ()))
        unit_n = len(units.get(c, ()))
        vmark = str(vec_n) if vec_n else ("MISSING" if ids.tx[c] else "-")
        umark = str(unit_n) if unit_n else ("none" if ids.tx[c] else "-")
        print(f"{c:02X}  {names.get(c, '?')[:34]:<34} {direction:<6} {''.join(sorted(ids.lens[c])):<4} "
              f"{ids.tx[c]:>5} {ids.rx[c]:>5}  {vmark:>7}  {umark:>4}")

    if not args.no_shapes:
        print()
        print("Request forms (app -> glasses) and reply / report shapes (glasses -> app), with count and")
        print("first occurrence. <len16> is the u16 LE length field, <seq> the app's sequence byte.")
        for c in all_ids:
            print(f"\n{c:02X} {names.get(c, '?')}")
            for label, table in (("tx", ids.forms), ("rx", ids.shapes)):
                for shp, (n, where) in sorted(table[c].items()):
                    print(f"   {label} {shp:<40} x{n:<6} {where}")
            if vecs.get(c):
                print("   vectors: " + ", ".join(vecs[c]))

    no_vec = [c for c in app_sent if not vecs.get(c)]
    no_unit = [c for c in app_sent if not units.get(c)]
    extra_vec = sorted(c for c in vecs if c not in ids.tx)
    print()
    print(f"ids seen: {len(all_ids)} ({len(app_sent)} sent by the app, "
          f"{len(all_ids) - len(app_sent)} only from the glasses: "
          f"{' '.join(f'{c:02X}' for c in all_ids if not ids.tx[c]) or '-'})")
    if ids.timeout_only:
        print("sent but only seen as a timeout: " + " ".join(f"{c:02X}" for c in sorted(ids.timeout_only)))
    print("app-sent ids without a vector: " + (" ".join(f"{c:02X}" for c in no_vec) or "none"))
    print("app-sent ids without a unit test: " + (" ".join(f"{c:02X}" for c in no_unit) or "none")
          + ("" if args.strict else "  (fails only with --strict)"))
    print("vector ids not sent in these logs: " + (" ".join(f"{c:02X}" for c in extra_vec) or "none"))
    if no_vec or (args.strict and no_unit):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
