/*
 * The charging case talks to the lens through the tag's mailbox. nfc_case: production_test.c
 * answers the case's 0x13 query and pt_data.c retries the charger. nfc_events: threads.c reports
 * the case to the phone (F5 events) and nfc_related() serves the tag interrupt; check_work_mode()
 * is silent for the first 10 s after boot.
 * 0x13 request: the lens dispatches on byte 0 and reads byte 4 (case level | 80h charging) and
 * byte 5 (lid: 06 closed, 07 open, 08 closed with activity) when present; nothing else is checked.
 * The real case sends 13 00 ch 00 lvl lid without a checksum (REQ_*); the QUERY form
 * 13 01 02 <n> ... <sum> is the tree's older framing. The mailbox read hands the lens MB_LEN_Dyn,
 * the length minus one; process_pt_data() counts the last byte again unless it is the sum of the
 * bytes before it, so the lid byte reaches get_battery_info() in both forms.
 */
#include <zephyr/drivers/adc/adc_emul.h>

#include "drivers.h"
#include "fakes.h"
#include "harness.h"
#include "st25dv_emul.h"

#include "nfc/nfc_wlc.h"

/* pt_data.c includes sensors_audio.h first: dmic_stream_stop() returns int in this file */
#include "production_test/production_test.c"
#include "production_test/pt_data.c"
/* used before its definition; the host gcc rejects implicit declarations */
void init_key_event_sem(void);
#include "threads/threads.c"

/* nfc_wlc.c: the last field reading, mV x 10 */
extern uint32_t nfc_field_mv;

/* what pt_comm.c, production_test.c and threads.c use that the nfc app does not link */
DEFINE_FAKE_VALUE_FUNC(int, dmic_stream_stop);
DEFINE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
FAKE_VALUE_FUNC(int, lpuart_async_b, const struct device *, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, read_serial_data, const struct device *, uint8_t **, uint32_t *);
FAKE_VOID_FUNC(serial_rx_clear);
FAKE_VALUE_FUNC(int, get_work_mode);
FAKE_VALUE_FUNC(int, is_task_running, const struct GlassesState *);
FAKE_VALUE_FUNC(int, update_persist_task_status_to_idle, struct GlassesState *);
FAKE_VOID_FUNC(aw93203_init);
FAKE_VALUE_FUNC(int32_t, aw9320x_read_chipid);
FAKE_VALUE_FUNC(int, display_reflash_screen, enum screen_id);
FAKE_VOID_FUNC(free_demo_line_buf);
FAKE_VALUE_FUNC(int, global_system_resume, struct GlassesState *);
FAKE_VALUE_FUNC(int, global_system_suspend, struct GlassesState *);
FAKE_VALUE_FUNC(int, init_serial_comm);
FAKE_VALUE_FUNC(uint8_t, opt3007_get_brightness_level, struct opt3007_context *);
FAKE_VOID_FUNC(reset_dash_buf, int);
DEFINE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);
int esb_signal;
uint8_t task_status_lock;

FUNC_NORETURN void sys_reboot(int type) {
  ARG_UNUSED(type);
  ztest_test_fail();
  CODE_UNREACHABLE;
}

/* the F5 events threads.c sends to the phone */
static struct g1_pkt_log events_sent;

int enqueue_bt_data(const void *data, uint8_t len) {
  g1_pkt_log_add(&events_sent, data, len);
  return 0;
}

/*
 * before every nfc test: production_test.c and pt_data.c as at boot, the link on NFC;
 * get_work_mode() returns 0 again after nfc_events set it
 */
static void pt_reset(bool keep_media) {
  ARG_UNUSED(keep_media);
  G1_PT_STATICS_RESET();
  RESET_FAKE(get_work_mode);
}

G1_UNIT_RESET_HOOK(pt_reset);

/* the case's 0x13 query; case_send() appends the sum */
#define QUERY "13 01 02 00"

/* the replies the case reads */
static uint8_t reply[256];

/* the case puts hex and its sum into the mailbox */
static void case_send(const char *hex) {
  uint8_t msg[64];
  int len = g1_hex(hex, msg, sizeof(msg) - 1);

  msg[len] = g1_sum8(msg, len);
  zassert_ok(st25dv_emul_rf_put_msg(msg, len + 1));
}

/* the case sends hex, the lens answers; returns the length of the reply the case reads */
static int case_exchange(const char *hex) {
  case_send(hex);
  zassert_equal(init_nfc_comm(), 0);
  return st25dv_emul_rf_get_msg(reply, sizeof(reply));
}

/*
 * The real case (firmware 2.67, reference_material/logs/case/case_mailbox.hex) sends
 * 13 00 ch 00 lvl lid with no checksum: ch 01 = NFC0 = right lens, 00 = NFC1 = left lens
 * (the lens ignores it), lvl = 80h case charging | case %, lid 6 closed, 7 open, 8 closed with
 * activity. Captured with the lid open, USB in, case at 40 %; the case polls each lens every
 * 650 ms.
 */
#define REQ_NFC0         "13 00 01 00 a8 07" /* case_request_nfc0 */
#define REQ_NFC1         "13 00 00 00 a8 07" /* case_request_nfc1 */
#define REQ_NFC0_LID8    "13 00 01 00 a8 08" /* case_request_nfc0_lid8 */
#define REQ_NFC0_USB_OUT "13 00 01 00 28 07" /* 40 %, USB out */

/* the case puts hex into the mailbox as it is: no checksum */
static void case_put(const char *hex) {
  uint8_t msg[64];
  int len = g1_hex(hex, msg, sizeof(msg));

  zassert_ok(st25dv_emul_rf_put_msg(msg, len));
}

