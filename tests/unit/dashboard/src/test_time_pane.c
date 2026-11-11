/*
 * Dashboard Set sub 1 (time and weather) and sub 6 (pane mode). The sub 1 stamps are local time
 * encoded as UTC.
 */
#include "dash_test.h"
#include "fakes_esb.h"
#include "fakes_misc.h"
#include "fakes_storage.h"
#include "utils/burial_point.h"

ZTEST_SUITE(dashboard, NULL, NULL, NULL, NULL, NULL);

static uint8_t pkt[256];

static size_t load(const char *name)
{
	int n = g1_vector_load(name, pkt, sizeof(pkt));

	zassert_true(n > 0, "vector %s: %d", name, n);
	return n;
}

/* ---- sub 1: time and weather ---- */

ZTEST(dashboard, test_0x06_time_weather_replies_the_6_byte_echo_with_status_0)
{
	g1_send_vector("0x06_time_weather_sunny");

	expect_frame("06 16 00 09 01 00");
	expect_no_frame();
}

ZTEST(dashboard, test_0x06_time_weather_stores_icon_temperature_both_stamps_and_formats)
{
	g1_send_vector("0x06_time_weather_sunny");

	zassert_equal(dts()->ts, 1762603160u);
	zassert_equal(dts()->ms_ts, 1762603160491ull);
	zassert_equal(dts()->weather_icon, 0x10);
	zassert_equal(dts()->temperature, 0x14);
	zassert_equal(dts()->temp_unit, 1);
	zassert_equal(dts()->time_disp_mode, 1);
	zassert_equal(dts()->date_format, 0);
}

ZTEST(dashboard, test_0x06_time_without_weather_stores_ff_ff_and_the_stamps)
{
	g1_send_vector("0x06_time_weather");

	expect_frame("06 16 00 09 01 00");
	zassert_equal(dts()->weather_icon, 0xff);
	zassert_equal(dts()->temperature, 0xff);
	zassert_equal(dts()->ts, 1762981198u);
	zassert_equal(dts()->ms_ts, 1762981198349ull);
}

ZTEST(dashboard, test_0x06_time_sets_the_wall_clock_from_the_millisecond_stamp)
{
	int64_t ms;

	g1_clock_advance_ms(4000); /* catches a boot-relative clock */
	g1_send_vector("0x06_time_weather_sunny");

	get_wall_clock_ms(&ms);
	zassert_equal(ms, 1762603160491ll, "wall clock %lld", ms);
	g1_clock_advance_ms(1500);
	get_wall_clock_ms(&ms);
	zassert_equal(ms, 1762603160491ll + 1500, "wall clock %lld", ms);
}

/* 2.5 s after arrival: 2 whole seconds, not the time since boot */
ZTEST(dashboard, test_0x06_time_restarts_the_dashboard_clock_reference)
{
	g1_clock_advance_ms(7000);
	g1_send_vector("0x06_time_weather_sunny");
	g1_clock_advance_ms(2500);

	accumulate_elapsed_seconds((uint32_t *)(void *)dts(), 0, 0); /* ts is at offset 0 */
	zassert_equal(dts()->ts, 1762603160u + 2, "ts %u", dts()->ts);
}

ZTEST(dashboard, test_0x06_time_echoes_the_sequence_byte)
{
	size_t n = load("0x06_time_weather_sunny");

	pkt[3] = 0xa5;
	g1_send(pkt, n);

	expect_frame("06 16 00 a5 01 00");
}

ZTEST(dashboard, test_0x06_time_length_field_mismatch_replies_status_1_and_stores_nothing)
{
	size_t n = load("0x06_time_weather_sunny");
	int64_t ms;

	pkt[1] = 0x17;
	g1_send(pkt, n);

	expect_frame_padded("06 17 00 09 01 01", 20);
	expect_no_frame();
	zassert_equal(dts()->ts, 0);
	zassert_equal(dts()->ms_ts, 0);
	zassert_equal(dts()->weather_icon, 0);
	get_wall_clock_ms(&ms);
	zassert_equal(ms, 0, "wall clock set to %lld", ms);
}

/* byte 21 is date_format */
ZTEST(dashboard, test_0x06_time_changed_date_format_is_stored_and_committed)
{
	size_t n = load("0x06_time_weather_sunny");
	struct GlassesState *gs = g1_state();

	gs->dashboard_cmd = 0x55;
	pkt[21] = 1;
	g1_send(pkt, n);

	zassert_equal(dts()->date_format, 1);
	zassert_equal(gs->dashboard_cmd, 0);
	zassert_equal(k_sem_count_get(&gs->dashboard_position_sem), 1);
}

ZTEST(dashboard, test_0x06_time_unchanged_date_format_requests_no_commit)
{
	g1_send_vector("0x06_time_weather_sunny");

	zassert_equal(dts()->date_format, 0);
	zassert_equal(k_sem_count_get(&g1_state()->dashboard_position_sem), 0);
}

