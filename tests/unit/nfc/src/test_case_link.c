/* 0x13 request: 13 01 02 <n> [<case level | 80h charging> [<lid: 06 closed, 07 open>]] <sum>. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fake_npm1300.h"
#include "fakes_nfc.h"
#include "nfc_test.h"

#include "production_test/pt_data.c"
#include "production_test/production_test.c"

#define E TAG_E

void nfc_pt_test_reset(void)
{
	pt_comm.nfc = NULL;
	pt_comm.uart = (const struct device *)1;
	pt_comm.type = PT_COMM_NFC;
	memset(pt_resp_buf, 0, sizeof(pt_resp_buf));
	memset(pt_req_buf, 0, sizeof(pt_req_buf));
	pt_test_mode = 0;
	pt_level_flags.raw = 0;
	aging_mode = 0;
	aging_phase = 0;
	battery_full_flag = false;
	battery_high_v_count = 0;
	battery_full_count = 0;
	battery_info_last_ms = 0;
	charger_enable_ok = true;
	charger_retry_last_ms = 0;
	charger_retry_fail_count = 0;
	memset(pt_uart_frame, 0, sizeof(pt_uart_frame));
}

static void case_before(void *fixture)
{
	ARG_UNUSED(fixture);
	nfc_wlc_test_reset();
	nfc_pt_test_reset();
	init_box_status();
	st25dv_emul_enable_mailbox(E);
	st25dv_emul_set_write_time_ms(E, 1);

	g1_state()->battery_left = 50;
	g1_state()->battery_current_ma = 100;
	g1_state()->battery_temp_x100 = 2500;
	nfc_field_mv = 5500;
}

ZTEST_SUITE(nfc_case, NULL, NULL, case_before, NULL, NULL);

static void case_send(const uint8_t *frame, size_t n)
{
	uint8_t msg[64];

	memcpy(msg, frame, n);
	msg[n] = nfc_test_sum8(frame, n);
	zassert_ok(st25dv_emul_rf_put_msg(E, msg, n + 1));
}

static const uint8_t query[] = {0x13, 0x01, 0x02, 0x00};

static void expect_reply(const uint8_t *got, int n, const uint8_t *exp, size_t en)
{
	char g[3 * 32 + 4];
	char e[3 * 32 + 4];

	zassert_true(n > 0, "no reply (%d)", n);
	g1_hex_format(got, n, g, sizeof(g));
	g1_hex_format(exp, en, e, sizeof(e));
	zassert_true((size_t)n == en && memcmp(got, exp, en) == 0, "\n  got  %s\n  want %s", g,
		     e);
}

ZTEST(nfc_case, test_case_message_is_consumed_and_the_reply_waits_for_the_case)
{
	uint8_t ctrl;

	case_send(query, sizeof(query));
	zassert_equal(init_nfc_comm(), 0);

	ctrl = st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_CTRL);
	zassert_equal(ctrl & ST25DV_EMUL_RF_PUT_MSG, 0, "case message consumed");
	zassert_equal(ctrl & ST25DV_EMUL_HOST_PUT_MSG, ST25DV_EMUL_HOST_PUT_MSG);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_MB_LEN), 10, "11-byte reply");
	zassert_equal(pt_comm_busy_set_fake.call_count, 1, "touch polling paused");
	zassert_equal(pt_comm_busy_clear_fake.call_count, 1);
}

ZTEST(nfc_case, test_charging_flag_follows_the_reply_status)
{
	uint8_t reply[256];

	case_send(query, sizeof(query));
	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
	zassert_equal(reply[4], 0x00);
	zassert_equal(is_box_charging(), 1, "status 0: charging");

	g1_state()->battery_temp_x100 = 6000; /* status 2: too hot */
	case_send(query, sizeof(query));
	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
	zassert_equal(reply[4], 0x02);
	zassert_equal(is_box_charging(), 0);

	g1_state()->battery_temp_x100 = 2500;
	g1_state()->battery_left = 100; /* status 1: full */
	case_send(query, sizeof(query));
	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
	zassert_equal(reply[4], 0x01);
	zassert_equal(is_box_charging(), 0);
}

