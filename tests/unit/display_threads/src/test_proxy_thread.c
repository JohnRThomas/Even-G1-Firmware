/*
 * Sync 6 subs: 7 power, 8 language, 9 startup mode, 10 dashboard toggle, 12 double tap, 13 long
 * press.
 */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_ipc.h"
#include "fakes_storage.h"
#include "fakes_ui.h"

#include "display/proxy_thread.c"

extern struct k_msgq display_msgq;

static void proxy_before(void *fixture)
{
	ARG_UNUSED(fixture);

	k_msgq_purge(&proxy_msgq);
	k_msgq_purge(&display_msgq);
	proxy_tid = NULL;
	memset(response_buf, 0, sizeof(response_buf));
	response_pending = 0;
	response_len = 0;
	audio_file_start = 0;
	audio_file_end = 0;
	audio_pkt_total = 0;
	audio_file_size = 0;
	audio_pkt_index = 0;
	audio_file_sent = 0;
	audio_block_idx = 0;
	audio_pkt_seq = 0;
	audio_sending = 0;
}

static void proxy_after(void *fixture)
{
	ARG_UNUSED(fixture);

	if (proxy_tid) {
		k_thread_abort(proxy_tid);
		proxy_tid = NULL;
	}
	k_msgq_purge(&proxy_msgq);
	k_msgq_purge(&display_msgq);
}

ZTEST_SUITE(proxy_thread, NULL, NULL, proxy_before, proxy_after, NULL);

static struct GlassesState *proxy_start(void)
{
	struct GlassesState *gs = g1_state();

	zassert_equal(start_proxy_thread_handler(gs), 0);
	g1_clock_advance_ms(1);
	return gs;
}

static void expect_sync(int i, const char *hex)
{
	uint8_t exp[8];
	int n = g1_hex_parse(hex, exp, sizeof(exp));
	char got[64];

	zassert_true(i < sync_to_slave_fake.call_count, "sync_to_slave call %d missing (%d calls)",
		     i, sync_to_slave_fake.call_count);
	zassert_equal_ptr(sync_to_slave_fake.arg0_history[i], g1_state());
	zassert_equal(sync_to_slave_fake.arg1_history[i], 6, "call %d: sync command %d", i,
		      sync_to_slave_fake.arg1_history[i]);
	zassert_equal(sync_to_slave_fake.arg3_history[i], n, "call %d: length %u", i,
		      (unsigned int)sync_to_slave_fake.arg3_history[i]);
	g1_hex_format(g1_sync_to_slave_log.data[i], g1_sync_to_slave_log.len[i], got, sizeof(got));
	zassert_mem_equal(g1_sync_to_slave_log.data[i], exp, n, "call %d: %s, expected %s", i, got,
			  hex);
}

/* ---- response queue ---- */

ZTEST(proxy_thread, test_slave_queues_no_response)
{
	const uint8_t resp[2] = {0x0a, 0xc9};

	g1_unit_set_role(G1_ROLE_SLAVE);

	zassert_equal(send_response_data_to_msgqueue(resp, sizeof(resp)), 0);
	zassert_equal(send_response_data_to_ble(), 0);
	zassert_equal(k_msgq_num_used_get(&proxy_msgq), 0);
}

ZTEST(proxy_thread, test_response_longer_than_20_bytes_is_rejected)
{
	uint8_t resp[21] = {0};

	zassert_equal(send_response_data_to_msgqueue(resp, sizeof(resp)), -1);
	zassert_equal(send_response_data_to_msgqueue(resp, 20), 0);
	zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);
}

ZTEST(proxy_thread, test_loaded_response_is_written_to_the_phone_on_send)
{
	const uint8_t resp[10] = {0x0a, 0x0a, 0x00, 0x05, 0xc9, 0x01, 0x02, 0x03, 0x04, 0x05};

	proxy_start();
	send_response_data_to_msgqueue(resp, sizeof(resp));
	g1_clock_advance_ms(1);
	expect_no_frame();

	send_response_data_to_ble();
	g1_clock_advance_ms(1);

	expect_frame("0a 0a 00 05 c9 01 02 03 04 05");
	expect_no_frame();
}

ZTEST(proxy_thread, test_send_without_a_loaded_response_writes_nothing)
{
	proxy_start();

	send_response_data_to_ble();
	g1_clock_advance_ms(1);

	expect_no_frame();
}

