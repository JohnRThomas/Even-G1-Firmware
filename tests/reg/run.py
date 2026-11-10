#!/usr/bin/env python3
"""Regression scenarios: run each on the simulator and compare what the glasses do with the vendor 1.6.6 image.

    python3 tests/reg/run.py [-k NAME,...] [-j N] [--firmware ZIP] [--keep]
    python3 tests/reg/run.py --record --firmware even_g1_1.66.zip [-k NAME,...]

A scenario is a list of timed inputs, (seconds, lens, input): a phone packet (bytes), a Renode monitor command
(str), or "?" and a monitor command whose output goes into the trace. A run is reduced to a trace of
(seconds, lens, item): every line of the virtual phone (link events, the packets both ways, audio as streams),
boots and crashes, the query outputs, and each panel content that stays up for HOLD seconds. A run passes when its
trace and the vendor image's (ref/NAME.txt) pair up item for item, each pair within TOL seconds. --record merges the
vendor image's traces under two radio schedules (SEEDS): an item of only one is optional (~), as some outcomes
depend on radio timing. A failed run keeps its scratch directory: the log, the frames and a PNG of each panel
content in its trace; the vendor image with --keep gives the same of the reference.
"""
import argparse
import base64
import glob
import hashlib
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIM = os.path.join(REPO, "tools", "simulator")
REF = os.path.join(REPO, "tests", "reg", "ref")              # the vendor image's trace of each scenario, NAME.txt
VENDOR = "748836f2d9d092b0783cbb7672fdccec4f730051cad27f095bbb156ac3e5d316"   # sha256 of even_g1_1.66.zip
TOL, HOLD = 2.0, 0.5
SEEDS = (1, 2)              # radio schedules --record merges; a run uses the first (g1sim's)

# ------------------------------------------------------------------------------------------------------- inputs

M, S, B = "master", "slave", "both"
PH, IMU, TOUCH, NPM, OPT, NFC, JBD, PDM = (
    "sysbus.vcentral ", "sysbus.twim2.lsm6dso ", "sysbus.twim2.touch ", "sysbus.twim1.npm1300 ",
    "sysbus.twim1.opt3001 ", "sysbus.twim1.st25dv ", "spim_a.jbd ", "sysbus.pdm0 ")
h = bytes.fromhex
MTU, INFO = h("4DFB"), h("2C01")
V = {}                                              # the shared packets of tests/cases by name, "0x2d_mac_get"
for path in glob.glob(os.path.join(REPO, "tests", "cases", "*.hex")):
    for tag, data in re.findall(r"^# (\w+)\n([^#]*)", open(path, encoding="utf-8").read(), re.M):
        V["0x%02x_%s" % (h(data.replace(",", " "))[0], tag)] = h(data.replace(",", " "))


def u(n, size=2):
    return (n & (1 << 8 * size) - 1).to_bytes(size, "little")


def f(cmd, seq, sub, body=b""):
    """cmd, u16 length of the packet, seq, sub, body"""
    return bytes((cmd,)) + u(5 + len(body)) + bytes((seq & 255, sub)) + bytes(body)


def chunks(data, size):
    return [data[i:i + size] for i in range(0, len(data), size)] or [b""]


def chunked(cmd, seq, sub, data, size=180, head=b""):
    """Framed chunks with u16 count and index from 1; a new seq for each, as the app does."""
    c = chunks(data, size)
    return [f(cmd, seq + i, sub, u(len(c)) + u(i + 1) + head + p) for i, p in enumerate(c)]


def split(head, data, size, mid=b""):
    """Chunks: head, count, index from 0, mid, piece."""
    c = chunks(data, size)
    return [head + bytes((len(c), i)) + mid + p for i, p in enumerate(c)]


def noise(n, key):
    return hashlib.shake_128(key.encode()).digest(n)


def tlv(*texts):
    return b"".join(bytes((k, len(x))) + x.encode() for k, x in enumerate(texts, 1))


def compact(o):
    return json.dumps(o, separators=(",", ":")).encode()


def text(seq, s, style=7, page=1, pages=1):
    return split(bytes((0x4E, seq)), s.encode(), 191, bytes((style << 4 | 1, 0, 0, page, pages)))


def whitelist(*apps):
    return split(b"\x04", compact({"calendar_enable": True, "call_enable": True, "msg_enable": True,
                                   "ios_mail_enable": True,
                                   "app": {"list": [{"id": i, "name": n} for i, n in apps], "enable": True}}), 177)


def notification(msg_id, app, title, message, name):
    return split(b"\x4B\x00", compact({"ncs_notification": {
        "msg_id": msg_id, "action": 0, "app_identifier": app, "title": title, "subtitle": "", "message": message,
        "time_s": 1762899959, "date": "2025-11-11 22:25:59", "display_name": name}}), 176)


def clock(seq, s, icon=0x10, temp=20, us=0, ms=None):
    """0x06 sub 1: local time sent as UTC, in s and ms; weather icon and temperature; Fahrenheit and 12 h (us)"""
    return f(6, seq, 1, u(s, 4) + u(s * 1000 if ms is None else ms, 8) + bytes((icon, temp & 255, us, us, 0)))


def pane(seq, mode, number):
    return f(6, seq, 6, bytes((mode, number)))


def calendar(seq, events, mode=1):
    return chunked(6, seq, 3, bytes((mode, 3, 3 if events else 2)) + (
        bytes((len(events),)) + b"".join(tlv(*e) for e in events) if events else b""))


def stock(n, *texts, status):
    return bytes((1, 1, 2, n, 1)) + tlv(*texts) + bytes((6, 1, status, 7)) + u(1886) + noise(1886, "chart%d" % n)


def news(n, source, s):
    return bytes((1, 2, 2, n, 1)) + tlv(source) + b"\x02" + u(len(s)) + s.encode()


def icon(seq, x, y):
    return f(6, seq, 7, u(1) + u(1) + bytes((1, 4, 3)) + u(x) + u(y) + noise(128, "icon"))


