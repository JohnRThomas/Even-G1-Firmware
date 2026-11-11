/* last_rx_time is 32-bit, so silence is measured modulo 2^32. */
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_bt.h"
#include "ble/phone_silence.h"

ZTEST_SUITE(link, NULL, NULL, NULL, NULL, NULL);

/* Bluetooth Core Vol 1 Part F 1.3 */
#define HCI_REMOTE_USER_TERMINATED 0x13

ZTEST(link, test_no_connection_returns_false_and_calls_nothing)
{
	struct GlassesState *gs = g1_state();

	gs->ble_context.last_rx_time = 1000;

	zassert_false(ble_drop_silent_phone(gs, NULL, 1000 + 600000));
	zassert_equal(bt_conn_disconnect_fake.call_count, 0);
	zassert_equal(gs->ble_context.last_rx_time, 1000);
}

ZTEST(link, test_ios_phone_is_never_dropped)
{
	struct GlassesState *gs = g1_state();

	gs->phone_is_ios = 1;
	gs->ble_context.last_rx_time = 1000;

	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 1000 + 30001));
	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 1000 + 3600000));
	zassert_equal(bt_conn_disconnect_fake.call_count, 0);
	zassert_equal(gs->ble_context.last_rx_time, 1000);
}

ZTEST(link, test_dfu_mode_work_mode_1_never_drops)
{
	struct GlassesState *gs = g1_state();

	gs->work_mode = 1;
	gs->ble_context.last_rx_time = 1000;

	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 1000 + 30001));
	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 1000 + 3600000));
	zassert_equal(bt_conn_disconnect_fake.call_count, 0);
	zassert_equal(gs->ble_context.last_rx_time, 1000);
}

/* only work_mode 1 (DFU) is exempt */
ZTEST(link, test_other_work_modes_still_drop_a_silent_android_phone)
{
	struct GlassesState *gs = g1_state();

	gs->work_mode = 4;
	gs->ble_context.last_rx_time = 1000;

	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, 1000 + 30001));
	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
}

ZTEST(link, test_android_phone_silent_30000_ms_is_kept)
{
	struct GlassesState *gs = g1_state();

	gs->ble_context.last_rx_time = 5000;

	/* the limit is exclusive */
	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 5000 + 30000));
	zassert_equal(bt_conn_disconnect_fake.call_count, 0);
	zassert_equal(gs->ble_context.last_rx_time, 5000);
}

ZTEST(link, test_android_phone_silent_30001_ms_is_disconnected_once)
{
	struct GlassesState *gs = g1_state();

	gs->ble_context.last_rx_time = 5000;

	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, 5000 + 30001));
	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
	zassert_equal_ptr(bt_conn_disconnect_fake.arg0_val, G1_FAKE_CONN);
	zassert_equal(bt_conn_disconnect_fake.arg1_val, HCI_REMOTE_USER_TERMINATED, "reason 0x%02x",
		      bt_conn_disconnect_fake.arg1_val);
	/* refreshed to now */
	zassert_equal(gs->ble_context.last_rx_time, 35001);
}

ZTEST(link, test_after_a_drop_the_next_30000_ms_do_not_drop_again)
{
	struct GlassesState *gs = g1_state();

	gs->ble_context.last_rx_time = 5000;
	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, 35001));

	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 36001));
	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, 35001 + 30000));
	zassert_equal(bt_conn_disconnect_fake.call_count, 1);

	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, 35001 + 30001));
	zassert_equal(bt_conn_disconnect_fake.call_count, 2);
	zassert_equal(gs->ble_context.last_rx_time, 65002);
}

/* last rx 4096 ms before the 2^32 ms wrap */
ZTEST(link, test_silence_is_measured_across_the_32_bit_uptime_wrap)
{
	struct GlassesState *gs = g1_state();
	const int64_t wrap = 0x100000000LL;

	gs->ble_context.last_rx_time = 0xfffff000u;

	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, wrap + 25904));
	zassert_equal(bt_conn_disconnect_fake.call_count, 0);

	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, wrap + 25905));
	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
	zassert_equal(gs->ble_context.last_rx_time, 25905);

	zassert_false(ble_drop_silent_phone(gs, G1_FAKE_CONN, wrap + 25905 + 30000));
	zassert_true(ble_drop_silent_phone(gs, G1_FAKE_CONN, wrap + 25905 + 30001));
	zassert_equal(bt_conn_disconnect_fake.call_count, 2);
}
