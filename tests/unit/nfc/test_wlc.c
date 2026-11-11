/*
 * nfc/nfc_wlc.c through its API: field voltage, charge state, the WLC listener, the WLC message on
 * the tag, nfc_init(), nfc_wlc_process() and the GPO line. test_ndef.c includes nfc_wlc.c; before
 * every test it restores its statics and puts a constant 0 mV on the field pin.
 * Charge state: 1 charging, 2 no field, 3 updating, 4 full.
 */
#include <zephyr/drivers/adc/adc_emul.h>

#include "harness.h"
#include "drivers.h"
#include "st25dv_emul.h"

#include "nfc/nfc_wlc.h"
#include "nfc/st25dv.h"
#include "sensors_audio/sensors_audio.h"

/* the field pin: adc0 channel 3 */
#define ADC0 DEVICE_DT_GET(DT_NODELABEL(adc0))

#define GPO_PIN 10

/* nfc_wlc.c: the tag is an ST25DV of the expected type */
extern uint8_t nfc_tag_type_ok;

static void set_pin_mv(uint32_t mv)
{
  zassert_ok(adc_emul_const_value_set(ADC0, 3, mv));
}

/* every nPM1300 read: BCHGCHARGESTATUS constant current, the charger is already charging */
static int charger_charging(const struct device *dev, uint8_t base, uint8_t offset,
          uint8_t *value)
{
  ARG_UNUSED(dev);
  ARG_UNUSED(base);
  ARG_UNUSED(offset);
  *value = 0x08;
  return 0;
}

static void wlc_before(void *fixture)
{
  ARG_UNUSED(fixture);
  mfd_npm1300_reg_read_fake.custom_fake = charger_charging;
  st25dv_emul_set_write_time_ms(1);
}

static struct gpio_callback gpo_cb;
static bool gpo_cb_added;
static int gpo_falling;

static void gpo_edge(const struct device *port, struct gpio_callback *callback, uint32_t pins)
{
  ARG_UNUSED(port);
  ARG_UNUSED(callback);
  ARG_UNUSED(pins);
  gpo_falling++;
}

/* the tag's GPO pulls gpio1 pin 10 low; gpo_falling counts the edges */
static void gpo_line_attach(void)
{
  zassert_ok(gpio_pin_configure(G1_GPIO1, GPO_PIN, GPIO_INPUT | GPIO_PULL_UP));
  st25dv_emul_connect_gpo(G1_GPIO1, GPO_PIN);
  gpio_init_callback(&gpo_cb, gpo_edge, BIT(GPO_PIN));
  zassert_ok(gpio_add_callback(G1_GPIO1, &gpo_cb));
  gpo_cb_added = true;
  zassert_ok(gpio_pin_interrupt_configure(G1_GPIO1, GPO_PIN, GPIO_INT_EDGE_FALLING));
  gpo_falling = 0;
}

static void wlc_after(void *fixture)
{
  ARG_UNUSED(fixture);
  if (gpo_cb_added) {
    (void)gpio_pin_interrupt_configure(G1_GPIO1, GPO_PIN, GPIO_INT_DISABLE);
    (void)gpio_remove_callback(G1_GPIO1, &gpo_cb);
    (void)gpio_pin_configure(G1_GPIO1, GPO_PIN, GPIO_DISCONNECTED);
    gpo_cb_added = false;
  }
}

ZTEST_SUITE(nfc_wlc, NULL, NULL, wlc_before, wlc_after, NULL);

static const uint8_t cap_default[10] = {0, 0, 0, 1, 9, 9, 0x1e, 3, 0x0a, 0};

static uint32_t field_reading(void)
{
  uint32_t mv = 0xdeadbeef;

  zassert_equal(nfc_read_field_mv(&mv), 1);
  return mv;
}

