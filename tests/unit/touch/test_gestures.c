/* Key events: 1-3 taps, 4 long press, 5 its release, 6 its timeout. Polled every 250 ms. */
#include "aw9320x_emul.h"
#include "drivers.h"

/* threads.c uses it before its definition */
void init_key_event_sem(void);

#include "button/button.c"
#include "threads/threads.c"

/* threads.c and button.c dependencies that no test checks */
FAKE_VALUE_FUNC(int, config_watchdog_wait_reset);
FAKE_VALUE_FUNC(uint8_t, nfc_is_tag_type_ok);
FAKE_VOID_FUNC(reset_all_usr_data, struct GlassesState *, int);
FAKE_VOID_FUNC(set_band_for_ESB, uint8_t);
uint8_t even_ai_scrolling;

/* lens ticks (32768 Hz) in ms, rounded down */
#define SEQ_MS        305   /* 10000 ticks */
#define TAP_MS        457   /* 15000 ticks */
#define LP_SHORT_MS   2746  /* 3 x 30000 ticks */
#define LP_TIMEOUT_MS 32043 /* 35 x 30000 ticks */
#define POLL_MS       250
#define SLACK_MS      2

#define EVENTS_MAX 16

struct key_event_record {
  int type;
  int64_t at_ms;
};

static struct key_event_record events[EVENTS_MAX];
static volatile int event_count;
static int64_t start_ms; /* script time 0, set by start(): every at_ms counts from it */

/* stands in for key_event_thread() */
static void key_consumer_entry(void *arg, void *unused1, void *unused2) {
  ARG_UNUSED(arg);
  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  for (;;) {
    k_sem_take(&key_event_sem, K_FOREVER);
    if (event_count < EVENTS_MAX) {
      events[event_count].type = key_event_type;
      events[event_count].at_ms = k_uptime_get() - start_ms;
    }
    event_count++;
    key_event_type = 0;
  }
}

static void gestures_before(void *fixture) {
  ARG_UNUSED(fixture);
  g1_gpio_remove_callback(G1_GPIO1, &button1_irq_cb);
  g1_gpio_remove_callback(G1_GPIO1, &button2_irq_cb);
  g1_gpio_port_reset(G1_GPIO0);
  g1_gpio_port_reset(G1_GPIO1);
  touch_irq_pending = 0;
  aw93203_ready = 0;
  aw9320x_wearing = UNKNOWN;
  lp_timeout_handled = 0;
  key_event_type = 0;
  event_count = 0;
}

ZTEST_SUITE(touch_gestures, NULL, NULL, gestures_before, NULL, NULL);

static void start(void) {
  button_init();
  aw9320x_emul_sync_int();
  aw93203_init();
  zassert_equal(aw93203_ready, 1, "controller init failed");
  zassert_false(aw9320x_emul_int_asserted());
  init_key_event_sem();

  g1_thread_start(key_consumer_entry, NULL, K_PRIO_PREEMPT(0));
  g1_thread_start(touch_key_thread, g1_state(), K_PRIO_PREEMPT(1));
  g1_clock_advance_ms(1);
  start_ms = k_uptime_get();
}

static void wait_until(int64_t at_ms) {
  expect_elapsed_ms(start_ms, 0, at_ms);
  g1_clock_advance_ms((uint32_t)(start_ms + at_ms - k_uptime_get()));
}

static void touch_at(int64_t at_ms, bool down) {
  wait_until(at_ms);
  zassert_true(aw9320x_emul_touch(down), "touch event not reported by the chip");
}

static void tap_at(int64_t at_ms, int64_t len) {
  touch_at(at_ms, true);
  touch_at(at_ms + len, false);
}

static void expect_events(int count) {
  zassert_equal(event_count, count, "%d key events, expected %d (first: type %d at %lld ms)", event_count, count,
                event_count ? events[0].type : 0, event_count ? events[0].at_ms : 0);
}

static void expect_event(int index, int type, int64_t due) {
  zassert_true(index < event_count, "key event %d missing", index);
  zassert_equal(events[index].type, type, "key event %d: type %d, expected %d", index, events[index].type, type);
  zassert_true(events[index].at_ms > due && events[index].at_ms <= due + POLL_MS + SLACK_MS,
               "key event %d (type %d) at %lld ms, expected in (%lld, %lld]", index, type, events[index].at_ms, due,
               due + POLL_MS + SLACK_MS);
}

ZTEST(touch_gestures, test_single_tap_is_key_event_1) {
  start();
  tap_at(100, 100);
  wait_until(2000);

  expect_events(1);
  expect_event(0, 1, 200 + SEQ_MS);
}

ZTEST(touch_gestures, test_double_tap_is_key_event_2) {
  start();
  tap_at(100, 80);
  tap_at(300, 80);
  wait_until(2000);

  expect_events(1);
  expect_event(0, 2, 380 + SEQ_MS);
}

ZTEST(touch_gestures, test_triple_tap_is_key_event_3) {
  start();
  tap_at(100, 80);
  tap_at(300, 80);
  tap_at(500, 80);
  wait_until(2000);

  expect_events(1);
  expect_event(0, 3, 580 + SEQ_MS);
}

ZTEST(touch_gestures, test_two_taps_further_apart_than_the_window_are_two_single_taps) {
  start();
  tap_at(100, 80);
  tap_at(600, 80);
  wait_until(2000);

  expect_events(2);
  expect_event(0, 1, 180 + SEQ_MS);
  expect_event(1, 1, 680 + SEQ_MS);
}

/* a press at 100 ms released at release_ms: held (4) after TAP_MS, then type at due */
#define PRESS_TEST(name, release_ms, end_ms, type, due)                                                                \
  ZTEST(touch_gestures, test_##name) {                                                                                 \
    start();                                                                                                           \
    touch_at(100, true);                                                                                               \
    touch_at(release_ms, false);                                                                                       \
    wait_until(end_ms);                                                                                                \
    expect_events(2);                                                                                                  \
    expect_event(0, 4, 100 + TAP_MS);                                                                                  \
    expect_event(1, type, due);                                                                                        \
  }

PRESS_TEST(long_press_reports_held_then_released, 2100, 4000, 5, 2100 + SEQ_MS)
PRESS_TEST(long_press_held_too_long_times_out, 35100, 37000, 6, 100 + LP_TIMEOUT_MS)
PRESS_TEST(short_press_ends_on_the_shortened_timeout, 900, 4000, 6, 100 + LP_SHORT_MS)

/* signal >> 10 must reach 20000 */
ZTEST(touch_gestures, test_weak_touch_is_ignored) {
  aw9320x_emul()->touch_diff = 19999 * 1024;

  start();
  tap_at(100, 100);
  wait_until(2000);

  expect_events(0);
}

ZTEST(touch_gestures, test_strong_negative_signal_is_a_touch) {
  aw9320x_emul()->touch_diff = -20000 * 1024;

  start();
  tap_at(100, 100);
  wait_until(2000);

  expect_events(1);
  expect_event(0, 1, 200 + SEQ_MS);
}

/* module_mask bit 0: touch */
ZTEST(touch_gestures, test_touch_module_off_reports_nothing) {
  g1_state()->module_mask &= ~BIT(0);

  start();
  tap_at(100, 100);
  wait_until(2000);

  expect_events(0);
}
