/*
 * Glasses -> phone reports. 0xF5 events: F5 <id> <payload>, 21 bytes.
 * 0x22 status, head up: 22 0A 00 00 01 <unread> <low power> <mode> <pane> <page>;
 * tap: 22 08 00 00 02 <mode> <pane> <page>.
 */
#include "ble/ble_proto.h"
#include "dashboard/dashboard.h"
#include "fakes.h"
#include "notify/notify.h"

#define F5_FRAME_LEN       21
#define PROXY_MSG_DATA_MAX 20

enum {
  MODE_FULL = 0,
  MODE_DUAL = 1,
  MODE_MINIMAL = 2
};
enum {
  PANE_NOTES = 0,
  PANE_STOCK = 1,
  PANE_NEWS = 2,
  PANE_CALENDAR = 3,
  PANE_MAP = 4,
  PANE_EMPTY = 5
};

static int responses_sent;

/* as proxy_thread.c: the master writes the newest queued reply, once, to the phone */
static int forward_response(void) {
  int last = g1_esb_response_log.count - 1;

  if (g1_state()->is_master == G1_ROLE_SLAVE) {
    return 0;
  }
  if (last >= responses_sent) {
    g1_ble()->write(g1_esb_response_log.data[last], MIN(g1_esb_response_log.len[last], PROXY_MSG_DATA_MAX));
    responses_sent = g1_esb_response_log.count;
  }
  return 0;
}

static void pages(uint8_t notes, uint8_t stocks, uint8_t news, uint8_t calendar) {
  notes_index = notes;
  stocks_index = stocks;
  news_index = news;
  calendar_index = calendar;
}

static void events_before(void *fixture) {
  ARG_UNUSED(fixture);
  send_response_data_to_ble_fake.custom_fake = forward_response;
  responses_sent = 0;
  /* dashboard.c has no harness reset hook */
  memset(&dashboard_prv, 0, sizeof(dashboard_prv));
  pages(0, 0, 0, 0);
}

ZTEST_SUITE(events, NULL, NULL, events_before, NULL, NULL);

static void report(uint8_t id, uint8_t arg) {
  uint8_t evt[3] = {0xf5, id, arg};

  zassert_equal(deal_event_to_phone(g1_ble(), evt), 0, "event 0x%02x", id);
}

/* ---- 0xF5 ---- */

/* Byte 2 of the packet (CB) is not payload: sent as 00. */
ZTEST(events, test_events_without_payload_are_f5_id_and_zeros_21_bytes) {
  static const uint8_t ids[] = {
    0x00, /* double tap */
    0x01, /* single tap */
    0x02, /* head up */
    0x03, /* head down */
    0x04, /* silent on */
    0x05, /* silent off */
    0x06, /* worn */
    0x07, /* not worn */
    0x08, /* lid open */
    0x0b, /* lid closed */
    0x0c, 0x0d, 0x10, 0x11, /* BLE paired */
    0x13, 0x14, 0x15, 0x16, /* onboarding */
    0x17, /* long press held */
    0x18, /* long press released */
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, /* dashboard open */
    0x1f, /* dashboard close */
    0x20, /* translate double tap */
  };
  char hex[8];

  for (size_t index = 0; index < ARRAY_SIZE(ids); index++) {
    report(ids[index], PKCS7_PACK_MORE);
    snprintf(hex, sizeof(hex), "f5 %02x", ids[index]);
    expect_frame_padded(hex, F5_FRAME_LEN);
  }
  expect_no_frame();
}

ZTEST(events, test_charging_event_0x09_carries_the_charging_flag) {
  report(0x09, 1);
  expect_frame_padded("f5 09 01", F5_FRAME_LEN);
  report(0x09, 0);
  expect_only_frame_padded("f5 09 00", F5_FRAME_LEN);
}

