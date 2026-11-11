/* Key events: 1-3 taps, 4 long press, 5 its release, 6 its timeout. Polled every 250 ms. */
#include <zephyr/ztest.h>

#include "harness.h"
#include "aw9320x_emul.h"
#include "gpio_util.h"

/* threads.c uses it before its definition */
void init_key_event_sem(void);

#include "threads/threads.c"
#include "button/button.c"

#define E G1_AW9320X_EMUL

/* lens ticks (32768 Hz) in ms, rounded down */
#define SEQ_MS        305   /* 10000 ticks */
#define TAP_MS        457   /* 15000 ticks */
#define LONG_END_MS   1098  /* 36000 ticks */
#define LP_SHORT_MS   2746  /* 3 x 30000 ticks */
#define LP_TIMEOUT_MS 32043 /* 35 x 30000 ticks */
#define POLL_MS       250
#define SLACK_MS      2

#define EV_MAX 16

struct key_ev {
	int type;
	int64_t t;
};

static struct key_ev ev[EV_MAX];
static volatile int ev_n;
static int64_t t0;

static K_THREAD_STACK_DEFINE(touch_stack, 4096);
static struct k_thread touch_thread;
static K_THREAD_STACK_DEFINE(key_stack, 2048);
static struct k_thread key_thread;
static bool threads_started;

/* stands in for key_event_thread() */
static void key_consumer(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	for (;;) {
		k_sem_take(&key_event_sem, K_FOREVER);
		if (ev_n < EV_MAX) {
			ev[ev_n].type = key_event_type;
			ev[ev_n].t = k_uptime_get() - t0;
		}
		ev_n++;
		key_event_type = 0;
	}
}

static void gestures_before(void *fixture)
{
	ARG_UNUSED(fixture);
	g1_gpio_remove_callback(G1_GPIO1, &button1_irq_cb);
	g1_gpio_remove_callback(G1_GPIO1, &button2_irq_cb);
	g1_gpio_port_reset(G1_GPIO0);
	g1_gpio_port_reset(G1_GPIO1);
	touch_irq_pending = 0;
	aw93203_ready = 0;
	aw9320x_wearing = UNKNOWN;
	even_ai_scrolling = 0;
	lp_timeout_handled = 0;
	key_event_type = 0;
	ev_n = 0;
	threads_started = false;
}

static void gestures_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (threads_started) {
		k_thread_abort(&touch_thread);
		k_thread_abort(&key_thread);
		threads_started = false;
	}
}

ZTEST_SUITE(touch_gestures, NULL, NULL, gestures_before, gestures_after, NULL);