/* case_put() and the lens's answer; returns the length of the reply the case reads */
static int case_poll(const char *hex) {
  case_put(hex);
  zassert_equal(init_nfc_comm(), 0);
  return st25dv_emul_rf_get_msg(reply, sizeof(reply));
}

/* ---- the 0x13 query ---- */

static void case_before(void *fixture) {
  ARG_UNUSED(fixture);
  init_box_status();
  st25dv_emul_enable_mailbox();
  st25dv_emul_set_write_time_ms(1);

  g1_state()->battery_left = 50;
  g1_state()->battery_current_ma = 100;
  g1_state()->battery_temp_x100 = 2500;
  nfc_field_mv = 5500;
}

ZTEST_SUITE(nfc_case, NULL, NULL, case_before, NULL, NULL);

ZTEST(nfc_case, test_case_message_is_consumed_and_the_reply_waits_for_the_case) {
  uint8_t ctrl;

  case_send(QUERY);
  zassert_equal(init_nfc_comm(), 0);

  ctrl = st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_CTRL);
  zassert_equal(ctrl & ST25DV_EMUL_RF_PUT_MSG, 0, "case message consumed");
  zassert_equal(ctrl & ST25DV_EMUL_HOST_PUT_MSG, ST25DV_EMUL_HOST_PUT_MSG);
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_LEN), 10, "11-byte reply");
  zassert_equal(pt_comm_busy_set_fake.call_count, 1, "touch polling paused");
  zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

ZTEST(nfc_case, test_charging_flag_follows_the_reply_status) {
  static const struct {
    int16_t temp_x100;
    uint8_t battery_left;
    uint8_t status;
    uint8_t charging;
  } polls[] = {
    {2500, 50, 0x00, 1},  /* charging */
    {6000, 50, 0x02, 0},  /* too hot */
    {2500, 100, 0x01, 0}, /* full */
  };

  for (size_t poll = 0; poll < ARRAY_SIZE(polls); poll++) {
    g1_state()->battery_temp_x100 = polls[poll].temp_x100;
    g1_state()->battery_left = polls[poll].battery_left;
    zassert_equal(case_exchange(QUERY), 11, "status %u", polls[poll].status);
    zassert_equal(reply[4], polls[poll].status, "status %u", polls[poll].status);
    zassert_equal(is_box_charging(), polls[poll].charging, "status %u", polls[poll].status);
  }
}

ZTEST(nfc_case, test_case_level_and_lid_closed_are_taken_from_the_request) {
  case_send("13 01 02 02 a8 06"); /* level 40 | 80h charging, lid closed */
  zassert_equal(init_nfc_comm(), 0);

  zassert_equal(is_box_lid_closed(), 1);
  zassert_equal(can_enter_low_power, 1, "closed case: low power allowed");
  zassert_equal(get_pt_level(), 40, "F5 0F payload");
  zassert_equal(get_pt_level_flag(), 1, "F5 0E payload");
  zassert_equal(set_ncs_mem_pt_level_fake.arg0_val, 40);
}

ZTEST(nfc_case, test_case_lid_open_clears_the_lid_flag) {
  zassert_true(case_exchange("13 01 02 02 3c 06") > 0); /* level 60, lid closed */
  zassert_equal(is_box_lid_closed(), 1);

  case_send("13 01 02 02 3c 07");
  zassert_equal(init_nfc_comm(), 0);
  zassert_equal(is_box_lid_closed(), 0);
  zassert_equal(can_enter_low_power, 0);
  zassert_equal(get_pt_level_flag(), 0);
}

ZTEST(nfc_case, test_request_sum_is_not_read_as_a_lid_code) {
  zassert_true(case_exchange("13 01 02 02 3c 06") > 0);

  case_send("13 01 02 01 f0"); /* sum byte 07 = the lid-open code */
  zassert_equal(st25dv_emul_mailbox()[5], 0x07);
  zassert_equal(init_nfc_comm(), 0);
  zassert_equal(is_box_lid_closed(), 1);
}

/* 4th high-field reply: byte 9 = 80h, resent 1 s later */
static uint8_t timer_reply[256];
static int timer_reply_len;

static void case_reads(struct k_timer *timer) {
  ARG_UNUSED(timer);
  timer_reply_len = st25dv_emul_rf_get_msg(timer_reply, sizeof(timer_reply));
}

K_TIMER_DEFINE(case_timer, case_reads, NULL);

static void three_high_field_polls(void) {
  nfc_field_mv = 6000;
  g1_clock_advance_ms(10001);
  for (int poll = 1; poll <= 3; poll++) {
    zassert_equal(case_exchange(QUERY), 11);
    zassert_equal(reply[9], 0x64, "poll %d not flagged", poll);
  }
}

ZTEST(nfc_case, test_flagged_reply_is_sent_again_when_the_case_has_read_it) {
  three_high_field_polls();
  timer_reply_len = 0;
  case_send(QUERY);
  k_timer_start(&case_timer, K_MSEC(500), K_NO_WAIT);

  zassert_equal(init_nfc_comm(), 0);
  expect_bytes(timer_reply, timer_reply_len, "13 01 02 06 00 17 70 32 00 80 55");
  zassert_equal(st25dv_emul_rf_get_msg(reply, sizeof(reply)), 11, "second reply in the mailbox");
  expect_bytes(reply, 9, "13 01 02 06 00 17 70 32 00");
  zassert_equal(reply[9], 0x00);
}

ZTEST(nfc_case, test_flagged_reply_stays_when_the_case_has_not_read_it) {
  three_high_field_polls();

  zassert_equal(case_exchange(QUERY), 11);
  zassert_equal(reply[9], 0x80, "the resend found the mailbox busy");
}

ZTEST(nfc_case, test_link_without_a_tag_fails) {
  global_st25dv_context_ptr = NULL;

  zassert_equal(init_nfc_comm(), -1);
}