static void listener_defaults(struct wlc_listener *listener)
{
  static const uint8_t stai[9] = {1, 5, 0, 0, 0, 0, 0, 0, 0};

  memset(listener, 0, sizeof(*listener));
  listener->active = 1;
  memcpy(listener->cap_info, cap_default, sizeof(cap_default));
  memcpy(listener->stai_info, stai, sizeof(stai));
  listener->fod_info[1] = 2;
}

/* ---- field voltage ---- */

ZTEST(nfc_wlc, test_field_reading_is_ten_times_the_pin_millivolts_below_1_2v)
{
  static const struct {
    uint32_t pin_mv;
    uint32_t reading;
    uint32_t tolerance;
  } rows[] = {
    {600, 6000, 10},
    {55, 550, 10},
    {0, 0, 0},         /* no voltage: no field */
    {1150, 11500, 10},
    {1250, 0, 0},      /* 1.2 V or more: no field */
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    set_pin_mv(rows[row].pin_mv);
    zassert_within(field_reading(), rows[row].reading, rows[row].tolerance,
             "pin %u mV", rows[row].pin_mv);
  }
}

static int adc_fails(const struct device *dev, unsigned int chan, void *data, uint32_t *mv)
{
  ARG_UNUSED(dev);
  ARG_UNUSED(chan);
  ARG_UNUSED(data);
  ARG_UNUSED(mv);
  return -EIO;
}

ZTEST(nfc_wlc, test_adc_failure_reads_as_no_field)
{
  zassert_ok(adc_emul_value_func_set(ADC0, 3, adc_fails, NULL));
  zassert_equal(field_reading(), 0);
}

ZTEST(nfc_wlc, test_field_is_detected_above_5_1v)
{
  set_pin_mv(530);
  zassert_true(nfc_field_detected());
  expect_within(nfc_get_field_mv(), 5300, 10); /* the reading is kept */

  set_pin_mv(490);
  zassert_false(nfc_field_detected());
  expect_within(nfc_get_field_mv(), 4900, 10);
}

ZTEST(nfc_wlc, test_vdc_rect_is_a_fresh_reading)
{
  set_pin_mv(700);
  expect_within(chargerGetVdcRect(), 7000, 10);
  set_pin_mv(0);
  zassert_equal(chargerGetVdcRect(), 0);
  zassert_equal(nfc_get_field_mv(), 0);
}

/* the case field is a reading above 5.1 V */
ZTEST(nfc_wlc, test_charge_state_follows_the_case_field_and_the_battery_level)
{
  static const struct {
    uint32_t pin_mv;
    uint8_t battery;
    uint8_t state;
  } rows[] = {
    {0, 50, 2},    /* no field */
    {400, 50, 2},
    {600, 50, 1},  /* charging */
    {600, 99, 1},
    {600, 100, 4}, /* full */
  };

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    struct nfc_charge charge = {.state = 3};

    g1_state()->battery_left = rows[row].battery;
    set_pin_mv(rows[row].pin_mv);
    nfc_field_voltage_update();
    nfc_charge_update_state(&charge);
    zassert_equal(charge.state, rows[row].state, "pin %u mV, battery %u %%",
            rows[row].pin_mv, rows[row].battery);
  }
}

/* ---- listener ---- */

ZTEST(nfc_wlc, test_field_appearing_is_reported_present_and_keeps_the_listener_state)
{
  struct wlc_listener listener;
  uint8_t present = 0;

  listener_defaults(&listener);
  listener.state = 5;
  set_pin_mv(600);

  wlc_update_field_state(&listener, &present);
  zassert_equal(present, 1);
  zassert_equal(listener.state, 5);
}

ZTEST(nfc_wlc, test_field_lost_while_not_charging_restarts_the_listener)
{
  struct wlc_listener listener;
  uint8_t present = 0;

  listener_defaults(&listener);
  set_pin_mv(600);
  wlc_update_field_state(&listener, &present);
  listener.state = 10;

  set_pin_mv(0);
  wlc_update_field_state(&listener, &present);
  zassert_equal(present, 0);
  zassert_equal(listener.state, 1, "back to the initialisation state");
}

