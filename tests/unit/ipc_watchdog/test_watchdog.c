#include <zephyr/drivers/watchdog.h>

#include "drivers.h"
#include "harness.h"

#include "threads/threads.h"

#define WDT DEVICE_DT_GET(DT_NODELABEL(pmic_wdt))

/* nPM1300 datasheet, TIMER block */
#define TIME_BASE      0x07
#define TIME_START     0x00
#define TIME_STOP      0x01
#define TIME_KICK      0x04
#define TIME_MODE      0x05
#define MODE_WDT_RESET 0x02

/* the PMIC accesses of the test */
#define OPS_MAX  64
#define OP_TIMER 0xff /* pseudo base for set_timer */

struct pmic_op {
  uint8_t base;
  uint8_t off;
  uint32_t val; /* or the timer period in ms */
};

static struct pmic_op ops[OPS_MAX];
static int op_count;

/* the next count calls fail with ret, all when count < 0; write_fail counts writes to off only */
struct pmic_fail {
  int off;
  int count;
  int ret;
};

static struct pmic_fail write_fail;
static struct pmic_fail timer_fail;

static int injected_error(struct pmic_fail *fail) {
  if (fail->count == 0) {
    return 0;
  }
  if (fail->count > 0) {
    fail->count--;
  }
  return fail->ret;
}

static void op_add(uint8_t base, uint8_t off, uint32_t val) {
  zassert_true(op_count < OPS_MAX, "PMIC log full");
  ops[op_count++] = (struct pmic_op){base, off, val};
}

static int reg_write_rec(const struct device *dev, uint8_t base, uint8_t off, uint8_t val) {
  zassert_equal_ptr(dev, G1_NPM1300_DEV, "the watchdog talks to its parent PMIC");
  op_add(base, off, val);
  return (base == TIME_BASE && off == write_fail.off) ? injected_error(&write_fail) : 0;
}

static int set_timer_rec(const struct device *dev, uint32_t ms) {
  zassert_equal_ptr(dev, G1_NPM1300_DEV, NULL);
  op_add(OP_TIMER, 0, ms);
  return injected_error(&timer_fail);
}

static void expect_op(int index, uint8_t base, uint8_t off, uint32_t val) {
  zassert_true(index < op_count, "only %d PMIC accesses, wanted #%d", op_count, index);
  zassert_true(ops[index].base == base && ops[index].off == off && ops[index].val == val,
               "PMIC access %d: got %02x/%02x = %u, want %02x/%02x = %u", index, ops[index].base, ops[index].off,
               ops[index].val, base, off, val);
}

static int count_ops(uint8_t base, uint8_t off) {
  int count = 0;

  for (int index = 0; index < op_count; index++) {
    count += ops[index].base == base && ops[index].off == off;
  }
  return count;
}

static void expect_armed(int first, uint32_t ms) {
  expect_op(first, TIME_BASE, TIME_STOP, 1);
  expect_op(first + 1, OP_TIMER, 0, ms);
  expect_op(first + 2, TIME_BASE, TIME_MODE, MODE_WDT_RESET);
  expect_op(first + 3, TIME_BASE, TIME_START, 1);
}

static void watchdog_before(void *fixture) {
  ARG_UNUSED(fixture);
  /* the driver keeps its timeout across tests */
  (void)wdt_disable(WDT);
  wdt_0_channel_id = 0;

  RESET_FAKE(mfd_npm1300_reg_write);
  RESET_FAKE(mfd_npm1300_set_timer);
  mfd_npm1300_reg_write_fake.custom_fake = reg_write_rec;
  mfd_npm1300_set_timer_fake.custom_fake = set_timer_rec;
  op_count = 0;
  write_fail = (struct pmic_fail){0};
  timer_fail = (struct pmic_fail){0};
}

ZTEST_SUITE(watchdog, NULL, NULL, watchdog_before, NULL, NULL);

ZTEST(watchdog, test_init_arms_a_30s_reset_watchdog_and_feeds_it) {
  zassert_equal(init_watchdog(), 0);

  zassert_equal(op_count, 5, "%d PMIC accesses", op_count);
  expect_armed(0, 30000);
  expect_op(4, TIME_BASE, TIME_KICK, 1);
  zassert_equal(wdt_0_channel_id, 0, "the nPM1300 has one channel, 0");
}

ZTEST(watchdog, test_init_on_a_pmic_that_is_not_ready_touches_nothing) {
  g1_device_set_ready(WDT, false);

  zassert_equal(init_watchdog(), 0, "the caller does not check the result");
  zassert_equal(op_count, 0);
}

ZTEST(watchdog, test_init_retries_the_install_when_the_driver_rejects_callbacks) {
  timer_fail = (struct pmic_fail){.count = 1, .ret = -ENOTSUP};

  zassert_equal(init_watchdog(), 0);
  zassert_equal(count_ops(OP_TIMER, 0), 2, "installed twice");
  expect_op(1, OP_TIMER, 0, 30000);
  expect_op(2, OP_TIMER, 0, 30000);
  expect_op(3, TIME_BASE, TIME_MODE, MODE_WDT_RESET);
  expect_op(4, TIME_BASE, TIME_START, 1);
  zassert_equal(wdt_0_channel_id, 0);
}

