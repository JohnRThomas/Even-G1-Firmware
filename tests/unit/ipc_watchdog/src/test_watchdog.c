#include <errno.h>
#include <string.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "devices.h"
#include "fake_npm1300.h"

#include "threads/threads.h"

#define WDT DEVICE_DT_GET(DT_NODELABEL(pmic_wdt))

/* nPM1300 datasheet, TIMER block */
#define TIME_BASE        0x07
#define TIME_START       0x00
#define TIME_STOP        0x01
#define TIME_KICK        0x04
#define TIME_MODE        0x05
#define MODE_WDT_RESET   0x02

/* ---- PMIC access log ---- */

#define OPS_MAX 64
#define OP_TIMER 0xff /* pseudo base for set_timer */

struct pmic_op {
	uint8_t base;
	uint8_t off;
	uint32_t val; /* or the timer period in ms */
};

static struct pmic_op ops[OPS_MAX];
static int n_ops;

/* next fail_n writes to fail_off fail (< 0: all) */
static int fail_off;
static int fail_n;
static int fail_ret;
static int timer_fail_n;
static int timer_fail_ret;

static void op_add(uint8_t base, uint8_t off, uint32_t val)
{
	zassert_true(n_ops < OPS_MAX, "PMIC log full");
	ops[n_ops++] = (struct pmic_op){base, off, val};
}

static int reg_write_rec(const struct device *dev, uint8_t base, uint8_t off, uint8_t val)
{
	zassert_equal_ptr(dev, G1_NPM1300_DEV, "the watchdog talks to its parent PMIC");
	op_add(base, off, val);
	if (base == TIME_BASE && off == fail_off && fail_n != 0) {
		if (fail_n > 0) {
			fail_n--;
		}
		return fail_ret;
	}
	return 0;
}

static int set_timer_rec(const struct device *dev, uint32_t ms)
{
	zassert_equal_ptr(dev, G1_NPM1300_DEV, NULL);
	op_add(OP_TIMER, 0, ms);
	if (timer_fail_n != 0) {
		if (timer_fail_n > 0) {
			timer_fail_n--;
		}
		return timer_fail_ret;
	}
	return 0;
}

static void ops_clear(void)
{
	n_ops = 0;
}

static void fail_writes(uint8_t off, int n, int ret)
{
	fail_off = off;
	fail_n = n;
	fail_ret = ret;
}

static void expect_op(int i, uint8_t base, uint8_t off, uint32_t val)
{
	zassert_true(i < n_ops, "only %d PMIC accesses, wanted #%d", n_ops, i);
	zassert_true(ops[i].base == base && ops[i].off == off && ops[i].val == val,
		     "PMIC access %d: got %02x/%02x = %u, want %02x/%02x = %u", i, ops[i].base,
		     ops[i].off, ops[i].val, base, off, val);
}

static int count_ops(uint8_t base, uint8_t off)
{
	int n = 0;

	for (int i = 0; i < n_ops; i++) {
		n += ops[i].base == base && ops[i].off == off;
	}
	return n;
}

static void expect_armed(int first, uint32_t ms)
{
	expect_op(first, TIME_BASE, TIME_STOP, 1);
	expect_op(first + 1, OP_TIMER, 0, ms);
	expect_op(first + 2, TIME_BASE, TIME_MODE, MODE_WDT_RESET);
	expect_op(first + 3, TIME_BASE, TIME_START, 1);
}

static void watchdog_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* the driver keeps its timeout across tests */
	(void)wdt_disable(WDT);
	wdt_0_channel_id = 0;

	RESET_FAKE(mfd_npm1300_reg_write);
	RESET_FAKE(mfd_npm1300_set_timer);
	mfd_npm1300_reg_write_fake.custom_fake = reg_write_rec;
	mfd_npm1300_set_timer_fake.custom_fake = set_timer_rec;
	ops_clear();
	fail_off = -1;
	fail_n = 0;
	timer_fail_n = 0;
}

ZTEST_SUITE(watchdog, NULL, NULL, watchdog_before, NULL, NULL);

ZTEST(watchdog, test_init_arms_a_30s_reset_watchdog_and_feeds_it)
{
	zassert_equal(init_watchdog(), 0);

	zassert_equal(n_ops, 5, "%d PMIC accesses", n_ops);
	expect_armed(0, 30000);
	expect_op(4, TIME_BASE, TIME_KICK, 1);
	zassert_equal(wdt_0_channel_id, 0, "the nPM1300 has one channel, 0");
}

ZTEST(watchdog, test_init_on_a_pmic_that_is_not_ready_touches_nothing)
{
	g1_device_set_ready(WDT, false);

	zassert_equal(init_watchdog(), 0, "the caller does not check the result");
	zassert_equal(n_ops, 0);
}

ZTEST(watchdog, test_init_retries_the_install_when_the_driver_rejects_callbacks)
{
	timer_fail_n = 1;
	timer_fail_ret = -ENOTSUP;

	zassert_equal(init_watchdog(), 0);
	zassert_equal(count_ops(OP_TIMER, 0), 2, "installed twice");
	expect_op(1, OP_TIMER, 0, 30000);
	expect_op(2, OP_TIMER, 0, 30000);
	expect_op(3, TIME_BASE, TIME_MODE, MODE_WDT_RESET);
	expect_op(4, TIME_BASE, TIME_START, 1);
	zassert_equal(wdt_0_channel_id, 0);
}