def bmp(w=576, h=136, pad=2):
    """1-bpp BMP; a multiple of 4 bytes long, as the QSPI flash wants"""
    px = noise(w * h // 8, "bitmap")
    return (b"BM" + struct.pack("<IHHIIiiHHIIiiII", 62 + len(px) + pad, 0, 0, 62, 40, w, h, 1, 1, 0, len(px), 0, 0, 2, 0)
            + b"\0\0\0\0\xff\xff\xff\0" + px + bytes(pad))


def transcript(seq, s, total=1, index=1):
    return f(0x52, seq, 2, u(total) + u(index) + b"\0\0\0" + s.encode())


def case(level, lid):
    """A message of the charging case's NFC reader (pt command 0x13), with its checksum"""
    m = bytes((0x13, 1, 2, 2, level, lid))
    return '%sRfPutMailboxMessage "%s"' % (NFC, (m + bytes((sum(m) & 255,))).hex().upper())


def at(t, lens, *inputs):
    return [(t, lens, x) for x in inputs]


def q(t, lens, *commands):
    return at(t, lens, *("?" + c for c in commands))


def tx(t, lens, *packets, gap=0.0):
    return [(t + gap * i, lens, p) for i, p in enumerate(packets)]


def both(t, *packets, gap=0.0):
    """To L (the slave) first, then R: the master redraws both lenses over ESB."""
    return tx(t, S, *packets, gap=gap) + tx(t + gap * max(0, len(packets) - 1) + 0.25, M, *packets, gap=gap)


def gesture(t, name, lens=B):
    return at(t, lens, IMU + 'PlayGesture "%s"' % name)


def pose(t, g):
    return at(t, B, *("%sAcceleration%s %s" % (IMU, axis, v) for axis, v in zip("XYZ", g)))


def taps(t, lens, n=1, tap="Tap 80"):
    return [(t + 0.2 * i, lens, TOUCH + tap) for i in range(n)]


def hold(t, release, lens):
    return at(t, lens, TOUCH + "Press") + at(release, lens, TOUCH + "Release")


def on(t=12, lens=B, os=1, hb=None):
    """The phone (os 1 Android, 2 iOS) switches on and sends the app's first packets; hb: heartbeat seconds."""
    return (at(t, lens, PH + "HeartbeatSeconds %g" % hb) if hb is not None else []) + at(
        t, lens, PH + "Enabled true", MTU, bytes((0x2C, os)))


def state(t):
    return q(t, B, *(PH + p for p in ("Connected", "Encrypted", "Bonded", "AttMtu")))


DON, CLOSE = gesture(5, "don"), gesture(11.5, "head_down")      # the dashboard up, or closed, before the phone
SINE = at(None, B, PDM + "ConfigureSine 8000 1000")
LEVEL, UP45, UP70, UP81, FLAT = ((0.342, 0.0, 0.94), (0.707, 0.0, 0.707), (0.94, 0.0, 0.342), (0.988, 0.0, 0.156),
                                 (0.0, 0.0, 1.0))
T1435, T2105, APP_TIME = 1762958100, 1762981500, (0x6914F54E, 0x19A79DE3A0D)
APP = ("com.example.chat", "Chat")
EVENTS = [("Team meeting", "15:00-16:00", "Room 4"), ("Dentist", "17:30-18:00", "Main St 12"),
          ("Call Anna", "19:00-19:15", "Phone"), ("Gym", "20:00-21:00", "Downtown"), ("Book club", "21:30-22:30", "Library")]
IMAGE = bmp()


def app_time(seq, later=0):
    return clock(seq, APP_TIME[0] + later, 0xFF, 0xFF, 1, APP_TIME[1] + 1000 * later)


def bringup(lens):
    """The app's bring-up after it connects, per lens, a request every 0.4 s."""
    master = lens == M
    groups = [
        (0, [h("3E")] * master + [h("37"), b"#t", f(0x39, 2, 1), f(0x39, 4, 1)] + [h("29")] * master
         + [h("2B"), INFO] + [h("3B")] * master + [f(0x26, 0, 2, b"\0\3\3"), pane(8, 1, 1), app_time(9)]
         + [f(0x22, 0x0A, 1)] * master),
        (6, [h("29")] * master + [h("2B"), INFO, pane(0x0B, 1, 1), app_time(0x0C, 4)] + [f(0x22, 0x0D, 1)] * master),
        (9, [f(0x1E, seq, 3, u(1) + u(1) + bytes((slot, 0, 1, 0, 1, 0, 0))) for seq, slot in
             ((0x0F, 1), (0x10, 2), (0x11, 3), (0x12, 4))] + [f(0x1E, 0x16, 1, b"\1")] * master),
        (11.5, [f(0x39, 0x17, 1)] + [h("32")] * master + [h("3A")] + [h("2A")] * master),
        (14, [f(0x3D, 0x1A, 1, b"\2"), f(0x3D, 0x1B, 1, b"\2")]),
        (16, [h("32"), h("0B1401"), h("4F0106"), f(0x26, 0, 8, b"\1"), f(0x08, 0, 4, b"\0"), h("3F05"), h("3F07")]
         if master else whitelist(("nodomain.freeyourgadget.gadgetb", "Gadgetbridge"), ("com.discord", "Discord"),
                                  ("com.google.android.gm", "Gmail")) + [h("3C"), h("4F0106")]),
    ]
    return [a for offset, packets in groups for a in tx(20 + offset, lens, *packets, gap=0.4)]


def fault(cmd, lens=M, t=None, seconds=19, tapped=(15, 16.5)):
    """A faulty peripheral from the start (t None) or from t; taps on both lenses; model state at the end."""
    return (seconds, at(t, lens, cmd), DON, [a for x in tapped for a in at(x, B, TOUCH + "Tap")],
            q(seconds - 0.5, B, *[TOUCH + p for p in ("ConfigWriteCount", "CommitWritten", "MagicWritten",
                                                      "EventsArmed", "PressesDelivered")],
              IMU + "AccelerometerEnabled", NPM + "WatchdogResetCount"))


def dashboard(seconds, steps, head, *more):
    """steps: (t, packets to both lenses, or "tap" on the master); head down and up again at head."""
    acts = gesture(14, "don") + on() + gesture(head[0], "head_down") + gesture(head[1], "head_up")
    for t, x in steps:
        acts += at(t, M, TOUCH + "Tap") if x == "tap" else both(t, *x, gap=0.04)
    return (seconds, acts) + more


def session(seconds, *more):
    """A feature screen: worn, the dashboard closed again before the phone is on; the 2C keep-alive every 10 s."""
    return (seconds, DON, CLOSE, on(hb=0), [(t, B, INFO) for t in range(22, seconds, 10)]) + more


SCENARIOS = {
    "boot": (30, DON),
    "pair": (30, on(), state(29)),
    "app_bringup": (41, on(), bringup(M), bringup(S), state(40)),
    "device_info": (40, on(), tx(20, B, *(V[n] for n in (
        "0x2d_mac_get", "0x33_glasses_sn_get", "0x34_device_sn_get", "0x35_esb_channel_get",
        "0x36_notification_count_get", "0x38_ancs_enable_get")), h("49"), h("1400"), V["0x38_ancs_enable_get"],
        V["0x14_ancs_enable"], V["0x38_ancs_enable_get"], gap=0.4),
        tx(26, S, V["0x04_whitelist_chunk1"], V["0x04_whitelist_chunk2"], gap=0.5),
        tx(28, S, V["0x4b_notification_chunk1"], V["0x4b_notification_chunk2"], gap=0.5),
        at(31, B, V["0x36_notification_count_get"]), at(32, S, h("4C0000019D")),
        at(34, B, V["0x36_notification_count_get"])),
    "settings_persist": (58, DON, on(), [(t + 0.4 * i, B, h(x)) for t in (18, 25, 52) for i, x in enumerate(
        ("29", "32", "2B", "3A", "3F05", "3F07", "3F08"))],
        tx(21, M, h("011000"), h("0B2801"), h("030C00"), h("2701"), f(0x26, 0x20, 5, b"\2"), f(0x26, 0x21, 7, b"\1"),
           f(0x26, 0x22, 8, b"\1"), gap=0.4), tx(21, S, h("030C00"), h("2701"), gap=0.4),
        taps(34, B, 5), at(37, B, MTU), q(45, B, NPM + "WatchdogResetCount"), state(57)),
    "debug_control": (46, DON, on(), gesture(23, "head_down"), gesture(26, "head_up"), [(t, M, x) for t, x in (
        (17, b"#2@0"), (18.5, f(0x39, 0x20, 1)), (20, b"#2@1"), (21.5, f(0x39, 0x21, 1)), (30, h("050100")),
        (31.5, f(0x39, 0x22, 1)), (33, h("050101")), (34.5, f(0x39, 0x23, 1)), (36, h("05030B")),
        (36.5, f(0x39, 0x24, 1)), (37, h("050306")), (37.5, f(0x39, 0x25, 1)), (39, h("050203")),
        (39.5, f(0x39, 0x26, 1)), (40, h("050202")), (40.5, f(0x39, 0x27, 1)))]),
    "debug_unpair": (40, on(), at(20, B, b"#1@title@msg"), at(22, B, V["0x36_notification_count_get"]),
                     at(24, B, b"#c"), at(25, B, MTU), at(34, B, INFO), state(38)),
    "hw_init": (8, q(7.5, B, JBD + "GetJournal", JBD + "FrameCounter", *(TOUCH + p for p in (
        "ConfigWriteCount", "CommitWritten", "MagicWritten", "EventsArmed")), IMU + "AccelerometerEnabled",
        IMU + "GyroscopeEnabled", *(NPM + p for p in ("LdswEnabled 1", "LdswEnabled 2", "BuckEnabled 1",
                                                       "BuckEnabled 2", "WatchdogResetCount", "ChargingEnabled")))),
    "hw_mic": (38, SINE, on(), [(t, lens, bytes((0x0E, x))) for t, lens, x in (
        (16, M, 1), (23, M, 0), (26, M, 1), (29, M, 0), (31, S, 1), (34, S, 0))], at(20, M, PDM + "ConfigureSilence")),
    "hw_touch": (46, DON, on(), taps(18, M), taps(21, M, 2), at(23, M, f(0x26, 0, 5, b"\4")), taps(25, S, 2),
                 taps(28, M, 2), taps(31, M, 3), at(34, B, h("2B")), taps(36, M, 3), at(39, B, h("2B")),
                 hold(40, 43, S)),
    "hw_touch_reboot": (42, DON, on(), at(16, B, h("3F05")), at(17, M, f(0x26, 0, 5, b"\1")), at(19, B, h("3F05")),
                        taps(22, M, 5), at(25.5, M, MTU), at(34, B, h("3F05"), "?" + NPM + "WatchdogResetCount")),
    "hw_imu_head": (51, DON, on(), pose(18, LEVEL), pose(22, UP81), at(26, M, h("0B1E01")), at(27, M, h("32")),
                    pose(28, LEVEL), pose(32, UP45), pose(36, UP70), pose(40, LEVEL), at(43, M, h("0B3C01")),
                    at(44, M, h("32")), pose(45, UP45)),
    "hw_imu_wear": (45, DON, on(), at(16, B, h("3A")), pose(17, FLAT), at(21, B, h("2701")), at(22, B, h("3A")),
                    pose(26, UP81), at(30, B, h("2700")), at(31, B, h("3A")), pose(32, LEVEL), pose(35, UP81),
                    at(40, M, IMU + "FaultNak true")),
    "hw_imu_calibration": (36, DON, on(), pose(15, LEVEL), at(19, B, V["0x10_calibration_start"]),
                           at(23, B, V["0x10_calibration_capture"]), at(26, B, V["0x10_calibration_set"]),
                           at(29, B, V["0x10_calibration_clear"]), at(31, B, f(0x10, 5, 1))),
    "hw_light_lux": (47, at(None, M, OPT + "Lux 0"), DON, on(), at(18, M, b"#i"), at(19, M, h("011E00")),
                     at(20, M, h("29")), at(23, M, OPT + "Lux 300"), at(27, M, b"#i"), at(28, M, h("010400")),
                     at(29, M, h("29")), pose(31, LEVEL), at(33.5, M, b"#i"), pose(35, UP81), at(40, M, OPT + "Lux 0"),
                     at(41.5, M, h("011400")), at(43, M, b"#i")),
    "hw_light_delay": (35, at(None, M, OPT + "Lux 300", OPT + "FaultConversionDelay 15000"), DON, on(),
                       [a for t in (16, 22, 26, 33) for a in q(t, M, OPT + "ResultRegister")], at(20, M, b"#i"),
                       at(21, M, h("010C00")), at(33.5, M, b"#i")),
    "hw_battery_sweep": (55, at(None, S, NPM + "BatteryVoltageMilliVolts 3900"), on(), [
        a for i, mv in enumerate(range(4200, 3399, -100))
        for a in at(18 + 3 * i, M, NPM + "BatteryVoltageMilliVolts %d" % mv) + at(20.5 + 3 * i, M, INFO, b"#v")],
        at(20.5, S, INFO), at(46, M, NPM + "BatteryCurrentMilliAmps 50"), at(48.5, M, INFO, b"#v"),
        at(50, M, NPM + "BatteryCurrentMilliAmps -40"), at(52.5, M, INFO, b"#v")),
    "hw_battery_low": (22, at(None, M, NPM + "BatteryVoltageMilliVolts 3500"), DON),
    "hw_battery_stuck": (175, at(None, S, NPM + "BatteryVoltageMilliVolts 3400"), q(174, B, NPM + "WatchdogResetCount")),
    "hw_battery_temp": (48, DON, on(), at(20, M, NPM + "BatteryTemperatureCelsius 65"), at(22.5, B, h("2B")),
                        at(22.5, M, INFO, b"#v"), at(24, M, NPM + "BatteryTemperatureCelsius 25"), pose(26, LEVEL),
                        pose(29, UP81), at(32, B, h("2B")), at(34, B, h("030A00")), pose(37, LEVEL), pose(40, UP81),
                        at(45, B, h("2B"))),
    "hw_battery_case": (57, at(None, B, NPM + "BatteryVoltageMilliVolts 3900"), on(),
                        [a for t in (23, 31.5, 52) for a in q(t, M, NPM + "ChargingEnabled", NPM + "ChargerStatus")],
                        at(24, M, NFC + "RfEnableMailbox", "sysbus.saadc SetAnalogInput 3 600000", NFC + "RfField true",
                           NPM + "VbusConnected true", NPM + "BatteryCurrentMilliAmps -40"),
                        [(26 + 1.5 * i, M, case(0xB7, 7)) for i in range(5)],
                        [(33.5 + 1.5 * i, M, case(55, 6)) for i in range(4)],
                        [(39.5 + 1.5 * i, M, case(55, 7)) for i in range(2)],
                        q(26.5, M, NFC + "RfReadMailboxMessage"), q(35.5, M, NFC + "RfReadMailboxMessage"),
                        at(31, M, INFO, b"#v"),
                        at(42.5, M, NFC + "RfField false", "sysbus.saadc SetAnalogInput 3 0",
                           NPM + "VbusConnected false", NPM + "BatteryCurrentMilliAmps 0"), at(53, M, INFO, b"#v")),
    "hw_fault_light_wrongid_master": fault(OPT + "FaultWrongId true"),
    "hw_fault_light_nak_master": fault(OPT + "FaultNak true"),
    "hw_fault_light_nak_master_mid": fault(OPT + "FaultNak true", t=10),
    "hw_fault_imu_wrongid_master": fault(IMU + "FaultWrongId true"),
    "hw_fault_imu_wrongid_slave": fault(IMU + "FaultWrongId true", S),
    "hw_fault_imu_nak_master": fault(IMU + "FaultNak true"),
    "hw_fault_imu_nak_slave": fault(IMU + "FaultNak true", S),
    "hw_fault_imu_nak_master_mid": fault(IMU + "FaultNak true", t=15, seconds=21, tapped=()),
    "hw_fault_touch_nak_master": fault(TOUCH + "FaultNak true"),
    "hw_fault_touch_nak_slave": fault(TOUCH + "FaultNak true", S),
    "hw_fault_touch_nak_master_mid": fault(TOUCH + "FaultNak true", t=14, seconds=22, tapped=(12, 15, 16.5)),
    "hw_fault_nfc_wrongid_master": fault(NFC + "FaultWrongId true"),
    "hw_fault_nfc_nak_master": fault(NFC + "FaultNak true"),
    "hw_fault_nfc_nak_master_mid": fault(NFC + "FaultNak true", t=10),
    "hw_fault_display_wrongid_master": fault(JBD + "FaultWrongId true"),
    "hw_fault_pmic_nak_master": fault(NPM + "FaultNak true"),
    "hw_fault_pmic_nak_slave": fault(NPM + "FaultNak true", S),
    "hw_fault_pmic_nak_master_mid": fault(NPM + "FaultNak true", t=4, seconds=40, tapped=()) + (
        q(39.5, M, JBD + "FrameCounter"),),
    "events_gestures": (53, DON, on(), taps(20, M), taps(23, S), taps(26, M, 2), gesture(30, "head_up"),
                        gesture(34, "head_down"), gesture(38, "head_up"), taps(42, M, 3), taps(46, M, 3),
                        at(44.5, B, h("2B")), at(48.5, B, h("2B")), state(52)),
    "events_long_press": (48, DON, on(), hold(20, 22, M), hold(36, 38, S), state(47)),
    "events_wear": (48, on(), at(18, B, h("2701")), at(18.5, B, h("3A")), at(22, B, "sysbus.radio RssiSample 30"),
                    at(35, B, "sysbus.radio RssiSample 60"), at(33, B, h("2B")), at(44, B, h("2B")), state(47)),
    "link_keepalive": (82, on(lens=M), on(lens=S, hb=0), [(t, S, INFO) for t in range(22, 73, 10)], state(81)),
    "link_silence": (64, on(lens=M, hb=0), on(lens=S, os=2, hb=0), at(48, M, MTU), state(63)),
    "link_disconnect": (36, on(), at(20, M, PH + "Disconnect", MTU), at(20, S, PH + "Enabled false"),
                        at(21, S, PH + "Enabled true", MTU), at(30, B, INFO), state(35)),
    "link_unbond": (38, on(), at(20, B, h("47")), at(21, B, MTU), at(32, B, INFO), state(37)),
    "link_reboot": (44, DON, on(), at(20, S, b"#r"), at(34, S, INFO), state(43)),
    "esb_sync": (46, gesture(16, "don"), on(hb=0), at(22, B, INFO), at(32, B, INFO),
                 at(12, M, pane(5, 1, 0), clock(6, 1762950896)), at(26, M, *text(7, "Text sent to the master lens only.")),
                 at(33, M, h("18")), at(37, M, h("4A02"))),
    "esb_reboot_slave": (50, DON, on(hb=0), [(t, M, INFO) for t in (22, 32, 42)], at(18, S, b"#r"), at(19, S, MTU, INFO),
                         gesture(28, "don", S), at(30, S, INFO), at(40, S, INFO)),
    "upgrade_mode": (68, DON, on(hb=0), at(20, B, V["0x17_upgrade_mode"]), at(56, M, PH + "Enabled false"),
                     at(62, M, PH + "Enabled true"), at(62.5, M, MTU), state(66)),
    "image": (37, DON, CLOSE, on(hb=0), at(22, B, INFO), at(32, B, INFO),
              tx(17, B, *(bytes((0x15, i & 255)) + b"\0\x1c\0\0" * (i == 0) + p for i, p in enumerate(chunks(IMAGE, 194))),
                 gap=0.1),
              at(23, B, h("200D0E")), at(24, B, b"\x16" + zlib.crc32(b"\0\x1c\0\0" + IMAGE).to_bytes(4, "big")),
              at(31, B, h("18"))),
    "notification": (46, gesture(14, "don"), on(), both(14, h("4F0106")), at(15, S, h("3C")),
                     tx(16, S, *whitelist(("nodomain.freeyourgadget.gadgetb", "Gadgetbridge"), APP), gap=0.5),
                     at(17.5, S, h("2E")),
                     tx(22, S, *notification(7, APP[0], "Lunch at noon?", "Meet at the cafe on the corner.", APP[1]),
                        gap=0.5),
                     at(27, M, f(0x22, 1, 1)), at(32, S, h("4C00000007")), at(36, M, f(0x22, 2, 1))),
    "notification_display": (58, gesture(14, "don"), on(), tx(14, S, *whitelist(APP), gap=0.5), both(15, h("4F0106")),
                             gesture(22, "head_down"), tx(25, S, *notification(
                                 11, APP[0], "Auto display on", "This one pops up while you look down.", APP[1]),
                                 gap=0.5),
                             both(35, h("4F000F")), at(36, S, h("3C")), tx(38, S, *notification(
                                 12, APP[0], "Auto display off", "This one waits on the dashboard.", APP[1]), gap=0.5),
                             gesture(46, "head_up"), tx(50, S, *notification(
                                 13, "com.example.unknown", "Unknown app", "Not in the whitelist.", "Unknown"), gap=0.5),
                             at(54, M, f(0x22, 1, 1))),
    "text": (40, gesture(14, "don"), on(), both(22, V["0x4e_text_style7"]), both(32, h("18"))),
    "text_evenai": (60, gesture(14, "don"), on(), [a for t, seq, s, style, page, pages in (
        (22, 2, "Even AI answer, page one of two.", 3, 1, 2), (30, 3, "Even AI answer, page two of two.", 4, 2, 2),
        (41, 4, "Second answer, page one of two.", 3, 1, 2), (45, 5, "Manual scroll, page one of two.", 5, 1, 2),
        (49, 6, "Manual scroll, page two of two.", 5, 2, 2), (53, 7, "No network.", 6, 1, 1))
        for a in both(t, *text(seq, s, style, page, pages))], at(47.5, M, TOUCH + "Tap")),
    "text_evenai_app": (48, gesture(14, "don"), on(), both(22, f(0x54, 1, 1, b"\1\0\0")), both(25, V["0x54_evenai_text"]),
                        both(28, f(0x55, 1, 5, b"\0")), both(31, f(0x55, 2, 5, b"\0")), both(34, V["0x56_evenai_exit"]),
                        both(38, f(0x54, 3, 1, b"\0\0\0"))),
    "even_ai": (42, SINE, DON, on(), hold(17, 20, S), [(t, lens, p) for t, p in (
        (22, f(0x54, 0x10, 3, u(1) + u(1) + bytes((0, 0, 100)) + b"Paris is the capital of France.")),
        (25, f(0x55, 0x11, 5, b"\0")), (27, f(0x56, 0x12, 8, b"\0")), (30, f(0x54, 0x13, 1, b"\2\0\0")))
        for lens in (S, M)]),
    "even_ai_timeout": (42, SINE, DON, on(), hold(15, 18, S)),
    "dashboard_time": dashboard(52, [(21 + 2 * i, [pane(1, 0, 0)] * (i == 0) + [clock(2 + i, T1435, *w)]) for i, w in
                                     enumerate(((0xFF, 0xFF), (1, 20), (3, 5), (5, 12), (7, 25), (10, 31), (13, 0),
                                                (16, 38), (0xFF, 0xFF)))] + [(39, [clock(20, T2105, 1, 20, 1)])],
                                (42, 46), at(21.7, M, f(0x22, 30, 1))),
    "dashboard_panes": dashboard(56, [(21, [clock(1, T1435, 1, 20)])] + [s for i, (mode, n) in enumerate(
        [(0, n) for n in range(6)] + [(1, n) for n in range(6)] + [(2, 0)])
        for s in ((22 + 2 * i, [pane(2 + i, mode, n)]), (23 + 2 * i, "tap"))], (48, 51)),
    "dashboard_calendar": dashboard(52, [(21, [clock(1, T1435, 1, 20), pane(2, 1, 3)]), (23, calendar(3, EVENTS[:1])),
                                         (25, calendar(4, EVENTS)), (27, "tap"), (29, "tap"), (31, "tap"),
                                         (33, [pane(5, 0, 3)]), (36, calendar(6, [], 0))], (43, 47),
                                    both(34.5, f(0x58, 40, 1, b"\0")), at(40.5, M, f(0x50, 41, 1, b"\1"))),
    "dashboard_stocks": dashboard(50, [
        (21, [clock(1, T1435, 1, 20), pane(2, 1, 1)]),
        (22, chunked(6, 3, 4, stock(1, "GME.XNYS", "GameStop Corp", "21.36", "-0.09", "-0.42%", status=2))),
        (25, chunked(6, 20, 4, stock(2, "AAPL.XNAS", "Apple Inc", "270.10", "+1.25", "+0.46%", status=1))),
        (28, "tap"), (30, [pane(40, 0, 1)]), (32, [pane(41, 1, 2)]),
        (33, chunked(6, 42, 5, news(1, "CBS News", "Sunny afternoon with a light wind from the west, highs near 20 degrees."))),
        (35, chunked(6, 43, 5, news(2, "BBC", "Second headline of the day for the news pane of the regression test."))),
        (37, "tap"), (39, [pane(44, 0, 2)]), (41, "tap")], (43, 46)),
    "dashboard_canvas": dashboard(40, [(21, [clock(1, T1435, 1, 20), pane(2, 1, 4), chunked(6, 3, 7, b"\1\4\0")[0]]),
                                       (23, chunked(6, 4, 7, b"\1\4\2\1" + noise(6392, "map"))),
                                       (28, [icon(50, 0xAC, 0x34)]), (31, [icon(51, 0x40, 0x10)])], (34, 36.5)),
    "teleprompter": session(39, at(17, B, V["0x09_teleprompter_init"]),
                            [(18 + 2 * i, M, f(0x25, 0x11 + i, 4, bytes((i + 1,)))) for i in range(9)],
                            at(23, B, V["0x09_teleprompter_position"]), at(26, B, V["0x24_teleprompter_pause"]),
                            at(29, B, f(0x24, 5, 6, b"\0" + u(1762981210349, 8) + u(10, 4))),
                            at(33, B, V["0x09_teleprompter_exit"])),
    "navigation": session(43, at(17, B, V["0x0a_init"]),
                          [(17.5 + i, M, f(0x0A, 0x20 + i, 4, bytes((2 * i + 1,)))) for i in range(20)],
                          at(18, B, V["0x0a_trip_status"]),
                          tx(18.5, B, *chunked(0x0A, 0x78, 2, b"".join(bytes((17, x)) for x in noise(272, "overview")),
                                               185), gap=0.05),
                          tx(19.5, B, *chunked(0x0A, 0x81, 3, noise(16592, "panoramic"), 185, b"\0"), gap=0.04),
                          at(29, B, V["0x0a_arrived"]), at(37, B, V["0x0a_exit"])),
    "translate": session(35, at(17, B, V["0x1c_translate_control"]), at(17.5, M, V["0x0e_mic_on"]),
                         at(18, B, V["0x0f_translate_src_clear"]), at(19, B, V["0x0d_translate_dst_clear"]),
                         at(20, B, V["0x0f_translate_src_text"]), at(21, B, V["0x0d_translate_dst_text"]),
                         at(27, M, V["0x0e_mic_off"]), at(28, B, V["0x18_exit"])),
    "quicknote": session(36, at(17, B, V["0x1e_quicknote_add"]), gesture(18, "head_up"), gesture(23, "head_down"),
                         at(25, B, f(0x51, 0x30, 1, b"\1"))),
    "countdown": session(36, at(17, B, h("070500000001")), at(26, B, h("075A00000001")), at(30, B, h("070000000000"))),
    "transcribe": session(38, at(17.5, M, V["0x0e_mic_on"]), [(t, B, p) for t, p in (
        (17, f(0x52, 0x40, 1, b"\1")), (19, transcript(0x41, "Hello from the transcription test")),
        (21, f(0x53, 0x42, 3, b"\1")),
        (23, transcript(0x43, "The first chunk of a longer transcription carries this sentence, ", 2, 1)),
        (23.5, transcript(0x44, "and the second chunk finishes it.", 2, 2)), (25, f(0x53, 0x45, 3, b"\2")),
        (26, f(0x52, 0x46, 3, b"\x2A")), (27, f(0x52, 0x47, 1, b"\3")), (29, f(0x52, 0x48, 1, b"\4")),
        (31, f(0x52, 0x49, 1, b"\0")), (32, f(0x53, 0x4A, 3, b"\3")), (33, f(0x52, 0x4B, 3, b"\x2B")))]),
    "onboarding": session(38, [(t, B, p) for t, p in (
        (17, V["0x1f_onboarding_start"]), (18, h("1F1200")), (19, h("1F12")), (20, h("1F0C02")), (23, h("1F0803")),
        (26, h("1F0704")), (29, h("1F0205")), (32, h("1F0A06")))]),
}

# --------------------------------------------------------------------------------------------------------- runs


def timeline(name, seed=SEEDS[0]):
    """The scenario's seconds, its g1sim options (--execute for inputs at time None, --at for the others) and its
    queries, numbered in the echo markers around them."""
    seconds, *parts = SCENARIOS[name]
    args, queries = [] if seed == SEEDS[0] else ["--execute", "emulation SetSeed %d" % seed], []
    acts = sorted((a for part in parts for a in part), key=lambda a: -1 if a[0] is None else a[0])
    if any(x == PH + "Enabled true" for _, _, x in acts):
        args += ["--execute", 'emulation SetGlobalQuantum "0.00005"']            # as g1sim.py --phone does
    for t, lens, x in acts:
        for one in (M, S) if lens == B else (lens,):
            cmd = ("slave: " if one == S else "") + (PH + 'SendPacket "%s"' % x.hex().upper() if isinstance(x, bytes)
                                                     else x.lstrip("?"))
            if x[:1] == "?":
                queries.append((t, one, x))
            for c in ['echo "@@%d@@"' % (len(queries) - 1), cmd, 'echo "@@@@"'] if x[:1] == "?" else [cmd]:
                args += ["--execute", c] if t is None else ["--at", "%g %s" % (t, c)]
    return seconds, args, queries


def start(name, seed, firmware, renode):
    """The scenario on its own copy of the simulator, which starts with fresh flash."""
    out = tempfile.mkdtemp(prefix="g1reg-%s-" % name)
    sim = os.path.join(out, "sim")
    shutil.copytree(SIM, sim, ignore=shutil.ignore_patterns(".renode", ".git", "__pycache__", "g1_extstore_left.bin",
                                                            "g1_extstore_right.bin"))
    seconds, args, _ = timeline(name, seed)
    with open(os.path.join(out, "argv.json"), "w") as f:      # not on the command line: Windows caps it at 32 KiB
        json.dump([os.path.join(sim, "g1sim.py"), firmware, "--renode", renode, "--seconds", "%g" % seconds,
                   "--frames", out] + args, f, indent=0)
    with open(os.path.join(out, "log.txt"), "w") as log:
        return subprocess.Popen([sys.executable, "-c", "import json, runpy, sys; sys.argv = json.load(open(sys.argv[1]));"
                                 " runpy.run_path(sys.argv[0], run_name='__main__')", os.path.join(out, "argv.json")],
                                cwd=sim, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                start_new_session=True), out, time.time()


def kill(p):
    """The simulator and the Renode under it."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
    else:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass


# ------------------------------------------------------------------------------------------------------- traces

LENS = {"g1": M, "g1_slave": S}
PHONE = re.compile(r"\] (g1|g1_slave)/vcentral: +([\d.]+)s  (.*)")
UART = re.compile(r"\] (g1|g1_slave)/uart(0|_net): \[host:[^|]*\|virt: *([\d.]+)(m?s)[^]]*\] ?(.*)")
FATAL = ("ASSERTION FAIL", "FATAL ERROR", "watch dog reset", "bt hci cmd send sync failed", "Kernel panic", "HardFault")


def monitor(log):
    """The monitor's output: log records cut out, also from the middle of a line of it."""
    return re.sub(r"\d\d:\d\d:\d\d\.\d+ \[\w+\] [^\n]*\n", "", log)


# Bytes [from, to) that differ between builds or runs: the times and CRCs of 0x21's audio records, the battery
# levels in 0x2C (the fuel gauge runs on its own clock; for that, its F5 0A events are left out), the uptime in 0x37.
MASKS = {b"\x21": (7, None), b"\x2C\x66": (2, 4), b"\x37\x37": (2, 6)}


def packet(d):
    """A received packet, masked; text replies (the '#' commands) as text, without build dates and battery level."""
    s = d.rstrip(b"\0")
    if s and all(32 <= c < 127 or c in (10, 13) for c in s):
        s = re.sub(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d", "<date>", repr(s.decode()))
        return re.sub(r"B:\d+", "B:..", s) + " (%d B)" % len(d)
    x = d.hex(" ").upper().split()
    for prefix, (a, b) in MASKS.items():
        if d.startswith(prefix):
            x[a:b] = [".."] * len(x[a:b])
    return " ".join(x)


def trace(out, seconds, queries):
    """The run as sorted (seconds, lens, item)."""
    items, audio = [], {}
    log = open(os.path.join(out, "log.txt"), encoding="utf-8", errors="replace").read()
    for n, output in re.findall(r"@@(\d+)@@(.*?)@@@@", monitor(log), re.S):      # journal entries without times
        lines = [re.sub(r"^#\d+ t=\S+ op=0x", "", s.strip()) for s in output.splitlines() if s.strip()]
        t, lens, x = queries[int(n)]
        items.append((t, lens, "%s = %s" % (x, " | ".join(lines))))
    for line in log.splitlines():
        g = PHONE.search(line) if "/vcentral:" in line else None
        if g:
            lens, t, x = LENS[g[1]], float(g[2]), g[3].strip()
            if x.startswith("rx F1 "):                      # audio: where a stream starts and ends
                a = audio.get(lens)
                if a and t - a[1] < 1:
                    a[1] = t
                    continue
                items += [(a[1], lens, "audio end")] if a else []
                audio[lens], x = [t, t], "audio"
            elif x.startswith("rx F5 0A "):
                continue
            elif x.startswith("rx "):
                x = "rx " + packet(h(x[3:]))
            items.append((t, lens, x))
        g = UART.search(line) if "/uart" in line else None
        if g:
            lens, t = LENS[g[1]], float(g[3]) / (1000 if g[4] == "ms" else 1)
            items += [(t, lens, "boot " + ("app" if g[2] == "0" else "net"))] if "*** Booting" in g[5] else []
            items += [(t, lens, "fatal: " + p) for p in FATAL if p in g[5]]
    items += [(a[1], lens, "audio end") for lens, a in audio.items()]
    for lens in (M, S):                         # panel contents that stay HOLD s, each once until another one does
        rows, last = [], None
        path = os.path.join(out, lens + ".frames")
        for line in open(path) if os.path.isfile(path) else []:
            p = line.split()
            if len(p) == 8 or len(p) == 3 and rows:
                rows.append((float(p[0]), p[6][:12] if len(p) == 8 else rows[-1][1]))
        rows = [r for i, r in enumerate(rows) if not i or r[1] != rows[i - 1][1]] + [(seconds, None)]
        for (t, sha), (end, _) in zip(rows, rows[1:]):
            if end - t >= HOLD and sha != last:
                items.append((t, lens, "frame " + sha))
                last = sha
    return sorted((round(t, 3), lens, x) for t, lens, x in items)


def png(out, items):
    """A PNG of each panel content in the trace, 4-bit grey like the frame buffer."""
    chunk = lambda kind, data: struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    for lens in (M, S):
        want = {x[6:] for _, one, x in items if one == lens and x.startswith("frame ")}
        path = os.path.join(out, lens + ".frames")
        for line in open(path) if os.path.isfile(path) else []:
            p = line.split()
            if len(p) == 8 and p[6][:12] in want:
                want.discard(p[6][:12])
                fb = zlib.decompress(base64.b64decode(p[7]), -15)
                with open(os.path.join(out, "%s_%s.png" % (lens, p[6][:12])), "wb") as f:
                    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 640, 480, 4, 0, 0, 0, 0))
                            + chunk(b"IDAT", zlib.compress(b"".join(b"\0" + fb[i:i + 320] for i in range(0, len(fb), 320))))
                            + chunk(b"IEND", b""))