ZTEST(nfc_wlc, test_field_lost_while_charging_keeps_the_listener_state)
{
  struct wlc_listener listener;
  uint8_t present = 0;

  listener_defaults(&listener);
  set_pin_mv(600);
  wlc_update_field_state(&listener, &present);
  listener.state = 14;
  listener.charging = 1;

  set_pin_mv(0);
  wlc_update_field_state(&listener, &present);
  zassert_equal(present, 0);
  zassert_equal(listener.state, 14);
}

ZTEST(nfc_wlc, test_no_field_without_an_earlier_field_changes_nothing)
{
  struct wlc_listener listener;
  uint8_t present = 1;

  listener_defaults(&listener);
  listener.state = 10;

  wlc_update_field_state(&listener, &present);
  zassert_equal(present, 0);
  zassert_equal(listener.state, 10);
}

ZTEST(nfc_wlc, test_tick_elapsed_counts_milliseconds)
{
  nfc_tick_reset();
  g1_clock_advance_ms(150);

  expect_within(nfc_tick_elapsed(), 150, 1);
}

ZTEST(nfc_wlc, test_listener_settles_150ms_after_writing_its_records)
{
  struct wlc_listener listener;

  listener_defaults(&listener);
  listener.state = 3;
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 4);

  g1_clock_advance_ms(140);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 4, "still settling after 140 ms");
  g1_clock_advance_ms(20);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 9);
}

ZTEST(nfc_wlc, test_listener_waits_the_wlc_interval_before_polling_again)
{
  struct wlc_listener listener;

  listener_defaults(&listener);
  listener.state = 9;
  wlc_listener_process(&listener); /* CAP_WT_INT 9: 2^12 - 50 = 4046 ms */
  zassert_equal(listener.state, 10);

  g1_clock_advance_ms(4000);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 10, "still waiting after 4000 ms");
  g1_clock_advance_ms(100);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 11);
}

ZTEST(nfc_wlc, test_field_loss_is_reported_after_3s_without_field)
{
  struct wlc_listener listener;
  struct nfc_charge charge = {0};

  listener_defaults(&listener);
  g1_state()->battery_left = 50;
  set_pin_mv(600);
  nfc_charge_refresh(&charge, &listener);
  zassert_equal(charge.state, 1);

  set_pin_mv(0);
  g1_clock_advance_ms(2900);
  nfc_charge_refresh(&charge, &listener);
  zassert_equal(listener.field_lost, 0, "not yet after 2.9 s");
  g1_clock_advance_ms(200);
  nfc_charge_refresh(&charge, &listener);
  zassert_equal(listener.field_lost, 1);
}

/* ---- WLC message ---- */

/*
 * On the tag: the CC (e1 40 40 01), the NDEF TLV (03 <len>), the records, the terminator (fe).
 * WLCCAP: the capability record after its header byte (lengths, "WLCCAP", cap_default encoded).
 */
#define WLCCAP " 06 06 57 4c 43 43 41 50 20 01 09 1e 03 0a"

ZTEST(nfc_wlc, test_wlc_message_has_the_capability_and_status_records)
{
  static const uint8_t stai[9] = {1, 0x05, 0x32, 0, 0x01, 0, 0, 0, 0};
  static const uint8_t fod[3] = {0, 2, 0};

  zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
  /* "WLCSTAI": control 05h, battery level 32h, receive voltage 01h */
  expect_bytes(st25dv_emul_user_mem(), 35,
         "e1 40 40 01 03 1c 91" WLCCAP " 51 07 03 57 4c 43 53 54 41 49 05 32 01 fe");
}

ZTEST(nfc_wlc, test_wlc_message_without_status_is_the_capability_record_alone)
{
  static const uint8_t stai[9] = {0};
  static const uint8_t fod[3] = {0};

  zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
  expect_bytes(st25dv_emul_user_mem(), 22, "e1 40 40 01 03 0f d1" WLCCAP " fe");
}