/* 93..96 % are spread over 94..99 so 100 is reported only when the gauge reads 100. */
ZTEST(events, test_battery_event_0x0a_reports_the_level) {
  static const struct {
    uint8_t level, soc_flag, reported;
  } rows[] = {
    {0x00, 0, 0x00}, {0x01, 0, 0x01}, {0x32, 0, 0x32}, {0x56, 0, 0x56}, {0x58, 0, 0x58}, {0x5c, 0, 0x5c},
    {0x5d, 0, 0x5e}, {0x5d, 1, 0x5f}, {0x5e, 0, 0x60}, {0x5e, 1, 0x61}, {0x5f, 0, 0x62}, {0x5f, 1, 0x62},
    {0x60, 0, 0x63}, {0x60, 1, 0x63}, {0x61, 0, 0x64}, {0x63, 1, 0x64}, {0x64, 0, 0x64},
  };
  char hex[16];

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    g1_state()->battery_left = rows[row].level;
    g1_state()->battery_soc_flag = rows[row].soc_flag;
    report(0x0a, 0);
    snprintf(hex, sizeof(hex), "f5 0a %02x", rows[row].reported);
    expect_frame_msg(hex, F5_FRAME_LEN, "level 0x%02x soc flag %u", rows[row].level, rows[row].soc_flag);
  }
  expect_no_frame();
}

ZTEST(events, test_battery_event_ignores_the_charging_byte_of_the_packet) {
  g1_state()->battery_left = 0x32;
  g1_ble()->charging = 1;

  report(0x0a, 1);

  expect_frame_padded("f5 0a 32", F5_FRAME_LEN);
}

ZTEST(events, test_case_charging_event_0x0e_carries_get_pt_level_flag) {
  get_pt_level_flag_fake.return_val = 1;
  report(0x0e, PKCS7_PACK_MORE);
  expect_frame_padded("f5 0e 01", F5_FRAME_LEN);

  get_pt_level_flag_fake.return_val = 0;
  report(0x0e, PKCS7_PACK_MORE);
  expect_frame_padded("f5 0e 00", F5_FRAME_LEN);
  zassert_equal(get_pt_level_flag_fake.call_count, 2);
}

ZTEST(events, test_case_battery_event_0x0f_carries_get_pt_level) {
  get_pt_level_fake.return_val = 0x28;
  report(0x0f, PKCS7_PACK_MORE);
  expect_frame_padded("f5 0f 28", F5_FRAME_LEN);

  get_pt_level_fake.return_val = 0x64;
  report(0x0f, PKCS7_PACK_MORE);
  expect_frame_padded("f5 0f 64", F5_FRAME_LEN);
}

ZTEST(events, test_brightness_event_0x12_carries_the_panel_brightness_level) {
  g1_state()->panel.panel_brightness_level = 6;
  report(0x12, PKCS7_PACK_MORE);
  expect_frame_padded("f5 12 06", F5_FRAME_LEN);

  g1_state()->panel.panel_brightness_level = 0x0c;
  report(0x12, PKCS7_PACK_MORE);
  expect_frame_padded("f5 12 0c", F5_FRAME_LEN);
}

/*
 * The master acks the last chunk of 0x0D / 0x0F / 0x4E only once the slave has it:
 * <cmd> C9 <seq> <kept count> <kept index>, seq from esb_down_id for 0x0D, else esb_up_id.
 * Each test reports the event and expects the ack as the only frame.
 */
#define DEFERRED_ACK_TEST(name, event, down_id, up_id, total, index, ack)                                              \
  ZTEST(events, test_deferred_ack_##name) {                                                                            \
    g1_state()->esb_down_id = (down_id);                                                                               \
    g1_state()->esb_up_id = (up_id);                                                                                   \
    deferred_text_ack_total = (total);                                                                                 \
    deferred_text_ack_idx = (index);                                                                                   \
    report((event), PKCS7_PACK_MORE);                                                                                  \
    expect_only_frame_padded((ack), F5_FRAME_LEN);                                                                     \
  }
DEFERRED_ACK_TEST(0xf0_is_0d_c9_seq_count_index_from_esb_down_id, 0xf0, 0x01, 0x77, 1, 0, "0d c9 01 01 00")
DEFERRED_ACK_TEST(0xf1_is_0f_c9_seq_count_index_from_esb_up_id, 0xf1, 0x77, 0x02, 1, 0, "0f c9 02 01 00")
DEFERRED_ACK_TEST(0xf2_is_4e_c9_seq_count_index_from_esb_up_id, 0xf2, 0x77, 0x2a, 3, 2, "4e c9 2a 03 02")

