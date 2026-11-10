#!/usr/bin/env python3
"""Check the g1reg/proto.py packet builders against tests/cases."""
import json
import os
import struct
import sys
import unittest

REG_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, REG_DIR)
sys.dont_write_bytecode = True

from g1reg import proto  # noqa: E402
from g1reg.proto import vector  # noqa: E402


def discover():
    return sorted(proto.vectors())


def hexs(data):
    return data.hex(" ").upper()


def test_bmp():
    """576x136 1-bpp BMP: rows 0 and 135 black, the rest white with a black pixel at both ends."""
    rows = []
    for row in range(136):
        line = bytearray(b"\x00" * 72 if row in (0, 135) else b"\xFF" * 72)
        line[0] &= 0x7F
        line[-1] &= 0xFE
        rows.append(bytes(line))
    pixels = b"".join(rows)
    header = b"BM" + struct.pack("<IHHI", 62 + len(pixels), 0, 0, 62)
    header += struct.pack("<IiiHHIIiiII", 40, 576, 136, 1, 1, 0, len(pixels), 0, 0, 2, 0)
    header += b"\x00\x00\x00\x00\xFF\xFF\xFF\x00"
    return header + pixels


def whitelist_from(v1, v2):
    config = json.loads((v1[3:] + v2[3:]).decode("utf-8"))
    rebuilt = proto.whitelist_json([(a["id"], a["name"]) for a in config["app"]["list"]],
                                   calendar=config["calendar_enable"], call=config["call_enable"],
                                   msg=config["msg_enable"], ios_mail=config["ios_mail_enable"],
                                   enable=config["app"]["enable"])
    return proto.whitelist_set(rebuilt)


def notification_from(v1, v2):
    n = json.loads((v1[4:] + v2[4:]).decode("utf-8"))["ncs_notification"]
    return proto.notification(n["msg_id"], n["app_identifier"], n["title"], n["message"], pack_id=v1[1],
                              subtitle=n["subtitle"], time_s=n["time_s"], date=n["date"],
                              display_name=n["display_name"], action=n["action"])


def calendar_events(body):
    events, at = [], 4
    for _ in range(body[3]):
        texts = []
        for tag in (1, 2, 3):
            assert body[at] == tag, "calendar field tag %d expected at %d" % (tag, at)
            texts.append(body[at + 2:at + 2 + body[at + 1]])
            at += 2 + body[at + 1]
        events.append(tuple(texts))
    return events


def calendar_from(*packets):
    body = b"".join(p[9:] for p in packets)
    return proto.dashboard_calendar(packets[0][3], calendar_events(body), mode=body[0], pane=body[1])


def le(data):
    return int.from_bytes(data, "little")