ZTEST(watchdog, test_init_install_error_starts_nothing_and_leaves_no_channel) {
  timer_fail = (struct pmic_fail){.count = -1, .ret = -EIO};

  zassert_equal(init_watchdog(), 0);
  zassert_equal(count_ops(OP_TIMER, 0), 1, "-EIO is not retried");
  zassert_equal(count_ops(TIME_BASE, TIME_START), 0, "not started");
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0, "not fed");
  zassert_true(wdt_0_channel_id < 0, "channel %d", wdt_0_channel_id);

  op_count = 0;
  zassert_equal(watchdog_disable(), 0);
  zassert_equal(op_count, 0);
}

ZTEST(watchdog, test_init_setup_error_does_not_feed) {
  write_fail = (struct pmic_fail){.off = TIME_START, .count = -1, .ret = -EIO};

  zassert_equal(init_watchdog(), 0);
  zassert_equal(count_ops(TIME_BASE, TIME_START), 1);
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0);
}

ZTEST(watchdog, test_init_feed_is_retried_every_50ms_until_it_succeeds) {
  int64_t start = g1_clock_uptime_ms();

  write_fail = (struct pmic_fail){.off = TIME_KICK, .count = 2, .ret = -EIO};

  zassert_equal(init_watchdog(), 0);
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 3);
  /* k_sleep(50) lasts 50..51 ms on 1 ms ticks */
  expect_elapsed_ms(start, 100, 104);
}

ZTEST(watchdog, test_feed_kicks_the_pmic_watchdog_once) {
  init_watchdog();
  op_count = 0;

  zassert_equal(watchdog_feed(), 0);
  zassert_equal(op_count, 1);
  expect_op(0, TIME_BASE, TIME_KICK, 1);
}

ZTEST(watchdog, test_feed_gives_up_after_5_attempts_and_returns_the_error) {
  int64_t start;

  init_watchdog();
  op_count = 0;
  write_fail = (struct pmic_fail){.off = TIME_KICK, .count = -1, .ret = -EIO};
  start = g1_clock_uptime_ms();

  zassert_equal(watchdog_feed(), -EIO);
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 5);
  /* 4 to 5 sleeps of 50..51 ms */
  expect_elapsed_ms(start, 200, 260);
}

ZTEST(watchdog, test_disable_stops_the_pmic_timer_and_forgets_the_channel) {
  init_watchdog();
  op_count = 0;

  zassert_equal(watchdog_disable(), 0);
  zassert_equal(op_count, 1);
  expect_op(0, TIME_BASE, TIME_STOP, 1);
  zassert_equal(wdt_0_channel_id, -EBUSY);

  op_count = 0;
  zassert_equal(watchdog_disable(), 0);
  zassert_equal(op_count, 0);
}

ZTEST(watchdog, test_feed_after_disable_does_not_kick_the_pmic) {
  init_watchdog();
  watchdog_disable();
  op_count = 0;

  zassert_not_equal(watchdog_feed(), 0);
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0);
  zassert_equal(count_ops(TIME_BASE, TIME_START), 0);
}

ZTEST(watchdog, test_disable_error_after_5_attempts_keeps_the_channel) {
  init_watchdog();
  op_count = 0;
  write_fail = (struct pmic_fail){.off = TIME_STOP, .count = -1, .ret = -EIO};

  zassert_equal(watchdog_disable(), -EIO);
  zassert_equal(count_ops(TIME_BASE, TIME_STOP), 5);
  zassert_equal(wdt_0_channel_id, 0, "still armed: the channel stays valid");
}

ZTEST(watchdog, test_wait_reset_rearms_100ms_reset_without_feeding) {
  init_watchdog();
  op_count = 0;

  zassert_equal(config_watchdog_wait_reset(), 0);
  zassert_equal(op_count, 4);
  expect_armed(0, 100);
  zassert_equal(count_ops(TIME_BASE, TIME_KICK), 0, "the point is that nobody feeds it");
  zassert_equal(wdt_0_channel_id, 0);
}

ZTEST(watchdog, test_wait_reset_on_a_pmic_that_is_not_ready_returns_minus_1) {
  g1_device_set_ready(WDT, false);

  zassert_equal(config_watchdog_wait_reset(), -1);
  zassert_equal(op_count, 0);
}

ZTEST(watchdog, test_wait_reset_disable_error_returns_minus_2) {
  write_fail = (struct pmic_fail){.off = TIME_STOP, .count = -1, .ret = -EIO};

  zassert_equal(config_watchdog_wait_reset(), -2);
  zassert_equal(count_ops(OP_TIMER, 0), 0, "not re-armed");
}

ZTEST(watchdog, test_wait_reset_install_error_returns_minus_3) {
  timer_fail = (struct pmic_fail){.count = -1, .ret = -EIO};

  zassert_equal(config_watchdog_wait_reset(), -3);
  zassert_true(wdt_0_channel_id < 0);
  zassert_equal(count_ops(TIME_BASE, TIME_START), 0);
}

ZTEST(watchdog, test_wait_reset_setup_error_returns_minus_4) {
  write_fail = (struct pmic_fail){.off = TIME_START, .count = -1, .ret = -EIO};

  zassert_equal(config_watchdog_wait_reset(), -4);
}