ZTEST(events, test_deferred_acks_clear_the_kept_count_and_index) {
  static const uint8_t ids[] = {0xf0, 0xf1, 0xf2};

  for (size_t index = 0; index < ARRAY_SIZE(ids); index++) {
    deferred_text_ack_total = 5;
    deferred_text_ack_idx = 4;
    report(ids[index], PKCS7_PACK_MORE);
    zassert_equal(deferred_text_ack_total, 0, "after 0x%02x: total %u", ids[index], deferred_text_ack_total);
    zassert_equal(deferred_text_ack_idx, 0, "after 0x%02x: idx %u", ids[index], deferred_text_ack_idx);
  }

  g1_frames_clear();
  g1_state()->esb_down_id = 9;
  report(0xf0, PKCS7_PACK_MORE);
  expect_frame_padded("0d c9 09 00 00", F5_FRAME_LEN);
}

ZTEST(events, test_f5_packet_from_the_work_thread_is_sent_through_the_dispatcher) {
  g1_state()->battery_left = 0x64;

  g1_send_hex("f5 06 cb");
  g1_send_hex("f5 0a 01");

  expect_frame_padded("f5 06", F5_FRAME_LEN);
  expect_only_frame_padded("f5 0a 64", F5_FRAME_LEN);
}

/* ---- 0x22 ---- */

static void dashboard(uint8_t mode, uint8_t pane) {
  g1_state()->dashboard_ts->disp_mode = mode;
  g1_state()->dashboard_ts->area = pane;
  dashboard_prv.pane_mode = pane;
}

static void messages(int count, int read) {
  memset(g_arrMsgContent, 0, sizeof(g_arrMsgContent));
  for (int slot = 0; slot < count; slot++) {
    strcpy(g_arrMsgContent[slot].app_identifier, "com.example.chat");
    g_arrMsgContent[slot].uid = 100 + slot;
    g_arrMsgContent[slot].read = slot < read;
  }
}

/* page = index + 1 on the notes, stock, news and calendar panes, else 0 */
struct page_row {
  uint8_t mode, pane;
  uint8_t notes, stocks, news, calendar; /* page indexes */
  const char *status;
};

/* sets up each row's dashboard and expects the status frame send_status() sends */
static void expect_page_rows(int (*send_status)(void), const struct page_row *rows, size_t count) {
  for (unsigned int row = 0; row < count; row++) {
    dashboard(rows[row].mode, rows[row].pane);
    pages(rows[row].notes, rows[row].stocks, rows[row].news, rows[row].calendar);
    zassert_equal(send_status(), 0, "row %u", row);
    expect_frame_msg(rows[row].status, -1, "row %u", row);
  }
}

ZTEST(events, test_head_up_status_is_22_0a_with_unread_low_power_mode_pane_and_page) {
  dashboard(MODE_DUAL, PANE_CALENDAR);
  messages(5, 0);

  zassert_equal(send_message_count_to_app(), 0);

  expect_only_frame("22 0a 00 00 01 05 00 01 03 01");
  zassert_equal(send_response_data_to_ble_fake.call_count, 1);
}

ZTEST(events, test_head_up_unread_count_leaves_out_read_messages) {
  dashboard(MODE_DUAL, PANE_MAP);
  messages(6, 2);

  zassert_equal(send_message_count_to_app(), 0);

  expect_frame("22 0a 00 00 01 04 00 01 04 00");
}

ZTEST(events, test_head_up_reports_low_power) {
  dashboard(MODE_DUAL, PANE_MAP);
  g1_state()->dashboard_ts->low_power = 1;

  zassert_equal(send_message_count_to_app(), 0);

  expect_frame("22 0a 00 00 01 00 01 01 04 00");
}