ZTEST(proxy_thread, test_a_loaded_response_is_written_once)
{
	const uint8_t resp[3] = {0x09, 0xc9, 0x07};

	proxy_start();
	send_response_data_to_msgqueue(resp, sizeof(resp));
	send_response_data_to_ble();
	send_response_data_to_ble();
	g1_clock_advance_ms(1);

	expect_frame("09 c9 07");
	expect_no_frame();
}

ZTEST(proxy_thread, test_the_last_loaded_response_is_the_one_sent)
{
	const uint8_t first[4] = {0x0a, 0x01, 0x02, 0x03};
	const uint8_t second[2] = {0x0a, 0xc9};

	proxy_start();
	send_response_data_to_msgqueue(first, sizeof(first));
	send_response_data_to_msgqueue(second, sizeof(second));
	send_response_data_to_ble();
	g1_clock_advance_ms(1);

	expect_frame("0a c9");
	expect_no_frame();
}

ZTEST(proxy_thread, test_failed_write_drops_the_response)
{
	const uint8_t resp[2] = {0x0a, 0xc9};

	proxy_start();
	g1_write_ret = -1;
	send_response_data_to_msgqueue(resp, sizeof(resp));
	send_response_data_to_ble();
	send_response_data_to_ble();
	g1_clock_advance_ms(1);

	expect_frame("0a c9");
	expect_no_frame();
}

ZTEST(proxy_thread, test_queue_holds_16_messages_and_rejects_the_17th)
{
	const uint8_t resp[1] = {0};

	for (int i = 0; i < 16; i++) {
		zassert_equal(send_response_data_to_ble(), 0, "message %d", i);
	}
	zassert_equal(send_response_data_to_ble(), -1);
	zassert_equal(send_response_data_to_msgqueue(resp, sizeof(resp)), -1);
	zassert_equal(SendPowerInfoToSlave(1), -1);
	zassert_equal(SendSystemLanguageInfoToSlave(1), -1);
	zassert_equal(SendDashBoardStartupModeInfoToSlave(1), -1);
	zassert_equal(SlaveDoubleClickEventInject(), -1);
	zassert_equal(SendDoubleTapCustomizeToSlave(1), -1);
	zassert_equal(SendLongpressTapCustomizeToSlave(1), -1);
	zassert_equal(sendAudioStreamFileToApp(0, 1), -1);
	zassert_equal(requestAudioInfoToApp(0), -1);
	zassert_equal(k_msgq_num_used_get(&proxy_msgq), 16);
}

/* ---- master -> slave ---- */

ZTEST(proxy_thread, test_power_info_syncs_sub_7_and_raises_the_power_event_locally)
{
	struct disp_msg m;

	proxy_start();
	zassert_equal(SendPowerInfoToSlave(1), 0);
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 1);
	expect_sync(0, "07 01");
	zassert_equal(k_msgq_get(&display_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.type, DISP_MSG_POWER_ON, "display message type %d", m.type);
}

ZTEST(proxy_thread, test_power_info_0_raises_the_power_resume_event)
{
	struct disp_msg m;

	proxy_start();
	SendPowerInfoToSlave(0);
	g1_clock_advance_ms(1);

	expect_sync(0, "07 00");
	zassert_equal(k_msgq_get(&display_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.type, DISP_MSG_POWER_OFF, "display message type %d", m.type);
}

ZTEST(proxy_thread, test_language_syncs_sub_8_and_stores_it)
{
	proxy_start();
	zassert_equal(SendSystemLanguageInfoToSlave(3), 0);
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 1);
	expect_sync(0, "08 03");
	zassert_equal(upgradeAppLanguageInfoToFlash_fake.call_count, 1);
	zassert_equal(upgradeAppLanguageInfoToFlash_fake.arg0_val, 3);
}

ZTEST(proxy_thread, test_startup_mode_syncs_sub_9_and_stores_it)
{
	proxy_start();
	zassert_equal(SendDashBoardStartupModeInfoToSlave(2), 0);
	g1_clock_advance_ms(1);

	expect_sync(0, "09 02");
	zassert_equal(upgradeDashboardStartupModeInfoToFlash_fake.call_count, 1);
	zassert_equal(upgradeDashboardStartupModeInfoToFlash_fake.arg0_val, 2);
}