ZTEST(nfc_wlc, test_wlc_foreign_object_record_is_wlcfod_with_its_two_bytes)
{
  static const uint8_t stai[9] = {0};
  static const uint8_t fod[3] = {1, 0x5a, 0xa5};

  zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
  /* "WLCFOD" 5a a5 */
  expect_bytes(st25dv_emul_user_mem(), 33,
         "e1 40 40 01 03 1a 91" WLCCAP " 51 06 02 57 4c 43 46 4f 44 5a a5 fe");
}

ZTEST(nfc_wlc, test_wlc_message_with_all_three_records_is_written_whole)
{
  static const uint8_t stai[9] = {1, 0x05, 0x32, 0, 0x01, 0, 0, 0, 0};
  static const uint8_t fod[3] = {1, 0x5a, 0xa5};

  zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
  zassert_equal(st25dv_emul_user_mem()[5], 39, "NDEF TLV length");
}

/* ---- nfc_init() ---- */

ZTEST(nfc_wlc, test_init_reads_the_identity_of_an_st25dv04kc_and_accepts_it)
{
  zassert_equal(nfc_init(), 0);
  zassert_equal_ptr(global_st25dv_context_ptr, &global_st25dv_context);
  zassert_equal(nfc_is_tag_type_ok(), 1);
}

ZTEST(nfc_wlc, test_tag_type_is_ok_for_the_kc_series_ie_product_codes_only)
{
  static const struct {
    uint8_t code;
    uint8_t ok;
  } rows[] = {
    {0x50, 1}, /* 04KC */
    {0x51, 1}, /* 16KC / 64KC */
    {0x24, 0}, /* 04K */
    {0x26, 0}, /* 16K / 64K */
    {0x00, 0},
  };
  uint8_t uid[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0x00, 0x02, 0xe0};

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    uid[5] = rows[row].code;
    st25dv_emul_set_uid(uid);
    nfc_tag_type_ok = 0;
    nfc_init();
    zassert_equal(nfc_is_tag_type_ok(), rows[row].ok, "product code %02x",
            rows[row].code);
  }
}

ZTEST(nfc_wlc, test_init_on_a_silent_tag_leaves_the_tag_type_unknown)
{
  st25dv_emul_set_nak(true);

  zassert_equal(nfc_init(), 0);
  zassert_equal(nfc_is_tag_type_ok(), 0);
}

ZTEST(nfc_wlc, test_init_without_the_bus_stops_before_the_tag)
{
  global_st25dv_context_ptr = NULL;
  g1_device_set_ready(G1_I2C1, false);

  zassert_equal(nfc_init(), 0);
  zassert_is_null(global_st25dv_context_ptr);
  zassert_equal(st25dv_emul_xfers(), 0);
  zassert_equal(nfc_is_tag_type_ok(), 0);
}

ZTEST(nfc_wlc, test_init_starts_out_of_the_case)
{
  set_box_status(BOX_IN_BOX, 1);
  set_box_status(BOX_LID_CLOSED, 1);
  set_box_status(2, 1);

  nfc_init();
  zassert_equal(is_in_box(), 0);
  zassert_equal(is_box_lid_closed(), 0);
  zassert_equal(is_box_charging(), 0);
}

/* ---- nfc_wlc_process() ---- */

/* index of the first successful transfer to addr that wrote exactly hex, -1 if none */
static int find_xfer(uint8_t addr, const char *hex)
{
  uint8_t want[ST25DV_EMUL_LOG_BYTES];
  int len = g1_hex(hex, want, sizeof(want));

  for (int index = 0; index < MIN(st25dv_emul_xfers(), ST25DV_EMUL_LOG); index++) {
    const struct st25dv_emul_xfer *xfer = st25dv_emul_xfer(index);

    if (xfer->addr == addr && xfer->wr_len == len && memcmp(xfer->wr, want, len) == 0 &&
        xfer->ret == 0) {
      return index;
    }
  }
  return -1;
}