def diff(ref, got):
    """Reference items without an equal run item TOL s or less away (-), and run items left over (+). Equal items
    pair up in order; optional reference items ("~", seen under one seed only) take run items left over."""
    pool, out = {}, []
    for t, lens, x in got:
        pool.setdefault((lens, x), []).append(t)
    for t, lens, x in sorted(ref, key=lambda i: i[2][:1] == "~"):
        ts = pool.get((lens, x.lstrip("~")), [])
        match = next((u for u in ts if abs(u - t) <= TOL), None)
        if match is not None:
            ts.remove(match)
        elif x[:1] != "~":
            out.append(("-", t, lens, x))
    return sorted(out + [("+", t, lens, x) for (lens, x), ts in pool.items() for t in ts], key=lambda d: d[1])


def merge(ref, got):
    """A reference from two traces of the vendor image: the items of only one become optional."""
    d = diff(ref, got)
    missing = {i[1:] for i in d if i[0] == "-"}
    return sorted([(t, lens, "~" + x if (t, lens, x) in missing else x) for t, lens, x in ref]
                  + [(t, lens, "~" + x) for sign, t, lens, x in d if sign == "+"])


def load(name):
    path = os.path.join(REF, name + ".txt")
    return [(float(t), lens, x) for t, lens, x in (line.rstrip("\n").split(None, 2) for line in open(
        path, encoding="utf-8"))] if os.path.isfile(path) else None