ZTEST(proxy_thread, test_double_tap_action_syncs_sub_12_and_stores_it)
{
	proxy_start();
	zassert_equal(SendDoubleTapCustomizeToSlave(4), 0);
	g1_clock_advance_ms(1);

	expect_sync(0, "0c 04");
	zassert_equal(upgradeDoubleTapCustomizeToFlash_fake.call_count, 1);
	zassert_equal(upgradeDoubleTapCustomizeToFlash_fake.arg0_val, 4);
}

ZTEST(proxy_thread, test_long_press_action_syncs_sub_13_and_stores_it)
{
	proxy_start();
	zassert_equal(SendLongpressTapCustomizeToSlave(1), 0);
	g1_clock_advance_ms(1);

	expect_sync(0, "0d 01");
	zassert_equal(upgradeLongpressTapCustomizeToFlash_fake.call_count, 1);
	zassert_equal(upgradeLongpressTapCustomizeToFlash_fake.arg0_val, 1);
}

ZTEST(proxy_thread, test_double_click_with_dashboard_action_opens_the_dashboard)
{
	struct GlassesState *gs = proxy_start();

	gs->double_tap_customize = 4;
	gs->dashboard_mode = 0;
	zassert_equal(SlaveDoubleClickEventInject(), 0);
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 1);
	expect_sync(0, "0a 01");
	zassert_equal(gs->double_tap_dashboard_on, 1);
	zassert_true(atomic_get(&dashboard_flags) & BIT(1));
}

ZTEST(proxy_thread, test_double_click_on_the_open_dashboard_closes_it)
{
	struct GlassesState *gs = proxy_start();

	gs->double_tap_customize = 4;
	gs->dashboard_mode = 6;
	gs->double_tap_dashboard_on = 1;
	gs->dashboard_ts->dashboard_hold = 1;
	SlaveDoubleClickEventInject();
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 1);
	expect_sync(0, "0a 02");
	zassert_equal(gs->double_tap_dashboard_on, 0);
	zassert_equal(gs->dashboard_ts->dashboard_hold, 0);
	zassert_true(atomic_get(&dashboard_flags) & BIT(1));
}

ZTEST(proxy_thread, test_double_click_with_another_action_syncs_nothing)
{
	struct GlassesState *gs = proxy_start();

	gs->double_tap_customize = 1;
	gs->dashboard_mode = 0;
	SlaveDoubleClickEventInject();
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 0);
	zassert_equal(gs->double_tap_dashboard_on, 0);
	zassert_equal(atomic_get(&dashboard_flags), 0);
}

ZTEST(proxy_thread, test_sync_is_tried_three_times_and_nothing_stored_without_ack)
{
	proxy_start();
	sync_to_slave_fake.return_val = 5000;

	SendDoubleTapCustomizeToSlave(4);
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 3);
	expect_sync(0, "0c 04");
	expect_sync(2, "0c 04");
	zassert_equal(upgradeDoubleTapCustomizeToFlash_fake.call_count, 0);
}

ZTEST(proxy_thread, test_sync_acknowledged_on_the_second_try_stores_once)
{
	static int waits[] = {5000, 12};

	proxy_start();
	SET_RETURN_SEQ(sync_to_slave, waits, ARRAY_SIZE(waits));

	SendSystemLanguageInfoToSlave(5);
	g1_clock_advance_ms(1);

	zassert_equal(sync_to_slave_fake.call_count, 2);
	zassert_equal(upgradeAppLanguageInfoToFlash_fake.call_count, 1);
	zassert_equal(upgradeAppLanguageInfoToFlash_fake.arg0_val, 5);
}

ZTEST(proxy_thread, test_forwarding_waits_until_the_app_is_connected)
{
	struct GlassesState *gs = g1_state();

	gs->module_mask &= ~BIT(7);
	proxy_start();
	SendSystemLanguageInfoToSlave(3);
	g1_clock_advance_ms(1500);

	zassert_equal(sync_to_slave_fake.call_count, 0);
	zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);

	gs->module_mask |= BIT(7);
	g1_clock_advance_ms(1100);
	zassert_equal(sync_to_slave_fake.call_count, 1);
}

ZTEST(proxy_thread, test_forwarding_waits_while_in_dfu_mode)
{
	struct GlassesState *gs = g1_state();

	gs->work_mode = 1;
	proxy_start();
	SendSystemLanguageInfoToSlave(3);
	g1_clock_advance_ms(4000);

	zassert_equal(sync_to_slave_fake.call_count, 0);
	zassert_equal(k_msgq_num_used_get(&proxy_msgq), 1);
}