# vector name -> builder call on the vector's bytes v
VECTORS = {
    # session
    "0x4d_mtu_251": lambda v: proto.mtu_set(251),
    "0x2c_android": lambda v: proto.device_info_get(proto.ANDROID),
    "0x2c_ios": lambda v: proto.device_info_get(proto.IOS),
    # GET
    "0x29_brightness_get": lambda v: proto.brightness_get(),
    "0x2a_anti_shake_get": lambda v: proto.anti_shake_get(),
    "0x2b_silent_mode_get": lambda v: proto.silent_mode_get(),
    "0x2d_mac_get": lambda v: proto.mac_get(),
    "0x2e_whitelist_get": lambda v: proto.whitelist_get(),
    "0x32_head_up_angle_get": lambda v: proto.head_up_angle_get(),
    "0x33_glasses_sn_get": lambda v: proto.glasses_sn_get(),
    "0x34_device_sn_get": lambda v: proto.device_sn_get(),
    "0x35_esb_channel_get": lambda v: proto.esb_channel_get(),
    "0x36_notification_count_get": lambda v: proto.notification_count_get(),
    "0x37_uptime": lambda v: proto.uptime_get(),
    "0x38_ancs_enable_get": lambda v: proto.ancs_enable_get(),
    "0x39_running_app": lambda v: proto.running_app_get(0x02),
    "0x3a_wear_detection_get": lambda v: proto.wear_detection_get(),
    "0x3b_raster_config_get": lambda v: proto.raster_config_get(),
    "0x3c_notification_auto_display_get": lambda v: proto.notification_auto_display_get(),
    "0x3e_burial_point": lambda v: proto.burial_point_get(),
    "0x3f_double_tap_get": lambda v: proto.hardware_get(5),
    "0x3f_lum_coefficient_get": lambda v: proto.hardware_get(6),
    "0x3f_long_press_get": lambda v: proto.hardware_get(7),
    "0x22_status_get": lambda v: proto.status_get(0x0A),
    # SET
    "0x01_brightness": lambda v: proto.brightness_set(0x2A, auto=False),
    "0x01_brightness_auto": lambda v: proto.brightness_set(0x2A, auto=True),
    "0x03_silent_mode_on": lambda v: proto.silent_mode_set(True, keep_picture=True),
    "0x03_silent_mode_off": lambda v: proto.silent_mode_set(False),
    "0x03_silent_mode_off_no_picture": lambda v: proto.silent_mode_set(False, keep_picture=True),
    "0x04_whitelist_chunk1": lambda v: whitelist_from(v, vector("0x04_whitelist_chunk2"))[0],
    "0x04_whitelist_chunk2": lambda v: whitelist_from(vector("0x04_whitelist_chunk1"), v)[1],
    "0x06_time_weather": lambda v: proto.dashboard_time_weather(
        0x09, 0x6914F54E, 0x19A79DE3A0D, icon=0xFF, temperature=0xFF, fahrenheit=True, twelve_hour=True),
    "0x06_time_weather_sunny": lambda v: proto.dashboard_time_weather(
        0x09, le(v[5:9]), le(v[9:17]), icon=0x10, temperature=20, fahrenheit=True, twelve_hour=True),
    "0x06_pane_mode": lambda v: proto.dashboard_pane_mode(0x08, 1, 1),
    "0x06_calendar_empty": lambda v: proto.dashboard_calendar(0x09, [])[0],
    "0x06_calendar_two_events": lambda v: calendar_from(v)[0],
    "0x06_calendar_chunk1": lambda v: calendar_from(v, vector("0x06_calendar_chunk2"))[0],
    "0x06_calendar_chunk2": lambda v: calendar_from(vector("0x06_calendar_chunk1"), v)[1],
    "0x06_stock_chunk1": lambda v: proto.chunk_packet(0x06, 0xD6, 4, 11, 1, v[9:]),
    "0x06_stock_last_chunk": lambda v: proto.chunk_packet(0x06, 0xE0, 4, 11, 11, v[9:]),
    "0x06_news_item": lambda v: proto.dashboard_pane_data(0x52, 5, v[9:])[0],
    "0x06_canvas_bg_chunk1": lambda v: proto.chunk_packet(0x06, 0x1E, 7, 36, 1, v[9:]),
    "0x06_canvas_bg_last_chunk": lambda v: proto.chunk_packet(0x06, 0x43, 7, 36, 36, v[9:]),
    "0x06_canvas_icon": lambda v: proto.dashboard_canvas_icon(0x46, 0xAC, 0x34, v[16:], mode=1),
    "0x06_canvas_mode0": lambda v: proto.dashboard_pane_data(0x13, 7, b"\x01\x04\x00")[0],
    "0x08_head_up_action_get": lambda v: proto.head_up_action_get(0x00),
    "0x08_head_up_action_set": lambda v: proto.head_up_action_set(0x00, 0),
    "0x0b_head_up_angle": lambda v: proto.head_up_angle_set(0x14, 1),
    "0x0e_mic_on": lambda v: proto.mic_set(True),
    "0x0e_mic_off": lambda v: proto.mic_set(False),
    "0x10_calibration_clear": lambda v: proto.calibration_clear(0x01),
    "0x10_calibration_set": lambda v: proto.calibration_set(0x02, enable=False),
    "0x10_calibration_start": lambda v: proto.calibration_set(0x0C, enable=True),
    "0x10_calibration_capture": lambda v: proto.calibration_set(0x03, enable=True, take_pitch=True),
    "0x14_ancs_enable": lambda v: proto.ancs_enable_set(True),
    "0x1c_translate_control": lambda v: proto.translate_control(False, 4, 2),
    "0x1e_quicknote_empty_slot1": lambda v: proto.quick_note_set(0x0F, 1),
    "0x1e_quicknote_add": lambda v: proto.quick_note_set(0x11, 1, "testing Your", "Oh wow"),
    "0x1e_record_info": lambda v: proto.quick_note_record_info(0x16),
    "0x1e_record_download": lambda v: proto.quick_note_record_download(0x1C, 1),
    "0x1e_record_delete": lambda v: proto.quick_note_record_delete(0x1D, 1),
    "0x1f_onboarding_start": lambda v: proto.tutorial_control(1, 1),
    "0x26_height_depth": lambda v: proto.display_height_depth(0x00, 6, 9),
    "0x26_height_depth_preview": lambda v: proto.display_height_depth(0x02, 5, 1, preview=True),
    "0x26_lum_coefficient_enter": lambda v: proto.lum_coefficient_hold(0x10, True),
    "0x26_lum_coefficient_set": lambda v: proto.lum_coefficient(0x11, 100),
    "0x26_mic_on_head_up_off": lambda v: proto.mic_on_head_up(0x00, on=False),
    "0x27_wear_detection_on": lambda v: proto.wear_detection_set(True),
    "0x3d_language_en": lambda v: proto.language_set(0x1A, 2),
    "0x4f_auto_display": lambda v: proto.notification_auto_display_set(True, 6),
    "0x4f_auto_display_off": lambda v: proto.notification_auto_display_set(False, 15),
    "0x50_dashboard_lock": lambda v: proto.dashboard_lock_set(0x00),
    # content
    "0x4b_gadgetbridge_chunk1": lambda v: notification_from(v, vector("0x4b_gadgetbridge_chunk2"))[0],
    "0x4b_gadgetbridge_chunk2": lambda v: notification_from(vector("0x4b_gadgetbridge_chunk1"), v)[1],
    "0x4b_notification_chunk1": lambda v: notification_from(v, vector("0x4b_notification_chunk2"))[0],
    "0x4b_notification_chunk2": lambda v: notification_from(vector("0x4b_notification_chunk1"), v)[1],
    "0x4c_clear": lambda v: proto.notification_clear(0x1A8),
    "0x4e_text_style7": lambda v: proto.text_set(0x00, v[9:], style=7, canvas=1, position=0, page=1, page_count=1)[0],
    "0x15_bitmap_first_packet": lambda v: proto.file_upload(test_bmp())[0],
    "0x15_bitmap_next_packet": lambda v: proto.file_upload(test_bmp())[1],
    "0x15_bitmap_last_packet": lambda v: proto.file_upload(test_bmp())[-1],
    "0x20_upload_complete": lambda v: proto.file_upload_complete(),
    "0x16_bitmap_crc": lambda v: proto.bitmap_show(proto.crc32_xz(proto.BITMAP_ADDRESS + test_bmp())),
    "0x18_exit": lambda v: proto.bitmap_hide(),
    "0x17_upgrade_mode": lambda v: proto.upgrade_mode(),
    "0x09_teleprompter_init": lambda v: proto.teleprompter_init(0x00, v[12:-8], le(v[-8:]), mode=1, countdown_s=3),
    "0x09_teleprompter_position": lambda v: proto.teleprompter_position(0x01, 50, 20, le(v[-8:])),
    "0x09_teleprompter_exit": lambda v: proto.teleprompter_exit(0x02),
    "0x24_teleprompter_pause": lambda v: proto.teleprompter_suspend(0x03, True, le(v[6:14]), 6),
    "0x25_teleprompter_heartbeat": lambda v: proto.teleprompter_sync(0x04, 1),
    "0x0a_init": lambda v: proto.nav_init(0x6D),
    "0x0a_trip_status": lambda v: proto.nav_trip_status(
        0x75, 3, 211, 123, "15 min", "5km", "Eden Bower Court ", "111m", "0.0km/h"),
    "0x0a_overview_map_chunk1": lambda v: proto.chunk_packet(0x0A, 0x78, 2, 9, 1, v[9:]),
    "0x0a_overview_map_last_chunk": lambda v: proto.chunk_packet(0x0A, 0x80, 2, 9, 9, v[9:]),
    "0x0a_panoramic_chunk1": lambda v: proto.chunk_packet(0x0A, 0x81, 3, 90, 1, v[9:]),
    "0x0a_panoramic_last_chunk": lambda v: proto.chunk_packet(0x0A, 0xDA, 3, 90, 90, v[9:]),
    "0x0a_sync": lambda v: proto.nav_sync(0x6E, 1),
    "0x0a_exit": lambda v: proto.nav_exit(0xB2),
    "0x0a_arrived": lambda v: proto.nav_arrived(0xAE),
    "0x0d_translate_dst_clear": lambda v: proto.translate_text(0x0D, 0x00, b"\r", screen_status=0)[0],
    "0x0d_translate_dst_text": lambda v: proto.translate_text(0x0D, 0x01, b" \r \rHello.", screen_status=2)[0],
    "0x0f_translate_src_clear": lambda v: proto.translate_text(0x0F, 0x00, b"\r", screen_status=0)[0],
    "0x0f_translate_src_text": lambda v: proto.translate_text(0x0F, 0x01, v[7:], screen_status=0)[0],
    "0x54_evenai_text": lambda v: proto.evenai_answer(0x00, v[12:], total=1, index=1, flag=0, progress=100),
    "0x55_evenai_heartbeat": lambda v: proto.evenai_heartbeat(0x01, 0),
    "0x56_evenai_exit": lambda v: proto.evenai_exit(0x02),
    "0x47_unpair": lambda v: proto.unpair(),
    # debug strings
    "0x23_build_info": lambda v: proto.build_info(),
    "0x23_reboot": lambda v: proto.reboot(),
}