ZTEST(nfc_wlc, test_first_pass_configures_the_tag_inside_the_i2c_session)
{
  int opened;
  int gpo_set;
  int closed;

  zassert_ok(st25dv_write_eh_ctrl_dyn(global_st25dv_context_ptr, ST25DV_EMUL_EH_EN));

  zassert_equal(nfc_wlc_process(), 0);

  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO1), 0xff);
  zassert_equal(st25dv_emul_get_sys(ST25DV_EMUL_SYS_GPO2), 0x0c);
  zassert_equal(st25dv_emul_get_dyn(ST25DV_EMUL_DYN_EH_CTRL) & ST25DV_EMUL_EH_EN, 0,
          "energy harvesting off");
  zassert_false(st25dv_emul_session_open(), "session closed again");

  /* I2C_PWD: the zero password, GPO1 = ff, then a wrong password */
  opened = find_xfer(0x57, "09 00 00 00 00 00 00 00 00 00 09 00 00 00 00 00 00 00 00");
  gpo_set = find_xfer(0x57, "00 00 ff");
  closed = find_xfer(0x57, "09 00 12 34 56 78 13 24 56 78 09 12 34 56 78 13 24 56 78");
  zassert_true(opened >= 0 && gpo_set > opened && closed > gpo_set,
         "password %d, GPO1 %d, wrong password %d", opened, gpo_set, closed);
}

ZTEST(nfc_wlc, test_tag_is_configured_on_the_first_pass_only)
{
  int first;

  nfc_wlc_process();
  first = st25dv_emul_xfers();
  nfc_wlc_process();

  for (int index = first; index < MIN(st25dv_emul_xfers(), ST25DV_EMUL_LOG); index++) {
    const struct st25dv_emul_xfer *xfer = st25dv_emul_xfer(index);

    zassert_false(memcmp(xfer->wr, "\x09\x00", 2) == 0 && xfer->addr == 0x57,
            "password presented again (transfer %d)", index);
  }
}

ZTEST(nfc_wlc, test_third_pass_puts_the_wlc_records_with_the_battery_level_on_the_tag)
{
  g1_state()->battery_left = 75;
  for (int pass = 0; pass < 3; pass++) {
    zassert_equal(nfc_wlc_process(), 0);
  }

  /* "WLCSTAI": control 05h, battery level 4bh */
  expect_bytes(st25dv_emul_user_mem(), 33,
         "e1 40 40 01 03 1c 91" WLCCAP " 51 07 03 57 4c 43 53 54 41 49 05 4b");
  zassert_equal(st25dv_emul_user_mem()[34], 0xfe);
}

/* ---- GPO line ---- */

/* the case's 0x13 query with its sum */
static const uint8_t case_query[] = {0x13, 0x01, 0x02, 0x00, 0x16};

ZTEST(nfc_wlc, test_configured_tag_interrupts_the_lens_on_field_and_case_message)
{
  zassert_equal(nfc_wlc_process(), 0);
  gpo_line_attach();

  st25dv_emul_rf_field(true);
  zassert_equal(gpo_falling, 1, "field appeared");
  st25dv_emul_enable_mailbox();
  zassert_ok(st25dv_emul_rf_put_msg(case_query, sizeof(case_query)));
  zassert_equal(gpo_falling, 2, "case message");
  zassert_equal(gpio_pin_get_raw(G1_GPIO1, GPO_PIN), 1, "line back to idle (high)");
}

ZTEST(nfc_wlc, test_it_status_reports_a_case_message_and_clears)
{
  uint8_t status = 0;

  st25dv_emul_set_sys(ST25DV_EMUL_SYS_GPO1, 0xff);
  st25dv_emul_enable_mailbox();
  zassert_ok(st25dv_emul_rf_put_msg(case_query, sizeof(case_query)));

  zassert_ok(nfc_read_it_status(&status));
  zassert_equal(status & ST25DV_EMUL_IT_RF_PUT_MSG, ST25DV_EMUL_IT_RF_PUT_MSG);
  zassert_ok(nfc_read_it_status(&status));
  zassert_equal(status, 0);
}

