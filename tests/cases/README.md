# Shared protocol vectors

Phone-to-glasses request packets of the Even G1 protocol, grouped by feature, shared by the two
test layers (`tests/PLAN.md` section 4). Protocol knowledge both layers need lives here once, so
the unit tests and the regression scenarios cannot drift apart. The packets are the ones the
official Android app 1.6.x sends (`tests/reg/app_sequence.md` decides which exist), plus a few the
firmware handles that the app does not send (`0x4E` text, `0x2C 02` iOS).

## File format

- One file per feature: `<feature>.hex` (`navigation.hex`, `dashboard.hex`, ...), holding every
  packet of the commands that feature uses.
- First line: the feature and its command ids, `# Navigation Control (0x0A)`.
- Each packet is a title line followed by one data line:

  ```
  # arrived
  0a, 1a 00, ae, 06, 02, 4e 61 76 69 67 61 74 69 6f 6e 20 43 6f 6d 70 6c, 65 74 65 00
  ```

  - title: `# <case>`, the case alone; the command id is the packet's first byte (see Naming);
  - data: the complete packet as written to the Nordic UART RX characteristic (no ATT header, no
    padding), hex byte pairs, lower case, no `0x` prefixes. Bytes of one field are separated by a
    space and fields by `, `; long fields are cut every 16 bytes. Packets are at most 200 bytes
    here; the unit harness reads up to 256.
- A packet is the data lines between its title and the next line starting with `#`, so every
  comment is a whole line and ends the packet above it.
- No other formats: no JSON, no reply files.

## Naming

Vector names are `0x<id>_<case>`: `<id>` is the command id, the first byte of the packet, as two
lower-case hex digits; `0x23` for the `#` debug strings (`#t` = `23 74`). `<case>` is the short
snake_case title. The name is not written out anywhere: `# exit` above `0a ...` is `0x0a_exit`, and
the same case under `18` is `0x18_exit`. Names are unique across all files. Multi-packet messages
are one vector per packet: `_chunk1`, `_chunk2`, ... when every packet of the message is given;
`_chunk1` and `_last_chunk` when only the first and the last packet of a long transfer are given
(the middle chunks follow the same layout). Consumers refer to a vector by its name, wherever it is
filed; moving a vector to another file changes nothing for them.

## Specification, not recording

Vectors are complete, well-formed packets as the app sends them, not log excerpts:

- The app logs print TX packets cut after 50 bytes. Where a logged packet is longer, the logged
  bytes are kept and the rest is reconstructed from the layout and from other evidence (the app's
  own log of the same values, a `[proto]` line that prints the packet in full, the firmware's echo
  of a header). The index names the sources.
- Where content is in no log and not fixed by the layout (bitmap pixels, stock chart image, map
  data, synthetic text), the vector uses a defined synthetic value: mostly the pattern
  `data[i] = i & 0xFF` by offset in the data block, so a missed or misplaced copy is visible in a
  unit test; RLE map streams are zero-filled instead (an RLE count of 0 writes nothing).