/* ---- the real case's requests and the captured replies ---- */

ZTEST(nfc_case, test_real_request_on_either_channel_gives_case_level_40_and_charging) {
  static const char *const requests[] = {REQ_NFC0, REQ_NFC1};

  for (size_t req = 0; req < ARRAY_SIZE(requests); req++) {
    RESET_FAKE(set_ncs_mem_pt_level);

    zassert_equal(case_poll(requests[req]), 11, "%s", requests[req]);
    expect_bytes_msg(reply, 4, "13 01 02 06", "%s", requests[req]);
    zassert_equal(get_pt_level(), 40, "%s", requests[req]);
    zassert_equal(get_pt_level_flag(), 1, "%s: case charging", requests[req]);
    zassert_equal(set_ncs_mem_pt_level_fake.arg0_val, 40);
  }
}

/* the 6-byte request ends in the lid byte, not in a sum */
ZTEST(nfc_case, test_real_request_lid_7_opens_the_lid) {
  set_box_status(BOX_LID_CLOSED, 1);
  can_enter_low_power = 1;

  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(is_box_lid_closed(), 0, "lid open");
  zassert_equal(can_enter_low_power, 0);
}

ZTEST(nfc_case, test_lid_8_closes_the_lid_and_refreshes_activity_but_not_low_power) {
  int64_t before;

  nfc_field_mv = 4900;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(is_box_lid_closed(), 0);
  last_activity_ms = 0;

  g1_clock_advance_ms(650);
  before = g1_clock_uptime_ms();
  zassert_equal(case_poll(REQ_NFC0_LID8), 11);
  zassert_equal(is_box_lid_closed(), 1);
  zassert_equal(can_enter_low_power, 0, "lid 8: low power not allowed");
  zassert_true((int64_t)last_activity_ms >= before && (int64_t)last_activity_ms <= g1_clock_uptime_ms(),
               "activity time %llu not refreshed at %lld", (unsigned long long)last_activity_ms, (long long)before);

  /* lid 6 is the code that allows low power; a lid 8 after it leaves that alone */
  zassert_equal(case_poll("13 00 01 00 a8 06"), 11);
  zassert_equal(can_enter_low_power, 1);
  zassert_equal(case_poll(REQ_NFC0_LID8), 11);
  zassert_equal(is_box_lid_closed(), 1);
  zassert_equal(can_enter_low_power, 1);
}

ZTEST(nfc_case, test_short_requests_leave_the_lid_and_then_the_level_alone) {
  /* lid closed, 40 % charging: the QUERY form gets the lid byte through */
  zassert_equal(case_exchange("13 01 02 02 a8 06"), 11);
  zassert_equal(is_box_lid_closed(), 1);
  zassert_equal(can_enter_low_power, 1);
  zassert_equal(get_pt_level(), 40);

  RESET_FAKE(set_ncs_mem_pt_level);
  zassert_equal(case_poll("13 00 01 00"), 11); /* no level byte */
  zassert_equal(get_pt_level(), 40, "level left alone");
  zassert_equal(set_ncs_mem_pt_level_fake.call_count, 0);
  zassert_equal(is_box_lid_closed(), 1, "lid left alone");

  zassert_equal(case_poll("13 00 01 00 3c"), 11); /* 60 %, USB out, no lid byte */
  zassert_equal(get_pt_level(), 60);
  zassert_equal(get_pt_level_flag(), 0);
  zassert_equal(is_box_lid_closed(), 1, "lid left alone");
  zassert_equal(can_enter_low_power, 1);
}

/* the lens state behind a captured reply */
struct lens_reply {
  uint32_t field_mv;
  uint8_t battery;
  int8_t current_ma;
  const char *reply; /* tag in case_mailbox.hex */
};

static void expect_lens_replies(const struct lens_reply *rows, size_t count) {
  for (size_t row = 0; row < count; row++) {
    nfc_field_mv = rows[row].field_mv;
    g1_state()->battery_left = rows[row].battery;
    g1_state()->battery_current_ma = rows[row].current_ma;
    zassert_equal(case_poll(REQ_NFC0), 11, "%s", rows[row].reply);
    expect_bytes_msg(reply, 11, rows[row].reply, "%u mV, %u %%, %d mA", rows[row].field_mv, rows[row].battery,
                     rows[row].current_ma);
  }
}

ZTEST(nfc_case, test_replies_are_bit_exact_against_the_captured_lens_replies) {
  static const struct lens_reply rows[] = {
    {4910, 69, -35, "13 01 02 06 00 13 2e 45 01 23 c6"}, /* lens_reply_nfc0_69pct_4910mv */
    {4930, 70, -35, "13 01 02 06 00 13 42 46 01 23 db"}, /* lens_reply_nfc0_70pct_4930mv */
    {4740, 50, -34, "13 01 02 06 00 12 84 32 01 22 07"}, /* lens_reply_nfc1_50pct_4740mv */
    {4660, 51, -35, "13 01 02 06 00 12 34 33 01 23 b9"}, /* lens_reply_nfc1_51pct_4660mv */
    {4790, 14, -34, "13 01 02 06 00 12 b6 0e 01 22 15"}, /* lens_reply_14pct_4790mv */
    {4390, 53, -37, "13 01 02 06 00 11 26 35 01 25 ae"}, /* lens_reply_left_53pct_4390mv */
  };

  expect_lens_replies(rows, ARRAY_SIZE(rows));
}