ZTEST(nfc_wlc, test_it_status_rejects_null_and_a_missing_tag)
{
  uint8_t status;

  zassert_equal(nfc_read_it_status(NULL), -1);
  global_st25dv_context_ptr = NULL;
  zassert_equal(nfc_read_it_status(&status), -1);
  zassert_equal(st25dv_emul_xfers(), 0);
}

ZTEST(nfc_wlc, test_it_status_of_a_silent_tag_is_tried_10_times)
{
  uint8_t status;

  st25dv_emul_set_nak(true);

  zassert_equal(nfc_read_it_status(&status), -EIO);
  zassert_equal(st25dv_emul_xfers(), 10 * 20, "10 reads of 20 attempts");
}

/* ---- the message the case reads on detection ---- */

/*
 * logs/case/case_mailbox.hex ndef_wlc_*: WLCCAP 20 01 09 1e 03 0a, WLCSTAI 05 <level> 01 (control
 * byte 5: battery level and receive voltage; (battery_voltage + 200) >> 8 = 1 for a voltage byte
 * of 56 or more). The level is the lens's at the time of the write, see the staleness test.
 */
static void expect_detection_message(uint8_t battery, const char *ndef)
{
  char hex[160];

  g1_state()->battery_left = battery;
  g1_state()->battery_voltage = 0xd0;
  for (int pass = 0; pass < 3; pass++) {
    zassert_equal(nfc_wlc_process(), 0);
  }

  snprintf(hex, sizeof(hex), "e1 40 40 01 03 1c %s fe", ndef);
  expect_bytes(st25dv_emul_user_mem(), 35, hex);
}

ZTEST(nfc_wlc, test_right_lens_message_on_the_tag_is_the_one_the_case_read_on_detection)
{
  /* ndef_wlc_right_lens: level 24h */
  expect_detection_message(36, "91 06 06 57 4c 43 43 41 50 20 01 09 1e 03 0a"
             " 51 07 03 57 4c 43 53 54 41 49 05 24 01");
}

ZTEST(nfc_wlc, test_left_lens_message_on_the_tag_is_the_one_the_case_read_on_detection)
{
  /* ndef_wlc_left_lens: level 0fh */
  expect_detection_message(15, "91 06 06 57 4c 43 43 41 50 20 01 09 1e 03 0a"
             " 51 07 03 57 4c 43 53 54 41 49 05 0f 01");
}

/* WLCSTAI on the tag: CC (4), TLV (2), WLCCAP record (15), WLCSTAI header (10), control, level */
#define STAI_LEVEL_OFFSET 32

/*
 * Seen on the bench (case_nfc_protocol.md section 6): a docked lens presents the level of its last
 * cold start. The listener writes WLCSTAI from state 3 only; in the charging states 12 and 14 a
 * level change does not reach the tag, and a field loss while charging keeps the state, so the
 * re-docked lens still shows the old level. Known limitation, pinned.
 */
ZTEST(nfc_wlc, test_wlc_status_level_is_not_rewritten_while_the_listener_is_charging)
{
  struct wlc_listener listener;

  listener_defaults(&listener);
  listener.stai_info[2] = 36;
  listener.state = 3;
  set_pin_mv(600);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 4);
  zassert_equal(st25dv_emul_user_mem()[STAI_LEVEL_OFFSET], 36);

  /* the lens fills: state 3 -> 12 (one more write) -> 14 */
  listener.charging = 1;
  listener.state = 3;
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 12);
  g1_clock_advance_ms(10);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14);
  zassert_equal(st25dv_emul_user_mem()[STAI_LEVEL_OFFSET], 36);

  /* 38 points later: nothing reaches the tag */
  listener.stai_info[2] = 74;
  g1_clock_advance_ms(10);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14);
  zassert_equal(st25dv_emul_user_mem()[STAI_LEVEL_OFFSET], 36, "old level kept");

  /* the field vanishes and returns: the state is kept and the old level stays on the tag */
  set_pin_mv(0);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14, "kept through the field loss");
  g1_clock_advance_ms(300);
  set_pin_mv(600);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14);
  zassert_equal(st25dv_emul_user_mem()[STAI_LEVEL_OFFSET], 36, "re-docked with the old level");
}