/* ---- QuickNote audio ---- */

#define VOICE_MGR_ADDR 0x400000
#define VOICE_BASE     0x421000

/* magic 5AA5AA5Ah, reserved, 4 x {state, start, end, timestamp, crc32} LE; state 2 = recorded */
static void put_le32(uint32_t addr, uint32_t v)
{
	g1_fake_flash_image[addr] = v;
	g1_fake_flash_image[addr + 1] = v >> 8;
	g1_fake_flash_image[addr + 2] = v >> 16;
	g1_fake_flash_image[addr + 3] = v >> 24;
}

static void put_block(int i, uint32_t state, uint32_t start, uint32_t end, uint32_t ts,
		      uint32_t crc)
{
	uint32_t a = VOICE_MGR_ADDR + 8 + i * 20;

	put_le32(a, state);
	put_le32(a + 4, start);
	put_le32(a + 8, end);
	put_le32(a + 12, ts);
	put_le32(a + 16, crc);
}

static void put_manager(void)
{
	put_le32(VOICE_MGR_ADDR, 0x5aa5aa5a);
	put_le32(VOICE_MGR_ADDR + 4, 0);
	for (int i = 0; i < 4; i++) {
		put_block(i, 0, 0, 0, 0, 0);
	}
}

/* entries {block, timestamp LE32, crc32 LE32} */
ZTEST(proxy_thread, test_audio_info_lists_the_recorded_blocks)
{
	put_manager();
	put_block(0, 2, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);
	put_block(2, 2, VOICE_BASE + 0x40000, VOICE_BASE + 0x40010, 0x11223344, 0x55667788);

	proxy_start();
	zassert_equal(requestAudioInfoToApp(0), 0);
	g1_clock_advance_ms(1);

	expect_frame("1e 18 00 00 01 02"
		     " 01 04 03 02 01 dd cc bb aa"
		     " 03 44 33 22 11 88 77 66 55");
	expect_no_frame();
}

/* chunk header: 1e len 00 seq 02 total LE16 index LE16 block */
ZTEST(proxy_thread, test_audio_file_is_streamed_in_190_byte_chunks)
{
	uint8_t exp[200];
	size_t len;
	const uint8_t *f;

	put_manager();
	put_block(0, 2, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);
	for (int i = 0; i < 300; i++) {
		g1_fake_flash_image[VOICE_BASE + i] = i * 7;
	}

	proxy_start();
	zassert_equal(sendAudioStreamFileToApp(0, 1), 0);
	g1_clock_advance_ms(1);

	zassert_equal(g1_frame_count(), 2, "%d frames", g1_frame_count());

	f = g1_frame_peek(0, &len);
	zassert_equal(len, 200);
	zassert_equal(g1_hex_parse("1e c8 00 00 02 02 00 01 00 01", exp, sizeof(exp)), 10);
	for (int i = 0; i < 190; i++) {
		exp[10 + i] = i * 7;
	}
	zassert_mem_equal(f, exp, 200);

	f = g1_frame_peek(1, &len);
	zassert_equal(len, 120);
	zassert_equal(g1_hex_parse("1e 78 00 01 02 02 00 02 00 01", exp, sizeof(exp)), 10);
	for (int i = 0; i < 110; i++) {
		exp[10 + i] = (190 + i) * 7;
	}
	zassert_mem_equal(f, exp, 120);

	zassert_true(clearAudioBlockInUse_fake.call_count >= 1);
}

/* DEFECT: setAudioBlockInUse() takes a 1-based block; the 0-based index is passed. */
ZTEST_EXPECT_FAIL(proxy_thread, test_sending_block_1_marks_block_1_in_use);
ZTEST(proxy_thread, test_sending_block_1_marks_block_1_in_use)
{
	put_manager();
	put_block(0, 2, VOICE_BASE, VOICE_BASE + 300, 0x01020304, 0xaabbccdd);

	proxy_start();
	sendAudioStreamFileToApp(0, 1);
	g1_clock_advance_ms(1);

	zassert_true(setAudioBlockInUse_fake.call_count >= 1);
	zassert_equal(setAudioBlockInUse_fake.arg0_history[0], 1, "marked %ld",
		      (long)setAudioBlockInUse_fake.arg0_history[0]);
}