/* re-dock: field not yet sampled, then full field with the charger off, then charging */
ZTEST(nfc_case, test_redock_replies_are_bit_exact_through_the_field_and_charger_ramp) {
  static const struct lens_reply rows[] = {
    {0, 74, 3, "13 01 02 06 00 00 00 4a 00 03 69"},      /* lens_reply_just_docked_vrect0 */
    {5800, 74, 15, "13 01 02 06 00 16 a8 4a 00 0f 33"},  /* lens_reply_full_field_5800mv */
    {5760, 74, -37, "13 01 02 06 00 16 80 4a 01 25 22"}, /* lens_reply_charging_5760mv */
  };

  expect_lens_replies(rows, ARRAY_SIZE(rows));
  zassert_equal(battery_high_v_count, 0, "cleared by the charge current");
}

/* the first reply after a case reboot: field 10 mV, charger off, 0 mA */
ZTEST(nfc_case, test_reply_after_a_case_boot_is_bit_exact) {
  static const struct lens_reply rows[] = {
    {10, 79, 0, "13 01 02 06 00 00 0a 4f 00 00 75"}, /* lens_reply_after_case_boot */
  };

  expect_lens_replies(rows, ARRAY_SIZE(rows));
}

/* byte 9 is this poll's current at every field, not what the reply before left there */
ZTEST(nfc_case, test_reply_at_low_field_without_charge_current_reports_0_ma) {
  static const struct lens_reply charging[] = {
    {5760, 74, -37, "13 01 02 06 00 16 80 4a 01 25 22"}, /* lens_reply_charging_5760mv */
  };

  expect_lens_replies(charging, ARRAY_SIZE(charging));

  nfc_field_mv = 10;
  g1_state()->battery_current_ma = 0;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[8], 0);
  zassert_equal(reply[9], 0, "0 mA, not the 37 mA of the reply before");
}

/* ---- the reset request: byte 9 = 80h ---- */

ZTEST(nfc_case, test_four_high_field_polls_without_charge_current_request_a_case_reset_once_per_10s) {
  int64_t flagged;

  g1_state()->battery_current_ma = 15;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001); /* the 10 s rate limit counts from boot */

  for (int poll = 1; poll <= 3; poll++) {
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[9], 15, "poll %d: the current", poll);
    zassert_equal(battery_high_v_count, poll);
  }
  flagged = g1_clock_uptime_ms();
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[8], 0);
  zassert_equal(reply[9], 0x80, "4th poll");
  expect_elapsed_ms(flagged, 1000, 1010); /* the lens waited 1 s for the resend */

  for (int poll = 5; poll <= 9; poll++) {
    g1_clock_advance_ms(650);
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[9], 15, "poll %d: inside 10 s", poll);
  }
  zassert_equal(battery_high_v_count, 9, "the counter runs on, to 40");

  g1_clock_advance_ms(10001);
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[9], 0x80, "again after 10 s");
}

ZTEST(nfc_case, test_case_reset_is_requested_between_3_and_99_percent_only) {
  static const struct {
    uint8_t battery;
    uint8_t byte9;
  } rows[] = {
    {2, 15},
    {3, 0x80},
    {99, 0x80},
    {100, 15}, /* full: status 1, the counters stay 0 */
  };

  g1_state()->battery_current_ma = 15;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001);

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    battery_high_v_count = 0;
    battery_full_count = 0;
    battery_full_flag = false;
    battery_info_last_ms = 0;
    g1_state()->battery_left = rows[row].battery;

    for (int poll = 1; poll <= 4; poll++) {
      zassert_equal(case_poll(REQ_NFC0), 11, "%u %%", rows[row].battery);
    }
    zassert_equal(reply[9], rows[row].byte9, "%u %%: byte 9 %02x", rows[row].battery, reply[9]);
  }
}

ZTEST(nfc_case, test_charger_refusing_to_enable_requests_a_case_reset_at_the_fourth_high_field_poll) {
  charger_enable_ok = 0;
  g1_state()->battery_current_ma = 0;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001);

  for (int poll = 1; poll <= 3; poll++) {
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[9], 0, "poll %d", poll);
    zassert_equal(battery_full_count, poll);
  }
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[9], 0x80, "4th poll");
  zassert_equal(reply[4], 0, "status 0 until the 40th poll");

  /* below 5.5 V the refusal count starts over at every poll: never a reset request */
  nfc_field_mv = 5000;
  for (int poll = 1; poll <= 6; poll++) {
    g1_clock_advance_ms(10001);
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[9], 0, "poll %d at 5.0 V", poll);
    zassert_equal(battery_full_count, 1, "poll %d at 5.0 V", poll);
    zassert_equal(battery_high_v_count, 0, "poll %d at 5.0 V", poll);
  }
}

/* |current| = 128 mA is 80h too; the case reboots on it (case_nfc_protocol.md section 6) */
ZTEST(nfc_case, test_128_ma_charge_current_is_reported_as_the_case_reset_request) {
  int64_t start = g1_clock_uptime_ms();

  nfc_field_mv = 4910;
  g1_state()->battery_left = 69;
  g1_state()->battery_current_ma = -128;

  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[8], 1, "charging");
  zassert_equal(reply[9], 0x80);
  zassert_equal(battery_high_v_count, 0, "no reset condition");
  zassert_equal(battery_full_count, 0);
  expect_elapsed_ms(start, 1000, 1010); /* and the lens resends it as a reset request would */
}

/* 4th high-field reply: byte 9 = 80h, resent 1 s later */
static void field_drops(struct k_timer *timer) {
  ARG_UNUSED(timer);
  st25dv_emul_rf_field(false);
}

static void field_returns(struct k_timer *timer) {
  ARG_UNUSED(timer);
  st25dv_emul_rf_field(true);
}

K_TIMER_DEFINE(field_drop_timer, field_drops, NULL);
K_TIMER_DEFINE(field_return_timer, field_returns, NULL);

