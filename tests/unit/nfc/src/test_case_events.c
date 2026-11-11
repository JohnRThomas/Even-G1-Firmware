/* check_work_mode() is silent for the first 10 s after boot. */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_nfc.h"
#include "nfc_test.h"

/* used before its definition; the host gcc rejects implicit declarations */
void init_key_event_sem(void);
#include "threads/threads.c"

#define E TAG_E

struct g1_nrf_reset g1_nrf_reset_s;

#define EVT_BOUND        0x11
#define EVT_OUT_UNWORN   0x07
#define EVT_CASE_CLOSED  0x0b
#define EVT_BATTERY      0x0a
#define EVT_CHARGING     0x09

static K_THREAD_STACK_DEFINE(test_nfc_stack, 8192);
static struct k_thread test_nfc_thread;
static bool test_nfc_thread_running;

static void events_before(void *fixture)
{
	ARG_UNUSED(fixture);
	nfc_wlc_test_reset();
	nfc_pt_test_reset();
	init_box_status();
	st25dv_emul_set_write_time_ms(E, 1);

	wear_state_event = 0xff;
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
	can_enter_low_power = 0;

	get_work_mode_fake.return_val = 5;            /* running, not boot / DFU */
	g1_ble()->receiver_thd_is_running = 1;
	g1_state()->battery_left = 50;
	if (g1_clock_uptime_ms() < 10000) {
		g1_clock_advance_ms(10000);
	}
}

static void events_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (test_nfc_thread_running) {
		k_thread_abort(&test_nfc_thread);
		test_nfc_thread_running = false;
	}
}

ZTEST_SUITE(nfc_events, NULL, NULL, events_before, events_after, NULL);

/* returns the event id, -1 for none */
static int poll(int in_box, int lid_closed)
{
	int n = g1_nfc_bt_log.count;

	check_work_mode(in_box, lid_closed, 1);
	if (g1_nfc_bt_log.count == n) {
		return -1;
	}
	zassert_equal(g1_nfc_bt_log.count, n + 1, "one event per poll");
	zassert_equal(g1_nfc_bt_log.len[n], 3);
	zassert_equal(g1_nfc_bt_log.data[n][0], 0xf5);
	return g1_nfc_bt_log.data[n][1];
}

static void first_polls(int in_box, int lid_closed, int where)
{
	zassert_equal(poll(in_box, lid_closed), EVT_BOUND);
	zassert_equal(poll(in_box, lid_closed), where);
	zassert_equal(poll(in_box, lid_closed), EVT_BATTERY);
}

/* ---- F5 09 / F5 0A ---- */

ZTEST(nfc_events, test_charging_in_the_case_is_reported_as_f5_09_with_1)
{
	set_box_status(2, 1);

	first_polls(1, 1, EVT_CASE_CLOSED);
	zassert_equal(poll(1, 1), EVT_CHARGING);
	zassert_equal(g1_ble()->charging, 1, "F5 09 payload");
	zassert_equal(poll(1, 1), -1, "reported once");
}

ZTEST(nfc_events, test_end_of_charging_in_the_case_is_reported_after_5_polls)
{
	set_box_status(2, 1);
	first_polls(1, 1, EVT_CASE_CLOSED);
	zassert_equal(poll(1, 1), EVT_CHARGING);

	set_box_status(2, 0);
	for (int i = 1; i <= 4; i++) {
		zassert_equal(poll(1, 1), -1, "poll %d", i);
	}
	zassert_equal(poll(1, 1), EVT_CHARGING);
	zassert_equal(g1_ble()->charging, 0);
}

ZTEST(nfc_events, test_taking_the_lens_out_reports_not_charging_at_once)
{
	set_box_status(2, 1);
	first_polls(1, 1, EVT_CASE_CLOSED);
	zassert_equal(poll(1, 1), EVT_CHARGING);

	zassert_equal(poll(0, 0), EVT_OUT_UNWORN);
	zassert_equal(poll(0, 0), EVT_CHARGING);
	zassert_equal(g1_ble()->charging, 0);
}

ZTEST(nfc_events, test_battery_level_is_reported_as_f5_0a_when_it_changes)
{
	first_polls(0, 0, EVT_OUT_UNWORN);
	zassert_equal(poll(0, 0), EVT_CHARGING, "not charging out of the case");
	zassert_equal(poll(0, 0), -1);

	g1_state()->battery_left = 51;
	zassert_equal(poll(0, 0), EVT_BATTERY);
	zassert_equal(poll(0, 0), -1, "same level: nothing");
	g1_state()->battery_left = 49;
	zassert_equal(poll(0, 0), EVT_BATTERY);
}

/* ---- nfc_related() ---- */

static void test_nfc_thread_start(void)
{
	k_thread_create(&test_nfc_thread, test_nfc_stack, K_THREAD_STACK_SIZEOF(test_nfc_stack), nfc_related,
			NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	test_nfc_thread_running = true;
	k_sleep(K_MSEC(1));
	zassert_equal(nfc_sem_ready, 1);
}

static void case_talks(void)
{
	static const uint8_t query[] = {0x13, 0x01, 0x02, 0x00, 0x16};

	st25dv_emul_set_sys(E, ST25DV_EMUL_SYS_GPO1, 0xff);
	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, query, sizeof(query)));
	trigger_nfc_event();
}

ZTEST(nfc_events, test_message_from_the_case_puts_the_lens_in_the_case)
{
	uint8_t reply[256];

	test_nfc_thread_start();
	zassert_equal(is_in_box(), 0);

	case_talks();
	k_sleep(K_MSEC(200));

	zassert_equal(is_in_box(), 1, "isInbox");
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11, "answered");
	zassert_equal(reply[0], 0x13);
}

ZTEST(nfc_events, test_case_silent_for_3s_takes_the_lens_out_of_the_case)
{
	uint8_t reply[256];

	test_nfc_thread_start();
	case_talks();
	k_sleep(K_MSEC(200));
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
	set_box_status(BOX_LID_CLOSED, 1);

	g1_clock_advance_ms(2000);
	zassert_equal(is_in_box(), 1, "2.2 s of silence");
	g1_clock_advance_ms(4500);
	zassert_equal(is_in_box(), 0, "6.7 s of silence");
	zassert_equal(is_box_lid_closed(), 0);
}

ZTEST(nfc_events, test_interrupt_without_a_case_message_does_not_put_the_lens_in_the_case)
{
	test_nfc_thread_start();
	st25dv_emul_set_sys(E, ST25DV_EMUL_SYS_GPO1, 0xff);
	st25dv_emul_rf_field(E, true);
	trigger_nfc_event();
	k_sleep(K_MSEC(200));

	zassert_equal(is_in_box(), 0);
}