ZTEST(events, test_head_up_reports_the_mode_pane_and_page) {
  static const struct page_row rows[] = {
    {MODE_FULL, PANE_NOTES, 0, 0, 0, 0, "22 0a 00 00 01 00 00 00 00 01"},
    {MODE_FULL, PANE_NOTES, 2, 0, 0, 0, "22 0a 00 00 01 00 00 00 00 03"},
    {MODE_DUAL, PANE_NOTES, 0, 0, 0, 0, "22 0a 00 00 01 00 00 01 00 01"},
    {MODE_MINIMAL, PANE_NOTES, 0, 0, 0, 0, "22 0a 00 00 01 00 00 02 00 01"},
    {MODE_DUAL, PANE_STOCK, 0, 0, 0, 0, "22 0a 00 00 01 00 00 01 01 01"},
    {MODE_DUAL, PANE_STOCK, 0, 3, 0, 0, "22 0a 00 00 01 00 00 01 01 04"},
    {MODE_DUAL, PANE_NEWS, 0, 0, 0, 0, "22 0a 00 00 01 00 00 01 02 01"},
    {MODE_DUAL, PANE_NEWS, 0, 0, 1, 0, "22 0a 00 00 01 00 00 01 02 02"},
    {MODE_DUAL, PANE_CALENDAR, 0, 0, 0, 1, "22 0a 00 00 01 00 00 01 03 02"},
    {MODE_DUAL, PANE_MAP, 1, 1, 1, 1, "22 0a 00 00 01 00 00 01 04 00"},
  };

  expect_page_rows(send_message_count_to_app, rows, ARRAY_SIZE(rows));
}

ZTEST(events, test_head_up_empty_pane_in_minimal_mode_has_page_0) {
  dashboard(MODE_MINIMAL, PANE_EMPTY);
  pages(1, 1, 1, 1);
  messages(4, 0);

  zassert_equal(send_message_count_to_app(), 0);

  expect_frame("22 0a 00 00 01 04 00 02 05 00");
}

ZTEST(events, test_head_up_status_is_not_sent_by_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  dashboard(MODE_DUAL, PANE_CALENDAR);

  zassert_equal(send_message_count_to_app(), 0);

  expect_no_frame();
  zassert_equal(g1_esb_response_log.count, 0);
}

ZTEST(events, test_tap_status_is_22_08_with_mode_pane_and_page) {
  dashboard(MODE_DUAL, PANE_CALENDAR);
  messages(5, 0);

  zassert_equal(sync_dashboard_key_event_to_app(), 0);

  expect_only_frame("22 08 00 00 02 01 03 01");
  zassert_equal(send_response_data_to_ble_fake.call_count, 1);
}

ZTEST(events, test_tap_reports_the_mode_pane_and_page) {
  static const struct page_row rows[] = {
    {MODE_DUAL, PANE_CALENDAR, 0, 0, 0, 0, "22 08 00 00 02 01 03 01"},
    {MODE_DUAL, PANE_CALENDAR, 0, 0, 0, 1, "22 08 00 00 02 01 03 02"},
    {MODE_DUAL, PANE_CALENDAR, 0, 0, 0, 2, "22 08 00 00 02 01 03 03"},
    {MODE_FULL, PANE_NOTES, 0, 0, 0, 0, "22 08 00 00 02 00 00 01"},
    {MODE_FULL, PANE_NOTES, 3, 0, 0, 0, "22 08 00 00 02 00 00 04"},
    {MODE_FULL, PANE_STOCK, 0, 1, 0, 0, "22 08 00 00 02 00 01 02"},
    {MODE_DUAL, PANE_NEWS, 0, 0, 2, 0, "22 08 00 00 02 01 02 03"},
    {MODE_DUAL, PANE_MAP, 1, 1, 1, 1, "22 08 00 00 02 01 04 00"},
    {MODE_MINIMAL, PANE_EMPTY, 1, 1, 1, 1, "22 08 00 00 02 02 05 00"},
  };

  expect_page_rows(sync_dashboard_key_event_to_app, rows, ARRAY_SIZE(rows));
}

ZTEST(events, test_tap_status_is_not_sent_by_the_slave) {
  g1_unit_set_role(G1_ROLE_SLAVE);
  dashboard(MODE_DUAL, PANE_CALENDAR);

  zassert_equal(sync_dashboard_key_event_to_app(), 0);

  expect_no_frame();
  zassert_equal(g1_esb_response_log.count, 0);
}