/*
 * The case reboots on the 80h reply without reading it: its field drops ~300 ms after the reply
 * and is back ~300 ms later. The 1 s resend finds HOST_PUT_MSG set and the mailbox keeps the 80h
 * reply until the rebooted case reads it.
 */
ZTEST(nfc_case, test_reset_request_stays_in_the_mailbox_through_the_case_reboot_until_it_is_read) {
  uint8_t msg[8];
  int len = g1_hex(REQ_NFC0, msg, sizeof(msg));
  int64_t flagged;

  three_high_field_polls();
  st25dv_emul_rf_field(true);
  k_timer_start(&field_drop_timer, K_MSEC(300), K_NO_WAIT);
  k_timer_start(&field_return_timer, K_MSEC(600), K_NO_WAIT);

  case_put(REQ_NFC0);
  flagged = g1_clock_uptime_ms();
  zassert_equal(init_nfc_comm(), 0);
  expect_elapsed_ms(flagged, 1000, 1010);

  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_MB_CTRL), 0x43,
                "MB_EN | HOST_PUT_MSG | HOST_CURRENT_MSG, as the case reads it");
  zassert_equal(st25dv_emul_mailbox()[9], 0x80, "the resend was refused");

  /* the rebooted case polls before reading: refused; then it reads the pending reply */
  zassert_equal(st25dv_emul_rf_put_msg(msg, len), -EBUSY);
  zassert_equal(st25dv_emul_rf_get_msg(reply, sizeof(reply)), 11);
  expect_bytes(reply, 11, "13 01 02 06 00 17 70 32 00 80 55");

  /* its next poll gets a normal reply: no second reset request inside 10 s */
  g1_clock_advance_ms(650);
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[9], 0x64);
}

/* ---- status codes ---- */

ZTEST(nfc_case, test_status_1_is_reported_at_the_aging_charge_limit) {
  aging_mode = 1;
  aging_phase = 2; /* limit 70 % */
  nfc_field_mv = 4900;

  g1_state()->battery_left = 69;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 0);
  zassert_equal(is_box_charging(), 1);

  g1_state()->battery_left = 70;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 1, "full at the aging limit");
  zassert_equal(is_box_charging(), 0);

  aging_mode = 0; /* limit 100 % again */
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 0);
  g1_state()->battery_left = 100;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 1);
}

ZTEST(nfc_case, test_status_2_is_reported_above_60c_or_above_50c_with_a_high_battery_voltage) {
  static const struct {
    int16_t temp_x100;
    uint8_t voltage;
    uint8_t status;
  } rows[] = {
    {5999, 0xd1, 0}, {6000, 0x00, 2}, {5000, 0xd1, 0}, {5000, 0xd2, 2}, {4999, 0xff, 0},
  };

  nfc_field_mv = 4900;
  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    g1_state()->battery_temp_x100 = rows[row].temp_x100;
    g1_state()->battery_voltage = rows[row].voltage;
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[4], rows[row].status, "%d.%02d C, voltage %02x: status %u", rows[row].temp_x100 / 100,
                  rows[row].temp_x100 % 100, rows[row].voltage, reply[4]);
  }
}

ZTEST(nfc_case, test_status_4_after_40_high_field_polls_with_the_charger_refusing) {
  charger_enable_ok = 0;
  g1_state()->battery_current_ma = 0;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001);

  for (int poll = 1; poll <= 39; poll++) {
    zassert_equal(case_poll(REQ_NFC0), 11);
    zassert_equal(reply[4], 0, "poll %d", poll);
    zassert_equal(is_box_charging(), 1, "poll %d", poll);
  }
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 4, "40th poll");
  zassert_equal(reply[9], 0, "the current, the reset request was 10 s ago");
  zassert_equal(is_box_charging(), 0);
}

/*
 * 40 high-field polls without charge current: status 4 below 7.8 V. It holds while the case keeps
 * a field up, with byte 9 = 100 below 5 V.
 */
ZTEST(nfc_case, test_status_4_after_40_high_field_polls_without_charge_current) {
  g1_state()->battery_current_ma = 15;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001);

  for (int poll = 1; poll <= 40; poll++) {
    zassert_equal(case_poll(REQ_NFC0), 11);
  }
  zassert_equal(reply[4], 4, "40th poll at 5.6 V");

  nfc_field_mv = 4900;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 4);
  zassert_equal(reply[9], 100, "the no-charge marker");
}

/* too hot outranks no-charge: the case still gets status 2 and the current, at any field */
ZTEST(nfc_case, test_status_2_is_kept_over_the_no_charge_status) {
  g1_state()->battery_current_ma = 15;
  nfc_field_mv = 5600;
  g1_clock_advance_ms(10001);

  for (int poll = 1; poll <= 40; poll++) {
    zassert_equal(case_poll(REQ_NFC0), 11);
  }
  zassert_equal(reply[4], 4, "40th poll at 5.6 V");

  g1_state()->battery_temp_x100 = 6000;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 2, "too hot at 5.6 V");

  nfc_field_mv = 4900;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[4], 2, "too hot at 4.9 V");
  zassert_equal(reply[9], 15, "the current, not the no-charge marker");
  zassert_equal(is_box_charging(), 0);
}

/* ---- the field as the lens samples it ---- */

/* the field pin (adc0 channel 3), sampled as nfc_wlc_process() does before every pass */
static void field_pin_mv(uint32_t mv) {
  zassert_ok(adc_emul_const_value_set(DEVICE_DT_GET(DT_NODELABEL(adc0)), 3, mv));
  nfc_field_voltage_update();
}