class VectorTest(unittest.TestCase):
    def test_builders_match_vectors(self):
        names = discover()
        unmapped = [n for n in names if n not in VECTORS]
        for name in names:
            if name not in VECTORS:
                continue
            with self.subTest(vector=name):
                expected, where = proto.vectors()[name]
                built = VECTORS[name](expected)
                self.assertIsInstance(built, bytes, "the VECTORS entry must give one packet")
                self.assertEqual(hexs(built), hexs(expected),
                                 "\nbuilder: %s\nvector:  %s (%s)" % (hexs(built), hexs(expected), where))
        if unmapped:
            sys.stderr.write("\ntest_proto_vectors: %d vector(s) without a builder in VECTORS: %s\n"
                             % (len(unmapped), ", ".join(unmapped)))
        if not names:
            sys.stderr.write("\ntest_proto_vectors: no vectors in %s (nothing to compare)\n" % proto.VECTOR_DIR)

    def test_table_names(self):
        for name in VECTORS:
            self.assertRegex(name, r"^0x[0-9a-f]{2}_\w+$")
        for name in set(VECTORS) & set(discover()):
            with self.subTest(vector=name):
                self.assertEqual(vector(name)[0], int(name[2:4], 16), "the name's command byte")

    def test_logged_app_packets(self):
        none = b""
        self.assertEqual(hexs(VECTORS["0x06_time_weather"](none)),
                         "06 16 00 09 01 4E F5 14 69 0D 3A DE 79 9A 01 00 00 FF FF 01 01 00")
        self.assertEqual(hexs(proto.quick_note_set(0x29, 1)), "1E 10 00 29 03 01 00 01 00 01 00 01 00 01 00 00")
        self.assertEqual(hexs(proto.display_height_depth(0, 5, 9)), "26 08 00 00 02 00 05 09")
        trip = VECTORS["0x0a_trip_status"](none)
        self.assertEqual((len(trip), hexs(trip[:10])), (0x34, "0A 34 00 75 01 03 D3 00 7B 00"))
        chunks = proto.notification(16, "nodomain.freeyourgadget.gadgetb", "Test notification",
                                    "This is a test notification from Gadgetbridge", time_s=1749606217,
                                    date="2025-06-10 18:43:37", display_name="Gadgetbridge")
        self.assertEqual([len(c) for c in chunks[:-1]], [180] * (len(chunks) - 1))
        self.assertEqual(hexs(chunks[0][:4]), "4B 00 %02X 00" % len(chunks))
        self.assertEqual(hexs(proto.heartbeat(7)), "25 06 00 07 04 07")
        self.assertEqual(len(test_bmp()), 9854)
        self.assertEqual(len(proto.file_upload(test_bmp())), 51)