ZTEST(nfc_case, test_case_level_and_lid_closed_are_taken_from_the_request)
{
	static const uint8_t closed[] = {0x13, 0x01, 0x02, 0x02, 0x80 | 40, 0x06};

	case_send(closed, sizeof(closed));
	zassert_equal(init_nfc_comm(), 0);

	zassert_equal(is_box_lid_closed(), 1);
	zassert_equal(can_enter_low_power, 1, "closed case: low power allowed");
	zassert_equal(get_pt_level(), 40, "F5 0F payload");
	zassert_equal(get_pt_level_flag(), 1, "F5 0E payload");
	zassert_equal(set_ncs_mem_pt_level_fake.arg0_val, 40);
}

ZTEST(nfc_case, test_case_lid_open_clears_the_lid_flag)
{
	static const uint8_t closed[] = {0x13, 0x01, 0x02, 0x02, 60, 0x06};
	static const uint8_t open[] = {0x13, 0x01, 0x02, 0x02, 60, 0x07};
	uint8_t reply[256];

	case_send(closed, sizeof(closed));
	zassert_equal(init_nfc_comm(), 0);
	zassert_true(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)) > 0);
	zassert_equal(is_box_lid_closed(), 1);

	case_send(open, sizeof(open));
	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(is_box_lid_closed(), 0);
	zassert_equal(can_enter_low_power, 0);
	zassert_equal(get_pt_level_flag(), 0);
}

ZTEST(nfc_case, test_request_sum_is_not_read_as_a_lid_code)
{
	static const uint8_t closed[] = {0x13, 0x01, 0x02, 0x02, 60, 0x06};
	/* sum byte 07 = the lid-open code */
	static const uint8_t level_only[] = {0x13, 0x01, 0x02, 0x01, 0xf0};
	uint8_t reply[256];

	case_send(closed, sizeof(closed));
	zassert_equal(init_nfc_comm(), 0);
	zassert_true(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)) > 0);

	case_send(level_only, sizeof(level_only));
	zassert_equal(st25dv_emul_mailbox(E)[5], 0x07);
	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(is_box_lid_closed(), 1);
}

/* 4th high-field reply: byte 9 = 80h, resent 1 s later */
static uint8_t timer_reply[256];
static int timer_reply_len;

static void case_reads(struct k_timer *t)
{
	ARG_UNUSED(t);
	timer_reply_len = st25dv_emul_rf_get_msg(E, timer_reply, sizeof(timer_reply));
}

K_TIMER_DEFINE(case_timer, case_reads, NULL);

static void three_high_field_polls(void)
{
	uint8_t reply[256];

	nfc_field_mv = 6000;
	g1_clock_advance_ms(10001);
	for (int i = 0; i < 3; i++) {
		case_send(query, sizeof(query));
		zassert_equal(init_nfc_comm(), 0);
		zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
		zassert_equal(reply[9], 0x64, "poll %d not flagged", i + 1);
	}
}

ZTEST(nfc_case, test_flagged_reply_is_sent_again_when_the_case_has_read_it)
{
	static const uint8_t flagged[] = {0x13, 0x01, 0x02, 0x06, 0x00, 0x17, 0x70,
					  0x32, 0x00, 0x80, 0x55};
	uint8_t again[256];
	int n;

	three_high_field_polls();
	timer_reply_len = 0;
	case_send(query, sizeof(query));
	k_timer_start(&case_timer, K_MSEC(500), K_NO_WAIT);

	zassert_equal(init_nfc_comm(), 0);
	expect_reply(timer_reply, timer_reply_len, flagged, sizeof(flagged));
	n = st25dv_emul_rf_get_msg(E, again, sizeof(again));
	zassert_equal(n, 11, "second reply in the mailbox");
	zassert_mem_equal(again, flagged, 9);
	zassert_equal(again[9], 0x00);
}