/* re-dock ramp 0 -> 5800 -> 4400 mV: the high-field counter runs only without charge current */
ZTEST(nfc_case, test_redock_field_ramp_counts_high_field_polls_only_without_charge_current) {
  field_pin_mv(0);
  g1_state()->battery_current_ma = 3;
  zassert_equal(case_poll(REQ_NFC0), 11);
  expect_bytes(&reply[5], 2, "00 00");
  zassert_equal(reply[8], 0);
  zassert_equal(battery_high_v_count, 0);

  field_pin_mv(580); /* full field, the charger still off */
  g1_state()->battery_current_ma = 15;
  zassert_equal(case_poll(REQ_NFC0), 11);
  expect_within(sys_get_be16(&reply[5]), 5800, 10);
  zassert_equal(reply[8], 0, "not charging");
  zassert_equal(battery_high_v_count, 1);

  field_pin_mv(440); /* the case stepped its power down, the charger is on */
  g1_state()->battery_current_ma = -37;
  zassert_equal(case_poll(REQ_NFC0), 11);
  expect_within(sys_get_be16(&reply[5]), 4400, 10);
  zassert_equal(reply[8], 1, "charging");
  zassert_equal(reply[9], 37);
  zassert_equal(battery_high_v_count, 0, "cleared by the charge current");

  field_pin_mv(580);
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[8], 1);
  zassert_equal(battery_high_v_count, 0, "not counted while charging");

  g1_state()->battery_current_ma = 0;
  zassert_equal(case_poll(REQ_NFC0), 11);
  zassert_equal(reply[8], 0);
  zassert_equal(battery_high_v_count, 1);
}

/* ---- charger retry ---- */

/* BCHGCHARGESTATUS: b0 battery detected, b3 constant current */
#define STAT_IDLE 0x01
#define STAT_CC   0x09

static const uint8_t *stat_seq;
static int stat_count;
static int stat_reads;

/* the status reads go through stat_seq, then repeat its last value */
static int read_stat(const struct device *dev, uint8_t base, uint8_t offset, uint8_t *value) {
  ARG_UNUSED(dev);
  zassert_equal(base, 0x03);
  zassert_equal(offset, 0x34);
  *value = stat_seq[MIN(stat_reads, stat_count - 1)];
  stat_reads++;
  return 0;
}

static void charger_status(const uint8_t *seq, int count) {
  stat_seq = seq;
  stat_count = count;
  stat_reads = 0;
  mfd_npm1300_reg_read_fake.custom_fake = read_stat;
  RESET_FAKE(mfd_npm1300_reg_write);
}

ZTEST(nfc_case, test_charger_retry_leaves_a_charging_charger_alone) {
  static const uint8_t seq[] = {STAT_CC};

  charger_status(seq, ARRAY_SIZE(seq));
  enable_charger_retry();
  zassert_equal(stat_reads, 1);
  zassert_equal(mfd_npm1300_reg_write_fake.call_count, 0);
}

ZTEST(nfc_case, test_charger_retry_enables_the_charger_until_it_charges) {
  static const uint8_t seq[] = {STAT_IDLE, STAT_IDLE, STAT_CC};
  int64_t start = g1_clock_uptime_ms();

  charger_status(seq, ARRAY_SIZE(seq));
  enable_charger_retry();

  zassert_equal(stat_reads, 3);
  zassert_equal(mfd_npm1300_reg_write_fake.call_count, 2);
  zassert_equal(mfd_npm1300_reg_write_fake.arg2_history[0], 0x04, "BCHGENABLESET");
  zassert_equal(mfd_npm1300_reg_write_fake.arg3_history[0], 0x01);
  expect_elapsed_ms(start, 40, 46); /* 20 ms after each enable */
  zassert_true(charger_enable_ok);
}

ZTEST(nfc_case, test_charger_retry_gives_up_after_10_attempts) {
  static const uint8_t seq[] = {STAT_IDLE};
  int64_t start = g1_clock_uptime_ms();

  charger_status(seq, ARRAY_SIZE(seq));
  enable_charger_retry();

  zassert_equal(stat_reads, 10);
  zassert_equal(mfd_npm1300_reg_write_fake.call_count, 10);
  expect_elapsed_ms(start, 200, 215);
}

ZTEST(nfc_case, test_charger_failing_four_rounds_a_second_apart_is_marked_failed) {
  static const uint8_t idle[] = {STAT_IDLE};
  static const uint8_t charging[] = {STAT_CC};

  charger_status(idle, ARRAY_SIZE(idle));
  g1_clock_advance_ms(1000);
  for (int round = 1; round <= 3; round++) {
    enable_charger_retry();
    zassert_true(charger_enable_ok, "round %d", round);
    enable_charger_retry();
    g1_clock_advance_ms(1000);
  }
  enable_charger_retry();
  zassert_false(charger_enable_ok, "fourth round");

  g1_clock_advance_ms(1000);
  charger_status(charging, ARRAY_SIZE(charging));
  enable_charger_retry();
  zassert_true(charger_enable_ok);
}

/* ---- threads.c: F5 events and nfc_related() ---- */

#define EVT_BOUND       0x11
#define EVT_OUT_UNWORN  0x07
#define EVT_CASE_CLOSED 0x0b
#define EVT_BATTERY     0x0a
#define EVT_CHARGING    0x09
#define EVT_CASE_OPEN   0x08
#define EVT_BOX_STATUS  0x0e
#define EVT_BOX_LEVEL   0x0f