ZTEST(watchdog, test_init_install_error_starts_nothing_and_leaves_no_channel)
{
	timer_fail_n = -1;
	timer_fail_ret = -EIO;

	zassert_equal(init_watchdog(), 0);
	zassert_equal(count_ops(OP_TIMER, 0), 1, "-EIO is not retried");
	zassert_equal(count_ops(TIME_BASE, TIME_START), 0, "not started");
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0, "not fed");
	zassert_true(wdt_0_channel_id < 0, "channel %d", wdt_0_channel_id);

	ops_clear();
	zassert_equal(watchdog_disable(), 0);
	zassert_equal(n_ops, 0);
}

ZTEST(watchdog, test_init_setup_error_does_not_feed)
{
	fail_writes(TIME_START, -1, -EIO);

	zassert_equal(init_watchdog(), 0);
	zassert_equal(count_ops(TIME_BASE, TIME_START), 1);
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0);
}

ZTEST(watchdog, test_init_feed_is_retried_every_50ms_until_it_succeeds)
{
	int64_t t0 = g1_clock_uptime_ms();
	int64_t dt;

	fail_writes(TIME_KICK, 2, -EIO);

	zassert_equal(init_watchdog(), 0);
	dt = g1_clock_uptime_ms() - t0;
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 3);
	/* k_sleep(50) lasts 50..51 ms on 1 ms ticks */
	zassert_true(dt >= 100 && dt <= 104, "%lld ms", dt);
}

ZTEST(watchdog, test_feed_kicks_the_pmic_watchdog_once)
{
	init_watchdog();
	ops_clear();

	zassert_equal(watchdog_feed(), 0);
	zassert_equal(n_ops, 1);
	expect_op(0, TIME_BASE, TIME_KICK, 1);
}

ZTEST(watchdog, test_feed_gives_up_after_5_attempts_and_returns_the_error)
{
	int64_t t0;
	int64_t dt;

	init_watchdog();
	ops_clear();
	fail_writes(TIME_KICK, -1, -EIO);
	t0 = g1_clock_uptime_ms();

	zassert_equal(watchdog_feed(), -EIO);
	dt = g1_clock_uptime_ms() - t0;
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 5);
	/* 4 to 5 sleeps of 50..51 ms */
	zassert_true(dt >= 200 && dt <= 260, "%lld ms", dt);
}

ZTEST(watchdog, test_disable_stops_the_pmic_timer_and_forgets_the_channel)
{
	init_watchdog();
	ops_clear();

	zassert_equal(watchdog_disable(), 0);
	zassert_equal(n_ops, 1);
	expect_op(0, TIME_BASE, TIME_STOP, 1);
	zassert_equal(wdt_0_channel_id, -EBUSY);

	ops_clear();
	zassert_equal(watchdog_disable(), 0);
	zassert_equal(n_ops, 0);
}

ZTEST(watchdog, test_feed_after_disable_does_not_kick_the_pmic)
{
	init_watchdog();
	watchdog_disable();
	ops_clear();

	zassert_not_equal(watchdog_feed(), 0);
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0);
	zassert_equal(count_ops(TIME_BASE, TIME_START), 0);
}

ZTEST(watchdog, test_disable_error_after_5_attempts_keeps_the_channel)
{
	init_watchdog();
	ops_clear();
	fail_writes(TIME_STOP, -1, -EIO);

	zassert_equal(watchdog_disable(), -EIO);
	zassert_equal(count_ops(TIME_BASE, TIME_STOP), 5);
	zassert_equal(wdt_0_channel_id, 0, "still armed: the channel stays valid");
}

ZTEST(watchdog, test_wait_reset_rearms_100ms_reset_without_feeding)
{
	init_watchdog();
	ops_clear();

	zassert_equal(config_watchdog_wait_reset(), 0);
	zassert_equal(n_ops, 4);
	expect_armed(0, 100);
	zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0, "the point is that nobody feeds it");
	zassert_equal(wdt_0_channel_id, 0);
}

ZTEST(watchdog, test_wait_reset_on_a_pmic_that_is_not_ready_returns_minus_1)
{
	g1_device_set_ready(WDT, false);

	zassert_equal(config_watchdog_wait_reset(), -1);
	zassert_equal(n_ops, 0);
}

ZTEST(watchdog, test_wait_reset_disable_error_returns_minus_2)
{
	fail_writes(TIME_STOP, -1, -EIO);

	zassert_equal(config_watchdog_wait_reset(), -2);
	zassert_equal(count_ops(OP_TIMER, 0), 0, "not re-armed");
}

ZTEST(watchdog, test_wait_reset_install_error_returns_minus_3)
{
	timer_fail_n = -1;
	timer_fail_ret = -EIO;

	zassert_equal(config_watchdog_wait_reset(), -3);
	zassert_true(wdt_0_channel_id < 0);
	zassert_equal(count_ops(TIME_BASE, TIME_START), 0);
}

ZTEST(watchdog, test_wait_reset_setup_error_returns_minus_4)
{
	fail_writes(TIME_START, -1, -EIO);

	zassert_equal(config_watchdog_wait_reset(), -4);
}