def save(name, items):
    os.makedirs(REF, exist_ok=True)
    with open(os.path.join(REF, name + ".txt"), "w", encoding="utf-8", newline="\n") as f:
        f.writelines("%8.3f %-6s %s\n" % i for i in items)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-k", metavar="NAME,...", help="only the scenarios whose name contains one of these")
    parser.add_argument("-j", type=int, default=2, metavar="N", help="simulators at once (default 2)")
    parser.add_argument("--firmware", metavar="ZIP", help="update zip (default: dfu_application_<VERSION>.zip)")
    parser.add_argument("--record", action="store_true", help="write the references (the vendor image only)")
    parser.add_argument("--keep", action="store_true", help="keep the scratch directories of passed runs too")
    args = parser.parse_args()
    version = ".".join(re.findall(r"(?:MAJOR|MINOR|PATCHLEVEL) = (\d+)", open(os.path.join(REPO, "VERSION")).read()))
    firmware = os.path.abspath(args.firmware or os.path.join(REPO, "dfu_application_%s.zip" % version))
    local = os.path.join(SIM, ".renode", "renode.exe" if os.name == "nt" else "renode")
    renode = os.environ.get("RENODE") or (local if os.path.isfile(local) else shutil.which("renode"))
    if not os.path.isfile(firmware) or not renode:
        sys.exit("g1reg: no %s" % ("firmware %s (give --firmware)" % firmware if renode else
                                   "Renode (run tools/simulator/setup.sh or setup.bat, or set $RENODE)"))
    if args.record and hashlib.sha256(open(firmware, "rb").read()).hexdigest() != VENDOR:
        sys.exit("g1reg: --record takes the vendor image, even_g1_1.66.zip")
    names = [n for n in SCENARIOS if not args.k or any(k in n for k in args.k.split(","))]
    jobs = [(n, seed) for n in names for seed in (SEEDS if args.record else SEEDS[:1])]
    print("g1reg: %s, %d scenario(s), -j %d" % (firmware, len(names), args.j), flush=True)
    running, failed, tries, recorded = {}, [], [], {}
    try:
        while jobs or running:
            while jobs and len(running) < args.j:
                job = jobs.pop(0)
                running[job] = start(*job, firmware, renode)
            time.sleep(0.5)
            for job, (p, out, began) in list(running.items()):
                (name, seed), (seconds, _, queries) = job, timeline(*job)
                if p.poll() is None and time.time() < began + 600 + 30 * seconds:
                    continue
                del running[job]
                if p.poll() is None:
                    kill(p)
                    p.wait()
                log = open(os.path.join(out, "log.txt"), encoding="utf-8", errors="replace").read()
                shutil.rmtree(os.path.join(out, "sim"), ignore_errors=True)
                if "CommandHistory.Save" in log and tries.count(job) < 3:    # two Renodes wrote their shared
                    tries.append(job)                                        # history file at once: again
                    jobs.insert(0, job)
                    shutil.rmtree(out, ignore_errors=True)
                    continue
                got = trace(out, seconds, queries)
                if p.returncode or "Renode is quitting" not in monitor(log):
                    diffs = [("!", seconds, "-", "the simulator failed (exit %s):\n%s" % (p.returncode, log[-3000:]))]
                elif args.record:
                    diffs = []
                    recorded.setdefault(name, []).append(got)
                    if len(recorded[name]) == len(SEEDS):
                        save(name, merge(*recorded[name]))
                else:
                    ref = load(name)
                    diffs = diff(ref, got) if ref is not None else [("!", 0, "-", "no reference")]
                print("%s  %-32s %4.0f s%s" % ("FAIL" if diffs else "REC " if args.record else "PASS",
                                               name + (" seed %d" % seed) * args.record, time.time() - began,
                                               "   " + out if diffs or args.keep else ""))
                for d in diffs[:40]:
                    print("      %s %8.3f %-6s %s" % d)
                if len(diffs) > 40:
                    print("      ... %d more" % (len(diffs) - 40))
                sys.stdout.flush()
                if diffs or args.keep:
                    png(out, got)
                else:
                    shutil.rmtree(out, ignore_errors=True)
                failed += [name] if diffs else []
    except KeyboardInterrupt:
        sys.exit("g1reg: interrupted")
    finally:
        for p, _, _ in running.values():
            kill(p)
    failed = sorted(set(failed))
    print("g1reg: %d passed, %d failed %s" % (len(names) - len(failed), len(failed), " ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