- The bitmap upload (`0x15`/`0x20`/`0x16`) uses a test image, so the CRC in `0x16_bitmap_crc` can be
  recomputed: a 576x136 1-bpp BMP of 9854 bytes, 72-byte rows bottom-up, rows 0 and 135 all `00`,
  the others `FF` with a 0 bit at each end. It
  is sent in 51 packets of 194 bytes, the first one behind the address `00 1C 00 00` (u32 BE). The
  CRC is CRC-32 (zlib, the notes' "Crc32Xz") over the address bytes and the 9854 file bytes, sent
  big-endian (`8F CE 79 92`).
- Bytes in doubt are marked `UNVERIFIED` (see the index). The recordings against the vendor image
  in `tests/reg/ref` settle them; a vector changes only with an index entry naming the new evidence.
- Malformed packets do not belong here. Unit tests that need a wrong length, a bad chunk order or
  an oversize field build it themselves (for example by loading a vector and changing one byte).

## Sequence numbers

The app keeps one shared counter (`app_sequence.md` 5.5). In commands with a u16 length at bytes
1..2 (`06 08 09 0A 10 1E 22 24 25 26 39 3D 50 54 55 56`) the sequence byte is byte 3; in `0D 0F 4E`
it is byte 1. Each vector uses a fixed value (the logged one where there is a log). The firmware
echoes these bytes and does not check them, so consumers may patch them.
Exception: byte 1 of `0x15` is the upload packet number and is checked (00 first, +1 per packet).

## Source tags used in the index

| Tag | Meaning |
|---|---|
| `L:<file>:<line>` | `reference_material/logs/even_logs/<file>` (app 1.6.2/1.6.3; firmware 1.6.6 unless the entry says 1.6.3). Line numbers as `grep -n` counts them (LF only; the logs contain lone CRs). `2025_11_22.txt` and `2025_11_22-unicode-notifications-calendar.txt` are the same file. |
| `logs/<file>:<line>` | `reference_material/logs/<file>`: older app versions (1.6.1 and earlier). |
| `notes '<section>'` | `reference_material/Even Realities G1 Notes and RE.md`. |
| `app_sequence.md <n>` | `tests/reg/app_sequence.md`; it quotes the APK evidence (`A:` Dart, `D:` native) the vectors use where no log exists. |
| parser / firmware | the firmware function that parses the command (`src/ble/ble_proto.c` and friends), tree `main` (1.6.6 source). When the notes and the source disagree, the source wins (see the disagreements below). |

## How the layers use the files

- **Unit tests** (`tests/unit`): `common/CMakeLists.txt` embeds every `tests/cases/*.hex` at build
  time through `common/gen_vectors.cmake`; `g1_vector_load(name, buf, cap)` finds the title
  `# <case>` whose packet starts with `<id>` in any file and returns the packet's bytes, and
  `g1_send_vector(name)` feeds them to `ble_handle_rx_data` (`common/harness.h`). A new file or
  packet is picked up by the next build. Tests that cover a command are named `test_0xNN_...`.
- **Regression scenarios** (`tests/reg/run.py`): reads every packet of the same files into `V` by
  name, and scenarios send them verbatim (`V["0x2d_mac_get"]`).
- **Command audit**: `python3 tests/unit/tools/audit_commands.py` reads the app logs, lists every
  command id with its request forms and reply shapes, and exits 1 when an id the app sends has no
  vector here (`--strict` also requires a `test_0xNN_` unit test).

## Traps for test authors

- `spec_ble_command_hook` sees every packet first and treats it as a C string: `#1@`, `#2@`, `#3@`,
  `#4@`, `#5@` or `#cmd@` anywhere before the first NUL byte, or a packet that is exactly `#c`, is
  taken as a debug command instead of being dispatched. Keep those sequences out of text payloads.
- The master defers the ack of the last chunk of `0x4E`, `0x0D` and `0x0F` until the slave has
  confirmed (`deal_event_to_phone` `0xf2`); the slave answers at once.
- Several commands answer only some chunks: `0x0A` sub 2/3 answer the first and the last chunk,
  `0x15` data packets are not answered unless out of order.

## Disagreements between the sources and what the vectors follow

- `0x4E`: the notes' table has a `Size` column and u16 chunk fields; the parser reads
  `4E seq count index status pos_hi pos_lo page pages text` (u8 fields). Parser followed.
- `0x16`: the notes show `16 0D 0E` + CRC; the parser reads the CRC from bytes 1..4. Parser followed.
- `0x0D` / `0x0F`: the app calls 0x0F "translator src" (original text) and 0x0D "translator dst"
  (translated text); `ble_cmd.h` names 0x0D `TRANSLATE_SRC_TEXT` and 0x0F `TRANSLATE_DST_TEXT`. The
  vectors are named after the app's meaning (`0x0d_translate_dst_*`, `0x0f_translate_src_*`).
- `0x0A` sub 1: the notes give x/y one byte each; the parser and the log use u16 LE. Sub 6
  (arrived) is `02` + a NUL-terminated prompt, not the notes' `01`.
- `0x06` sub 1: bytes 19/20/21 are `temp_unit`, `time_disp_mode`, `date_format` in the parser (notes:
  C/F, 24H/12H, `??`). `0x06` sub 3: the notes' fixed `01 03 03` is dashboard mode, pane and
  schedule mode; mode `03` carries an event list, other values (`01`, `02` seen) carry none.
- `0x4B` / `0x04`: the notes say 180-byte JSON segments; the app sends 176 / 177 JSON bytes behind a
  4 / 3 byte header (180-byte packets). The Gadgetbridge payload from the notes is chunked like the
  app does.
- `0x08`: byte 4 is a sub-command (03 set, 04 get), not the notes' "Local / Global".
- `0x0E`: sent to R in the only logged session (notes: Left).
- `0x34`: the 1.6.x logs show only replies (no request line); the vector is the APK builder's packet.

## Index

Step: the plan step (`tests/PLAN.md` section 8) the vector belongs to, 4 (first set: bring-up,
dashboard time/mode, text, notification, quick note, navigation start) or 9 (second set).

| Vector | File | Cmd | Case | Source | Step | Unverified |
|---|---|---|---|---|---|---|
| `0x01_brightness` | `display.hex` | `01` | brightness max, manual | logs/2025_07_27_2.log:12550 | 9 | |
| `0x01_brightness_auto` | `display.hex` | `01` | brightness auto | logs/2025_07_27.log:12568 | 9 | |
| `0x03_silent_mode_off` | `settings.hex` | `03` | silent mode off | L:2025_11_11.txt:1107 | 9 | |
| `0x03_silent_mode_off_no_picture` | `settings.hex` | `03` | silent mode off no picture | L:2025_11_08.txt:2411 | 9 | |
| `0x03_silent_mode_on` | `settings.hex` | `03` | silent mode on | L:2025_11_08.txt:1358 | 9 | |
| `0x04_whitelist_chunk1` | `notification.hex` | `04` | whitelist chunk 1/2 | L:2025_11_08.txt:1128 | 4 | |
| `0x04_whitelist_chunk2` | `notification.hex` | `04` | whitelist chunk 2/2 | L:2025_11_08.txt:1128 | 4 | |
| `0x06_calendar_chunk1` | `dashboard.hex` | `06` | calendar chunk 1/2 | L:2025_11_22.txt:21318 | 9 | |
| `0x06_calendar_chunk2` | `dashboard.hex` | `06` | calendar chunk 2/2 | L:2025_11_22.txt:21330 | 9 | |
| `0x06_calendar_empty` | `dashboard.hex` | `06` | calendar, no events | L:2025_11_22.txt:446 | 9 | |
| `0x06_calendar_two_events` | `dashboard.hex` | `06` | calendar, 2 events | L:2025_11_22.txt:10106 | 9 | |
| `0x06_canvas_bg_chunk1` | `dashboard.hex` | `06` | map background chunk 1/36 | L:2026_01_02.txt:1160 | 9 | yes |
| `0x06_canvas_bg_last_chunk` | `dashboard.hex` | `06` | map background chunk 36/36 | L:2026_01_02.txt:1366 | 9 | yes |
| `0x06_canvas_icon` | `dashboard.hex` | `06` | map icon 32x32 | L:2026_01_02.txt:1385 | 9 | yes |
| `0x06_canvas_mode0` | `dashboard.hex` | `06` | map pane mode 0 | L:2026_01_02.txt:784 | 9 | |
| `0x06_news_item` | `dashboard.hex` | `06` | news item | notes Scratch + L:2025_11_12.txt:7875 | 9 | yes |
| `0x06_pane_mode` | `dashboard.hex` | `06` | dashboard mode dual + stocks | L:2025_11_12.txt:557 | 4 | |
| `0x06_stock_chunk1` | `dashboard.hex` | `06` | stock chunk 1/11 | L:2025_11_12.txt:10338 + notes + L:2025_11_12.txt:10335 | 9 | yes |
| `0x06_stock_last_chunk` | `dashboard.hex` | `06` | stock chunk 11/11 | L:2025_11_12.txt:10398 | 9 | yes |
| `0x06_time_weather` | `dashboard.hex` | `06` | time, no weather (FF FF) | L:2025_11_12.txt:567 | 4 | |
| `0x06_time_weather_sunny` | `dashboard.hex` | `06` | time + weather | L:2025_11_08.txt:1243 | 4 | |
| `0x08_head_up_action_get` | `head_up.hex` | `08` | head-up action get | L:2025_11_08.txt:1180 | 4 | |
| `0x08_head_up_action_set` | `head_up.hex` | `08` | head-up action set | APK (app_sequence.md 6.1) + notes + parser | 9 | yes |
| `0x09_teleprompter_exit` | `teleprompter.hex` | `09` | teleprompter exit | APK template (app_sequence.md 5.9) | 9 | yes |
| `0x09_teleprompter_init` | `teleprompter.hex` | `09` | teleprompter start | APK template (app_sequence.md 5.9) + parser | 9 | yes |
| `0x09_teleprompter_position` | `teleprompter.hex` | `09` | teleprompter position | APK template (app_sequence.md 5.9) + parser | 9 | yes |
| `0x0a_arrived` | `navigation.hex` | `0A` | navigation arrived | L:2026_01_03-navigation.txt:6237 | 9 | |
| `0x0a_exit` | `navigation.hex` | `0A` | navigation exit | L:2026_01_03-navigation.txt:6266 | 9 | |
| `0x0a_init` | `navigation.hex` | `0A` | navigation init | L:2026_01_03-navigation.txt:2716 | 4 | |
| `0x0a_overview_map_chunk1` | `navigation.hex` | `0A` | overview map chunk 1/9 | L:2026_01_03-navigation.txt:2829 | 9 | yes |
| `0x0a_overview_map_last_chunk` | `navigation.hex` | `0A` | overview map chunk 9/9 | L:2026_01_03-navigation.txt:2849 | 9 | yes |
| `0x0a_panoramic_chunk1` | `navigation.hex` | `0A` | panoramic map chunk 1/90 | L:2026_01_03-navigation.txt:2868 | 9 | yes |
| `0x0a_panoramic_last_chunk` | `navigation.hex` | `0A` | panoramic map chunk 90/90 | L:2026_01_03-navigation.txt:3084 | 9 | yes |
| `0x0a_sync` | `navigation.hex` | `0A` | navigation sync | L:2026_01_03-navigation.txt:2737 | 9 | |
| `0x0a_trip_status` | `navigation.hex` | `0A` | navigation trip status | L:2026_01_03-navigation.txt:2783 + L:2026_01_03-navigation.txt:2781 | 4 | |
| `0x0b_head_up_angle` | `head_up.hex` | `0B` | head-up angle 20 | L:2025_11_08.txt:1149 | 4 | |
| `0x0d_translate_dst_clear` | `translate.hex` | `0D` | translate translated clear | L:2025_11_12.txt:1521 | 9 | |
| `0x0d_translate_dst_text` | `translate.hex` | `0D` | translate translated text | L:2025_11_12.txt:3263 | 9 | |
| `0x0e_mic_off` | `translate.hex` | `0E` | microphone off | L:2025_11_12.txt:3359 | 9 | |
| `0x0e_mic_on` | `translate.hex` | `0E` | microphone on | L:2025_11_12.txt:1507 | 9 | |
| `0x0f_translate_src_clear` | `translate.hex` | `0F` | translate original clear | L:2025_11_12.txt:1512 | 9 | |
| `0x0f_translate_src_text` | `translate.hex` | `0F` | translate original text | L:2025_11_12.txt:3238 | 9 | |
| `0x10_calibration_capture` | `head_up.hex` | `10` | calibration capture | notes Scratch 0x10 | 9 | |
| `0x10_calibration_clear` | `head_up.hex` | `10` | calibration clear | L:2025_11_09.txt:1151 | 9 | |
| `0x10_calibration_set` | `head_up.hex` | `10` | calibration mode off | L:2025_11_09.txt:1162 | 9 | |
| `0x10_calibration_start` | `head_up.hex` | `10` | calibration start | notes Scratch 0x10 | 9 | |
| `0x14_ancs_enable` | `notification.hex` | `14` | ANCS enable | APK (app_sequence.md 5.9) | 9 | |
| `0x15_bitmap_first_packet` | `bitmap.hex` | `15` | bitmap upload packet 0 | notes 'File Upload (0x15)' + parser | 9 | |
| `0x15_bitmap_last_packet` | `bitmap.hex` | `15` | bitmap upload packet 50 (last) | notes 'File Upload (0x15)' + parser | 9 | |
| `0x15_bitmap_next_packet` | `bitmap.hex` | `15` | bitmap upload packet 1 | notes 'File Upload (0x15)' + parser | 9 | |
| `0x16_bitmap_crc` | `bitmap.hex` | `16` | bitmap CRC check | notes 'Bitmap Show (0x16)' + parser (CRC computed) | 9 | |
| `0x17_upgrade_mode` | `session.hex` | `17` | erase DFU slot | L:2025_11_08.txt:1596 | 9 | |
| `0x18_exit` | `bitmap.hex` | `18` | exit running feature | L:2025_11_12.txt:3386 | 9 | |
| `0x1c_translate_control` | `translate.hex` | `1C` | translate start | L:2025_11_12.txt:1496 | 9 | |
| `0x1e_quicknote_add` | `quicknote.hex` | `1E` | quick note add | L:2025_11_22.txt:682 | 4 | |
| `0x1e_quicknote_empty_slot1` | `quicknote.hex` | `1E` | quick note slot cleared | L:2025_11_12.txt:651 | 4 | |
| `0x1e_record_delete` | `quicknote.hex` | `1E` | delete audio record 1 | L:2025_11_22.txt:20250 | 9 | |
| `0x1e_record_download` | `quicknote.hex` | `1E` | download audio record 1 | L:2025_11_22.txt:20109 | 9 | |
| `0x1e_record_info` | `quicknote.hex` | `1E` | quick note record info | L:2025_11_12.txt:680 | 4 | |
| `0x1f_onboarding_start` | `onboarding.hex` | `1F` | onboarding start | logs/app_log.txt:1077 | 9 | |
| `0x20_upload_complete` | `bitmap.hex` | `20` | upload complete | notes 'File Upload (0x15)' + app_sequence.md 5.8 | 9 | |
| `0x22_status_get` | `session.hex` | `22` | status get | L:2025_11_12.txt:575 | 4 | |
| `0x23_build_info` | `session.hex` | `23` | #t build info | L:2025_11_12.txt:452 | 4 | |
| `0x23_reboot` | `session.hex` | `23` | #r reboot | APK (app_sequence.md 1, 3.4); notes | 9 | |
| `0x24_teleprompter_pause` | `teleprompter.hex` | `24` | teleprompter pause | APK template (app_sequence.md 5.9) + parser | 9 | yes |
| `0x25_teleprompter_heartbeat` | `teleprompter.hex` | `25` | teleprompter heartbeat | APK template (app_sequence.md 3.3) | 9 | yes |
| `0x26_height_depth` | `display.hex` | `26` | display height/depth (bring-up) | L:2025_11_12.txt:550 | 4 | |
| `0x26_height_depth_preview` | `display.hex` | `26` | display height/depth preview | L:2025_11_11.txt:1365 | 9 | |
| `0x26_lum_coefficient_enter` | `display.hex` | `26` | brightness calibration enter | L:2025_11_09.txt:1710 | 9 | |
| `0x26_lum_coefficient_set` | `display.hex` | `26` | brightness coefficient set | L:2025_11_09.txt:1720 | 9 | |
| `0x26_mic_on_head_up_off` | `settings.hex` | `26` | mic on head-up off | L:2025_11_08.txt:1175 | 4 | |
| `0x27_wear_detection_on` | `settings.hex` | `27` | wear detection on | APK + notes | 9 | |
| `0x29_brightness_get` | `display.hex` | `29` | brightness | L:2025_11_12.txt:496 | 4 | |
| `0x2a_anti_shake_get` | `settings.hex` | `2A` | anti-shake | L:2025_11_09.txt:330 | 4 | |
| `0x2b_silent_mode_get` | `settings.hex` | `2B` | silent mode | L:2025_11_12.txt:500 | 4 | |
| `0x2c_android` | `session.hex` | `2C` | device info, Android | L:2025_11_12.txt:410 | 4 | |
| `0x2c_ios` | `session.hex` | `2C` | device info, iOS | firmware parser | 9 | |
| `0x2d_mac_get` | `device_info.hex` | `2D` | mac addresses get | notes + parser | 9 | |
| `0x2e_whitelist_get` | `notification.hex` | `2E` | notification app list get | notes + parser | 9 | |
| `0x32_head_up_angle_get` | `head_up.hex` | `32` | head-up angle | L:2025_11_12.txt:699 | 4 | |
| `0x33_glasses_sn_get` | `device_info.hex` | `33` | glasses serial number get | notes + parser | 9 | |
| `0x34_device_sn_get` | `device_info.hex` | `34` | device serial number | APK + notes | 9 | |
| `0x35_esb_channel_get` | `device_info.hex` | `35` | esb channel get | notes + parser | 9 | |
| `0x36_notification_count_get` | `notification.hex` | `36` | notification count get | notes + parser | 9 | |
| `0x37_uptime` | `session.hex` | `37` | uptime | L:2025_11_12.txt:430 | 4 | |
| `0x38_ancs_enable_get` | `notification.hex` | `38` | ANCS get | APK (app_sequence.md 5) | 9 | |
| `0x39_running_app` | `session.hex` | `39` | running app | L:2025_11_12.txt:455 | 4 | |
| `0x3a_wear_detection_get` | `settings.hex` | `3A` | wear detection | L:2025_11_12.txt:698 | 4 | |
| `0x3b_raster_config_get` | `display.hex` | `3B` | display height/depth | L:2025_11_12.txt:535 | 4 | |
| `0x3c_notification_auto_display_get` | `notification.hex` | `3C` | notification auto display | L:2025_11_08.txt:1151 | 4 | |
| `0x3d_language_en` | `settings.hex` | `3D` | language English | L:2025_11_12.txt:715 | 4 | |
| `0x3e_burial_point` | `session.hex` | `3E` | usage counters | L:2025_11_12.txt:431 | 4 | |
| `0x3f_double_tap_get` | `settings.hex` | `3F` | double-tap action get | L:2025_11_08.txt:1183 | 4 | |
| `0x3f_long_press_get` | `settings.hex` | `3F` | long-press enable get | L:2025_11_08.txt:1186 | 4 | |
| `0x3f_lum_coefficient_get` | `display.hex` | `3F` | brightness coefficient get | L:2025_11_09.txt:1703 | 9 | |
| `0x47_unpair` | `session.hex` | `47` | unpair | L:2026_01_17.txt:404 | 9 | |
| `0x4b_gadgetbridge_chunk1` | `notification.hex` | `4B` | notification (Gadgetbridge, notes) chunk 1/2 | notes 'Notification Send (0x4B)' | 4 | |
| `0x4b_gadgetbridge_chunk2` | `notification.hex` | `4B` | notification (Gadgetbridge, notes) chunk 2/2 | notes 'Notification Send (0x4B)' | 4 | |
| `0x4b_notification_chunk1` | `notification.hex` | `4B` | notification (app) chunk 1/2 | L:2025_11_11.txt:1432 | 4 | |
| `0x4b_notification_chunk2` | `notification.hex` | `4B` | notification (app) chunk 2/2 | L:2025_11_11.txt:1432 | 4 | |
| `0x4c_clear` | `notification.hex` | `4C` | notification clear id 424 | L:2025_11_12.txt:3847 | 9 | |
| `0x4d_mtu_251` | `session.hex` | `4D` | MTU 251 | L:2025_11_08.txt:429 | 4 | |
| `0x4e_text_style7` | `text.hex` | `4E` | text style 7 | notes 'Text Set (0x4E)' + parser | 4 | |
| `0x4f_auto_display` | `notification.hex` | `4F` | auto display on 6 s | L:2025_11_08.txt:1159 | 4 | |
| `0x4f_auto_display_off` | `notification.hex` | `4F` | auto display off | L:2026_01_02.txt:905 | 9 | |
| `0x50_dashboard_lock` | `dashboard.hex` | `50` | dashboard lock | L:2025_11_11.txt:1313 | 9 | |
| `0x54_evenai_text` | `even_ai.hex` | `54` | Even AI answer | APK template (app_sequence.md 5.2) + parser | 9 | yes |
| `0x55_evenai_heartbeat` | `even_ai.hex` | `55` | Even AI heartbeat | APK template (app_sequence.md 5.2) | 9 | yes |
| `0x56_evenai_exit` | `even_ai.hex` | `56` | Even AI exit | APK template (app_sequence.md 5.2) | 9 | yes |