static void events_before(void *fixture) {
  ARG_UNUSED(fixture);
  init_box_status();
  st25dv_emul_set_write_time_ms(1);
  g1_pkt_log_clear(&events_sent);

  wear_state_event = BLE_EVT_NONE;
  last_bind_state = 0xff;
  last_box_soc = 0xff;
  last_box_status = 0xff;
  last_battery = 0xff;
  last_charging = 0xff;
  last_brightness = 0xff;
  charging_cnt = 0;
  box_status_cnt = 0;
  unwear_cnt = 0;
  wear_cnt = 0;
  chipid_check_cnt = 0;
  is_wearing = 0;
  nfc_deadline = 0;
  nfc_sem_ready = 0;

  get_work_mode_fake.return_val = 5; /* running, not boot / DFU */
  g1_ble()->receiver_thd_is_running = 1;
  g1_state()->battery_left = 50;
  if (g1_clock_uptime_ms() < 10000) {
    g1_clock_advance_ms(10000);
  }
}

ZTEST_SUITE(nfc_events, NULL, NULL, events_before, NULL, NULL);

/* one check_work_mode(); returns the id of the event it sent, -1 for none */
static int poll_event(int in_box, int lid_closed) {
  int sent = events_sent.count;

  check_work_mode(in_box, lid_closed, 1);
  if (events_sent.count == sent) {
    return -1;
  }
  zassert_equal(events_sent.count, sent + 1, "one event per poll");
  zassert_equal(events_sent.len[sent], 3);
  zassert_equal(events_sent.data[sent][0], 0xf5);
  return events_sent.data[sent][1];
}

static void first_polls(int in_box, int lid_closed, int where) {
  zassert_equal(poll_event(in_box, lid_closed), EVT_BOUND);
  zassert_equal(poll_event(in_box, lid_closed), where);
  zassert_equal(poll_event(in_box, lid_closed), EVT_BATTERY);
}

ZTEST(nfc_events, test_charging_in_the_case_is_reported_as_f5_09_with_1) {
  set_box_status(2, 1);

  first_polls(1, 1, EVT_CASE_CLOSED);
  zassert_equal(poll_event(1, 1), EVT_CHARGING);
  zassert_equal(g1_ble()->charging, 1, "F5 09 payload");
  zassert_equal(poll_event(1, 1), -1, "reported once");
}

ZTEST(nfc_events, test_end_of_charging_in_the_case_is_reported_after_5_polls) {
  set_box_status(2, 1);
  first_polls(1, 1, EVT_CASE_CLOSED);
  zassert_equal(poll_event(1, 1), EVT_CHARGING);

  set_box_status(2, 0);
  for (int poll = 1; poll <= 4; poll++) {
    zassert_equal(poll_event(1, 1), -1, "poll %d", poll);
  }
  zassert_equal(poll_event(1, 1), EVT_CHARGING);
  zassert_equal(g1_ble()->charging, 0);
}

ZTEST(nfc_events, test_taking_the_lens_out_reports_not_charging_at_once) {
  set_box_status(2, 1);
  first_polls(1, 1, EVT_CASE_CLOSED);
  zassert_equal(poll_event(1, 1), EVT_CHARGING);

  zassert_equal(poll_event(0, 0), EVT_OUT_UNWORN);
  zassert_equal(poll_event(0, 0), EVT_CHARGING);
  zassert_equal(g1_ble()->charging, 0);
}

ZTEST(nfc_events, test_battery_level_is_reported_as_f5_0a_when_it_changes) {
  first_polls(0, 0, EVT_OUT_UNWORN);
  zassert_equal(poll_event(0, 0), EVT_CHARGING, "not charging out of the case");
  zassert_equal(poll_event(0, 0), -1);

  g1_state()->battery_left = 51;
  zassert_equal(poll_event(0, 0), EVT_BATTERY);
  zassert_equal(poll_event(0, 0), -1, "same level: nothing");
  g1_state()->battery_left = 49;
  zassert_equal(poll_event(0, 0), EVT_BATTERY);
}

static void nfc_thread_start(void) {
  g1_thread_start(nfc_related, NULL, K_PRIO_PREEMPT(5));
  k_sleep(K_MSEC(1));
  zassert_equal(nfc_sem_ready, 1);
}

/* the case puts its 0x13 query into the mailbox and the tag interrupts the lens */
static void case_talks(void) {
  st25dv_emul_set_sys(ST25DV_EMUL_SYS_GPO1, 0xff);
  st25dv_emul_enable_mailbox();
  case_send(QUERY);
  trigger_nfc_event();
}

ZTEST(nfc_events, test_message_from_the_case_puts_the_lens_in_the_case) {
  nfc_thread_start();
  zassert_equal(is_in_box(), 0);

  case_talks();
  k_sleep(K_MSEC(200));

  zassert_equal(is_in_box(), 1, "isInbox");
  zassert_equal(st25dv_emul_rf_get_msg(reply, sizeof(reply)), 11, "answered");
  zassert_equal(reply[0], 0x13);
}

ZTEST(nfc_events, test_case_silent_for_3s_takes_the_lens_out_of_the_case) {
  nfc_thread_start();
  case_talks();
  k_sleep(K_MSEC(200));
  zassert_equal(st25dv_emul_rf_get_msg(reply, sizeof(reply)), 11);
  set_box_status(BOX_LID_CLOSED, 1);

  g1_clock_advance_ms(2000);
  zassert_equal(is_in_box(), 1, "2.2 s of silence");
  g1_clock_advance_ms(4500);
  zassert_equal(is_in_box(), 0, "6.7 s of silence");
  zassert_equal(is_box_lid_closed(), 0);
}

ZTEST(nfc_events, test_interrupt_without_a_case_message_does_not_put_the_lens_in_the_case) {
  nfc_thread_start();
  st25dv_emul_set_sys(ST25DV_EMUL_SYS_GPO1, 0xff);
  st25dv_emul_rf_field(true);
  trigger_nfc_event();
  k_sleep(K_MSEC(200));

  zassert_equal(is_in_box(), 0);
}

/* ---- the real case cadence: a 0x13 every 650 ms per lens ---- */