static void start(void)
{
	button_init();
	aw9320x_emul_sync_int(E);
	aw93203_init();
	zassert_equal(aw93203_ready, 1, "controller init failed");
	zassert_false(aw9320x_emul_int_asserted(E));
	init_key_event_sem();

	k_thread_create(&key_thread, key_stack, K_THREAD_STACK_SIZEOF(key_stack), key_consumer,
			NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_thread_create(&touch_thread, touch_stack, K_THREAD_STACK_SIZEOF(touch_stack),
			touch_key_thread, g1_state(), NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	threads_started = true;
	g1_clock_advance_ms(1);
	t0 = k_uptime_get();
}

static int64_t now(void)
{
	return k_uptime_get() - t0;
}

static void wait_until(int64_t t)
{
	zassert_true(now() <= t, "script time %lld already passed", t);
	g1_clock_advance_ms((uint32_t)(t - now()));
}

static void touch_at(int64_t t, bool down)
{
	wait_until(t);
	zassert_true(aw9320x_emul_touch(E, down), "touch event not reported by the chip");
}

static void tap_at(int64_t t, int64_t len)
{
	touch_at(t, true);
	touch_at(t + len, false);
}

static void expect_events(int n)
{
	zassert_equal(ev_n, n, "%d key events, expected %d (first: type %d at %lld ms)", ev_n, n,
		      ev_n ? ev[0].type : 0, ev_n ? ev[0].t : 0);
}

static void expect_event(int i, int type, int64_t due)
{
	zassert_true(i < ev_n, "key event %d missing", i);
	zassert_equal(ev[i].type, type, "key event %d: type %d, expected %d", i, ev[i].type, type);
	zassert_true(ev[i].t > due && ev[i].t <= due + POLL_MS + SLACK_MS,
		     "key event %d (type %d) at %lld ms, expected in (%lld, %lld]", i, type,
		     ev[i].t, due, due + POLL_MS + SLACK_MS);
}

ZTEST(touch_gestures, test_single_tap_is_key_event_1)
{
	start();
	tap_at(100, 100);
	wait_until(2000);

	expect_events(1);
	expect_event(0, 1, 200 + SEQ_MS);
}

ZTEST(touch_gestures, test_double_tap_is_key_event_2)
{
	start();
	tap_at(100, 80);
	tap_at(300, 80);
	wait_until(2000);

	expect_events(1);
	expect_event(0, 2, 380 + SEQ_MS);
}

ZTEST(touch_gestures, test_triple_tap_is_key_event_3)
{
	start();
	tap_at(100, 80);
	tap_at(300, 80);
	tap_at(500, 80);
	wait_until(2000);

	expect_events(1);
	expect_event(0, 3, 580 + SEQ_MS);
}

/*
 * DEFECT: sequences are only classified on a poll; a press before that restarts the count and the
 * first tap is lost.
 */
ZTEST_EXPECT_FAIL(touch_gestures, test_two_taps_further_apart_than_the_window_are_two_single_taps);
ZTEST(touch_gestures, test_two_taps_further_apart_than_the_window_are_two_single_taps)
{
	start();
	tap_at(100, 80);
	tap_at(600, 80);
	wait_until(2000);

	expect_events(2);
	expect_event(0, 1, 180 + SEQ_MS);
	expect_event(1, 1, 680 + SEQ_MS);
}

ZTEST(touch_gestures, test_long_press_reports_held_then_released)
{
	start();
	touch_at(100, true);
	touch_at(2100, false);
	wait_until(4000);

	expect_events(2);
	expect_event(0, 4, 100 + TAP_MS);
	expect_event(1, 5, 2100 + SEQ_MS);
}

ZTEST(touch_gestures, test_long_press_held_too_long_times_out)
{
	start();
	touch_at(100, true);
	wait_until(35100);
	touch_at(35100, false);
	wait_until(37000);

	expect_events(2);
	expect_event(0, 4, 100 + TAP_MS);
	expect_event(1, 6, 100 + LP_TIMEOUT_MS);
}

ZTEST(touch_gestures, test_short_press_ends_on_the_shortened_timeout)
{
	start();
	touch_at(100, true);
	touch_at(900, false);
	wait_until(4000);

	expect_events(2);
	expect_event(0, 4, 100 + TAP_MS);
	expect_event(1, 6, 100 + LP_SHORT_MS);
}

/* signal >> 10 must reach 20000 */
ZTEST(touch_gestures, test_weak_touch_is_ignored)
{
	aw9320x_emul_set_touch_diff(E, 19999 * 1024);

	start();
	tap_at(100, 100);
	wait_until(2000);

	expect_events(0);
}

ZTEST(touch_gestures, test_strong_negative_signal_is_a_touch)
{
	aw9320x_emul_set_touch_diff(E, -20000 * 1024);

	start();
	tap_at(100, 100);
	wait_until(2000);

	expect_events(1);
	expect_event(0, 1, 200 + SEQ_MS);
}

/* module_mask bit 0: touch */
ZTEST(touch_gestures, test_touch_module_off_reports_nothing)
{
	g1_state()->module_mask &= ~BIT(0);

	start();
	tap_at(100, 100);
	wait_until(2000);

	expect_events(0);
}