/* ---- the case's field as the lens sees it ---- */

/*
 * Re-dock ramp 0 -> 5800 mV (case_nfc_protocol.md section 6): the charge state follows the field
 * alone; the charging flag the case reads (cf) is the sign of the battery current, test_case.c.
 */
ZTEST(nfc_wlc, test_redock_field_ramp_sets_the_charge_state_from_the_field_alone)
{
  static const struct {
    uint32_t pin_mv;
    uint32_t field_mv;
    uint32_t tolerance;
    uint8_t state;
  } rows[] = {
    {0, 0, 0, 2},       /* no field yet */
    {580, 5800, 10, 1}, /* full field, the charger still off */
  };
  struct wlc_listener listener;
  struct nfc_charge charge = {0};

  listener_defaults(&listener);
  g1_state()->battery_left = 74;

  for (size_t row = 0; row < ARRAY_SIZE(rows); row++) {
    set_pin_mv(rows[row].pin_mv);
    nfc_charge_refresh(&charge, &listener);
    zassert_within(charge.field_mv, rows[row].field_mv, rows[row].tolerance,
             "pin %u mV", rows[row].pin_mv);
    zassert_equal(charge.state, rows[row].state, "pin %u mV", rows[row].pin_mv);
    zassert_equal(listener.charging, 0, "pin %u mV: not full", rows[row].pin_mv);
  }
}

/*
 * The case regulates its field to 4.4..4.9 V once the charger draws current (every captured
 * steady-state reply): a field that was above 5.1 V counts down to NFC_FIELD_HOLD_MV.
 */
ZTEST(nfc_wlc, test_charge_state_is_charging_at_the_case_regulated_4400mv_field)
{
  struct wlc_listener listener;
  struct nfc_charge charge = {0};

  listener_defaults(&listener);
  g1_state()->battery_left = 74;
  set_pin_mv(580);
  nfc_charge_refresh(&charge, &listener);
  zassert_equal(charge.state, 1);

  set_pin_mv(440); /* the case stepped its power down, the charger on */
  nfc_charge_refresh(&charge, &listener);
  expect_within(charge.field_mv, 4400, 10);
  zassert_equal(charge.state, 1, "charging at 4400 mV");
}

/* the case reboots: its field is off for ~300 ms and comes back */
ZTEST(nfc_wlc, test_case_reboot_field_gap_restarts_a_listener_that_was_not_charging)
{
  struct wlc_listener listener;
  struct nfc_charge charge = {0};

  listener_defaults(&listener);
  g1_state()->battery_left = 74;
  listener.state = 10;
  set_pin_mv(580);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 11);

  /* the field loss puts the listener back to state 1, and the same pass runs it to 2 */
  set_pin_mv(0);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 2, "restarted");

  g1_clock_advance_ms(300);
  set_pin_mv(580);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 3, "starting over, not back at 11");
}

/* a full lens keeps wlc->charging through the gap, so wlc_update_field_state() leaves state 14 */
ZTEST(nfc_wlc, test_case_reboot_field_gap_keeps_the_state_of_a_charging_listener)
{
  struct wlc_listener listener;
  struct nfc_charge charge = {0};

  listener_defaults(&listener);
  g1_state()->battery_left = 100; /* full: the charge state that marks the listener charging */
  listener.state = 3;
  set_pin_mv(580);
  nfc_charge_refresh(&charge, &listener);
  zassert_equal(charge.state, 4);
  zassert_equal(listener.charging, 1);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 12);
  g1_clock_advance_ms(10);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14);

  set_pin_mv(0);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14, "kept through the field loss");

  g1_clock_advance_ms(300);
  set_pin_mv(580);
  nfc_charge_refresh(&charge, &listener);
  wlc_listener_process(&listener);
  zassert_equal(listener.state, 14);
}