ZTEST(nfc_case, test_flagged_reply_stays_when_the_case_has_not_read_it)
{
	uint8_t reply[256];

	three_high_field_polls();
	case_send(query, sizeof(query));

	zassert_equal(init_nfc_comm(), 0);
	zassert_equal(st25dv_emul_rf_get_msg(E, reply, sizeof(reply)), 11);
	zassert_equal(reply[9], 0x80, "the resend found the mailbox busy");
}

ZTEST(nfc_case, test_link_without_a_tag_fails)
{
	global_st25dv_context_ptr = NULL;

	zassert_equal(init_nfc_comm(), -1);
}

/* BCHGCHARGESTATUS: b0 battery detected, b3 constant current */
#define STAT_IDLE 0x01
#define STAT_CC   0x09

static const uint8_t *stat_seq;
static int stat_n;
static int stat_i;

static int read_stat(const struct device *dev, uint8_t base, uint8_t off, uint8_t *v)
{
	ARG_UNUSED(dev);
	zassert_equal(base, 0x03);
	zassert_equal(off, 0x34);
	*v = stat_seq[MIN(stat_i, stat_n - 1)];
	stat_i++;
	return 0;
}

static void charger_status(const uint8_t *seq, int n)
{
	stat_seq = seq;
	stat_n = n;
	stat_i = 0;
	mfd_npm1300_reg_read_fake.custom_fake = read_stat;
	RESET_FAKE(mfd_npm1300_reg_write);
}

ZTEST(nfc_case, test_charger_retry_leaves_a_charging_charger_alone)
{
	static const uint8_t seq[] = {STAT_CC};

	charger_status(seq, ARRAY_SIZE(seq));
	enable_charger_retry();
	zassert_equal(stat_i, 1);
	zassert_equal(mfd_npm1300_reg_write_fake.call_count, 0);
}

ZTEST(nfc_case, test_charger_retry_enables_the_charger_until_it_charges)
{
	static const uint8_t seq[] = {STAT_IDLE, STAT_IDLE, STAT_CC};
	int64_t t0 = g1_clock_uptime_ms();
	int64_t dt;

	charger_status(seq, ARRAY_SIZE(seq));
	enable_charger_retry();
	dt = g1_clock_uptime_ms() - t0;

	zassert_equal(stat_i, 3);
	zassert_equal(mfd_npm1300_reg_write_fake.call_count, 2);
	zassert_equal(mfd_npm1300_reg_write_fake.arg2_history[0], 0x04, "BCHGENABLESET");
	zassert_equal(mfd_npm1300_reg_write_fake.arg3_history[0], 0x01);
	zassert_true(dt >= 40 && dt <= 46, "20 ms after each enable: %lld ms", dt);
	zassert_true(charger_enable_ok);
}

ZTEST(nfc_case, test_charger_retry_gives_up_after_10_attempts)
{
	static const uint8_t seq[] = {STAT_IDLE};
	int64_t t0 = g1_clock_uptime_ms();
	int64_t dt;

	charger_status(seq, ARRAY_SIZE(seq));
	enable_charger_retry();
	dt = g1_clock_uptime_ms() - t0;

	zassert_equal(stat_i, 10);
	zassert_equal(mfd_npm1300_reg_write_fake.call_count, 10);
	zassert_true(dt >= 200 && dt <= 215, "%lld ms", dt);
}

ZTEST(nfc_case, test_charger_failing_four_rounds_a_second_apart_is_marked_failed)
{
	static const uint8_t idle[] = {STAT_IDLE};
	static const uint8_t cc[] = {STAT_CC};

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
	charger_status(cc, ARRAY_SIZE(cc));
	enable_charger_retry();
	zassert_true(charger_enable_ok);
}