/* the case detects the tag: GPO interrupts on, mailbox on */
static void case_docks(void) {
  st25dv_emul_set_sys(ST25DV_EMUL_SYS_GPO1, 0xff);
  st25dv_emul_enable_mailbox();
}

/* one real poll through the tag interrupt; the case reads the reply 650 ms later */
static int case_poll_650(const char *hex) {
  uint8_t msg[8];
  int len = g1_hex(hex, msg, sizeof(msg));

  zassert_ok(st25dv_emul_rf_put_msg(msg, len));
  trigger_nfc_event();
  k_sleep(K_MSEC(650));
  return st25dv_emul_rf_get_msg(reply, sizeof(reply));
}

/* case_poll_650() then one check_work_mode() with the lens's own box flags */
static int case_poll_event(const char *hex) {
  zassert_equal(case_poll_650(hex), 11);
  return poll_event(is_in_box(), is_box_lid_closed());
}

/*
 * The lens reports the end of charging after 5 polls of check_work_mode(); the device runs it
 * every 1 s from low_speed_peripheral_dispatch_thread(), here one per case poll: 3.25 s.
 */
ZTEST(nfc_events, test_real_cadence_reports_end_of_charging_after_5_polls_and_leaves_after_3s) {
  int64_t stopped;

  nfc_thread_start();
  case_docks();
  zassert_equal(case_poll_650(REQ_NFC0), 11);
  zassert_equal(is_in_box(), 1);
  zassert_equal(is_box_charging(), 1);
  first_polls(1, 0, EVT_CASE_OPEN);
  zassert_equal(poll_event(1, 0), EVT_CHARGING);
  zassert_equal(g1_ble()->charging, 1);

  /* the lens gets too hot: the next reply has status 2 and the charging flag drops */
  g1_state()->battery_temp_x100 = 6000;
  stopped = g1_clock_uptime_ms();
  zassert_equal(case_poll_650(REQ_NFC0), 11);
  zassert_equal(reply[4], 2);
  zassert_equal(is_box_charging(), 0);
  for (int poll = 1; poll <= 4; poll++) {
    zassert_equal(poll_event(1, 0), -1, "poll %d", poll);
    zassert_equal(case_poll_650(REQ_NFC0), 11);
  }
  zassert_equal(poll_event(1, 0), EVT_CHARGING);
  zassert_equal(g1_ble()->charging, 0);
  expect_elapsed_ms(stopped, 3250, 3260);

  /* one missed poll (a 1.3 s gap) keeps the lens in the case */
  k_sleep(K_MSEC(650));
  zassert_equal(is_in_box(), 1, "1.3 s after the last poll");
  zassert_equal(case_poll_650(REQ_NFC0), 11);
  zassert_equal(is_in_box(), 1);

  /* 3 s of silence takes it out */
  k_sleep(K_MSEC(2000));
  zassert_equal(is_in_box(), 1, "2.65 s after the last poll");
  k_sleep(K_MSEC(1000));
  zassert_equal(is_in_box(), 0, "3.65 s after the last poll");
  zassert_equal(poll_event(0, 0), EVT_OUT_UNWORN);
}

/* lid closed with the lens in: three polls with lid 8, then lid 7 when it opens */
ZTEST(nfc_events, test_lid_8_x3_then_7_reports_closed_then_open_without_low_power) {
  nfc_thread_start();
  case_docks();
  zassert_equal(case_poll_650(REQ_NFC0), 11);
  first_polls(1, 0, EVT_CASE_OPEN);
  zassert_equal(poll_event(1, 0), EVT_CHARGING);

  for (int poll = 1; poll <= 3; poll++) {
    int evt = case_poll_event(REQ_NFC0_LID8);

    zassert_equal(is_box_lid_closed(), 1, "poll %d", poll);
    zassert_equal(can_enter_low_power, 0, "poll %d: lid 8 does not arm low power", poll);
    zassert_equal(evt, poll == 1 ? EVT_CASE_CLOSED : -1, "poll %d", poll);
  }

  zassert_equal(case_poll_event(REQ_NFC0), EVT_CASE_OPEN);
  zassert_equal(is_box_lid_closed(), 0);
  zassert_equal(can_enter_low_power, 0);
}

ZTEST(nfc_events, test_case_charging_bit_and_level_are_reported_as_f5_0e_and_f5_0f) {
  nfc_thread_start();
  case_docks();
  zassert_equal(case_poll_650(REQ_NFC0), 11);
  first_polls(1, 0, EVT_CASE_OPEN);
  zassert_equal(poll_event(1, 0), EVT_CHARGING);

  /* a8: USB in, 40 %; the case status needs 5 polls, the level follows at once */
  for (int poll = 1; poll <= 4; poll++) {
    zassert_equal(case_poll_event(REQ_NFC0), -1, "poll %d", poll);
  }
  zassert_equal(case_poll_event(REQ_NFC0), EVT_BOX_STATUS);
  zassert_equal(get_pt_level_flag(), 1, "F5 0E payload: case charging");
  zassert_equal(case_poll_event(REQ_NFC0), EVT_BOX_LEVEL);
  zassert_equal(get_pt_level(), 40, "F5 0F payload");
  zassert_equal(case_poll_event(REQ_NFC0), -1);

  /* 28: USB out, still 40 % */
  for (int poll = 1; poll <= 4; poll++) {
    zassert_equal(case_poll_event(REQ_NFC0_USB_OUT), -1, "poll %d", poll);
  }
  zassert_equal(case_poll_event(REQ_NFC0_USB_OUT), EVT_BOX_STATUS);
  zassert_equal(get_pt_level_flag(), 0, "F5 0E payload: case not charging");
  zassert_equal(case_poll_event(REQ_NFC0_USB_OUT), -1, "same level: no F5 0F");
}