ZTEST(dashboard, test_0x06_time_21_byte_form_keeps_the_stored_date_format)
{
	size_t n = load("0x06_time_weather_sunny");

	dts()->date_format = 2; /* the rx buffer byte behind it would read 0 */
	pkt[1] = 0x15;
	g1_send(pkt, n - 1);

	expect_frame("06 15 00 09 01 00");
	zassert_equal(dts()->date_format, 2);
	zassert_equal(dts()->ts, 1762603160u);
}

ZTEST(dashboard, test_0x06_time_is_saved_to_retained_memory)
{
	g1_send_vector("0x06_time_weather_sunny");

	zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, 1);
	zassert_equal(set_ncs_mem_initialized_fake.call_count, 1);
}

/* ---- sub 6: pane mode ---- */

/* Request 06 07 00 <seq> 06 <mode> <pane>; reply: the first 5 bytes echoed, then status 00. */
ZTEST(dashboard, test_0x06_pane_mode_stores_mode_and_pane_and_echoes)
{
	static const struct {
		uint8_t seq;
		uint8_t mode;
		uint8_t pane;
	} rows[] = {
		{0x08, 0x01, 0x01},
		{0x08, 0x02, 0x01},
		{0x09, 0x01, 0x03},
		/* pane ids 5 and above are stored as sent */
		{0x08, 0x01, 0x05},
		{0x08, 0x01, 0x09},
	};
	size_t n = load("0x06_pane_mode");
	static char msg[G1_MSG_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
		const uint8_t want[] = {0x06, 0x07, 0x00, rows[i].seq, 0x06, 0x00};
		char hex[32];

		pkt[3] = rows[i].seq;
		pkt[5] = rows[i].mode;
		pkt[6] = rows[i].pane;
		g1_send(pkt, n);

		g1_hex_format(want, sizeof(want), hex, sizeof(hex));
		zassert_true(g1_frame_check(hex, -1, msg, sizeof(msg)),
			     "seq 0x%02x mode %u pane %u: %s", rows[i].seq, rows[i].mode,
			     rows[i].pane, msg);
		zassert_equal(dts()->disp_mode, rows[i].mode, "seq 0x%02x mode %u pane %u: mode %u",
			      rows[i].seq, rows[i].mode, rows[i].pane, dts()->disp_mode);
		zassert_equal(dts()->area, rows[i].pane, "seq 0x%02x mode %u pane %u: pane %u",
			      rows[i].seq, rows[i].mode, rows[i].pane, dts()->area);
		zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, i + 1,
			      "seq 0x%02x mode %u pane %u: %u saves", rows[i].seq, rows[i].mode,
			      rows[i].pane, save_dashboard_ts_to_ncs_mem_fake.call_count);
	}
	expect_no_frame();
}

/* the app sends it to each lens itself */
ZTEST(dashboard, test_0x06_pane_mode_is_not_forwarded_to_the_slave)
{
	g1_send_vector("0x06_pane_mode");

	zassert_equal(SendDashBoardStartupModeInfoToSlave_fake.call_count, 0);
	zassert_equal(send_response_data_to_msgqueue_fake.call_count, 0);
	zassert_equal(g1_fake_ipc_send_fake.call_count, 0);
}

ZTEST(dashboard, test_0x06_pane_mode_on_the_slave_is_stored_and_echoed)
{
	g1_unit_set_role(G1_ROLE_SLAVE);

	g1_send_vector("0x06_pane_mode");

	expect_frame("06 07 00 08 06 00");
	zassert_equal(dts()->disp_mode, 1);
	zassert_equal(dts()->area, 1);
}

/* Defect: mode 03 is stored unchecked and acknowledged with 00. */
ZTEST_EXPECT_FAIL(dashboard, test_0x06_pane_mode_invalid_mode_id_is_rejected);
ZTEST(dashboard, test_0x06_pane_mode_invalid_mode_id_is_rejected)
{
	size_t n = load("0x06_pane_mode");

	g1_send(pkt, n);
	expect_frame("06 07 00 08 06 00");

	pkt[3] = 0x0a;
	pkt[5] = 0x03;
	g1_send(pkt, n);

	zassert_equal(dts()->disp_mode, 1, "mode 3 stored");
	expect_frame("06 07 00 0a 06 01");
}

ZTEST(dashboard, test_0x06_pane_mode_length_mismatch_replies_status_1_and_stores_nothing)
{
	size_t n = load("0x06_pane_mode");

	pkt[1] = 0x08;
	g1_send(pkt, n);

	expect_frame_padded("06 08 00 08 06 01", 20);
	zassert_equal(dts()->disp_mode, 0);
	zassert_equal(dts()->area, 0);
	zassert_equal(save_dashboard_ts_to_ncs_mem_fake.call_count, 0);
}