class DecoderTest(unittest.TestCase):

    def test_device_info(self):
        master = bytes.fromhex("2C 66 64 64 F9 80 19 01 06 06 01 06 06 00 00 00 00 00 00 00")
        d = proto.decode(master)
        self.assertEqual((d.name, d["master_version"], d["slave_version"], d["battery_left"]),
                         ("device_info", "1.6.6", "1.6.6", 100))
        self.assertEqual(proto.normalise(master), "2C 66" + " .." * 17 + " 00")
        self.assertEqual(proto.normalise(master, keep=proto.VOLATILE),
                         "2C 66 64 64 F9 80 19 01 06 06 01 06 06 00 00 00 00 00 00 00")

    def test_events(self):
        d = proto.decode(bytes.fromhex("F5 0A 64") + bytes(18))
        self.assertEqual((d["event_name"], d["level"]), ("battery", 100))
        self.assertEqual(proto.normalise(d.data), "F5 0A .." + " 00" * 18)
        self.assertEqual(proto.decode(bytes.fromhex("F5 11") + bytes(19))["event_name"], "paired")
        self.assertEqual(proto.decode(bytes.fromhex("F5 12 05") + bytes(18))["level"], 5)

    def test_status_and_text(self):
        head_up = proto.decode(bytes.fromhex("22 0A 00 00 01 00 00 00 00 01"))
        self.assertEqual((head_up.name, head_up["page"]), ("status_head_up", 1))
        self.assertEqual(proto.decode(bytes.fromhex("4D C9") + bytes(18))["status"], "ok")
        info = b"net build time: 2025-10-22 14:21:16, app build time 2025-10-22 14:20:59, ver 1.6.6, JBD DeviceID 4010\n\r"
        info += bytes(200 - len(info))
        self.assertEqual(proto.decode(info).name, "build_info")
        self.assertIsNone(proto.decode(info).cmd)
        self.assertEqual(proto.normalise(info),
                         repr("net build time: <build_time>, app build time <build_time>, ver <version>, "
                              "JBD DeviceID 4010\n\r") + " (200 B)")
        self.assertEqual(proto.normalise(bytes.fromhex("3E C9") + bytes(284)), "3E C9 <usage 284 B>")


if __name__ == "__main__":
    unittest.main()
