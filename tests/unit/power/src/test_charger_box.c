/* nPM1300 register values from its product specification (CHARGER, base 03h). */
#include <errno.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fake_npm1300.h"

#include "sensors_audio/sensors_audio.h"

#define CHGR_BASE        0x03
#define CHGR_EN_SET      0x04
#define CHGR_VTERM       0x0c
#define CHGR_CHG_STAT    0x34
#define VTERM_4V20       8

#define BOX_CHARGING     2

#define STAT_DETECTED    BIT(0)
#define STAT_COMPLETED   BIT(1)
#define STAT_TRICKLE     BIT(2)
#define STAT_CC          BIT(3)
#define STAT_CV          BIT(4)

static void charger_box_before(void *fixture)
{
	ARG_UNUSED(fixture);
	init_box_status();
}

ZTEST_SUITE(charger_box, NULL, NULL, charger_box_before, NULL, NULL);

static void expect_flags(int in_box, int lid_closed, int charging)
{
	zassert_equal(is_in_box(), in_box);
	zassert_equal(is_box_lid_closed(), lid_closed);
	zassert_equal(is_box_charging(), charging);
	zassert_equal(get_box_status_by_type(BOX_IN_BOX), in_box);
	zassert_equal(get_box_status_by_type(BOX_LID_CLOSED), lid_closed);
	zassert_equal(get_box_status_by_type(BOX_CHARGING), charging);
}

/* ---- case flags ---- */

ZTEST(charger_box, test_after_init_the_lens_is_out_of_the_case)
{
	set_box_status(BOX_IN_BOX, 1);
	set_box_status(BOX_LID_CLOSED, 1);
	set_box_status(BOX_CHARGING, 1);

	init_box_status();
	expect_flags(0, 0, 0);
}

ZTEST(charger_box, test_each_flag_is_set_and_cleared_on_its_own)
{
	set_box_status(BOX_IN_BOX, 1);
	expect_flags(1, 0, 0);
	set_box_status(BOX_LID_CLOSED, 1);
	expect_flags(1, 1, 0);
	set_box_status(BOX_CHARGING, 1);
	expect_flags(1, 1, 1);
	set_box_status(BOX_LID_CLOSED, 0);
	expect_flags(1, 0, 1);
	set_box_status(BOX_IN_BOX, 0);
	expect_flags(0, 0, 1);
	set_box_status(BOX_CHARGING, 0);
	expect_flags(0, 0, 0);
}

ZTEST(charger_box, test_any_nonzero_status_sets_the_flag_to_1)
{
	set_box_status(BOX_IN_BOX, 0x80);
	set_box_status(BOX_LID_CLOSED, 2);
	set_box_status(BOX_CHARGING, 0xff);
	expect_flags(1, 1, 1);
}

ZTEST(charger_box, test_unknown_type_is_ignored_and_reads_as_0xfe)
{
	set_box_status(3, 1);
	set_box_status(0xff, 1);
	expect_flags(0, 0, 0);
	zassert_equal(get_box_status_by_type(3), 0xfe);
	zassert_equal(get_box_status_by_type(0xff), 0xfe);
}

/* ---- charger registers ---- */

ZTEST(charger_box, test_termination_voltage_is_set_to_4_20v)
{
	zassert_equal(change_termination_voltage(), 0);
	zassert_equal(mfd_npm1300_reg_write_fake.call_count, 1);
	zassert_equal_ptr(mfd_npm1300_reg_write_fake.arg0_val, G1_NPM1300_DEV);
	zassert_equal(mfd_npm1300_reg_write_fake.arg1_val, CHGR_BASE);
	zassert_equal(mfd_npm1300_reg_write_fake.arg2_val, CHGR_VTERM);
	zassert_equal(mfd_npm1300_reg_write_fake.arg3_val, VTERM_4V20);
}

ZTEST(charger_box, test_termination_voltage_write_error_returns_minus_1)
{
	mfd_npm1300_reg_write_fake.return_val = -EIO;

	zassert_equal(change_termination_voltage(), -1);
}

ZTEST(charger_box, test_enable_charger_sets_the_charging_enable_bit)
{
	zassert_equal(enable_charger(), 0);
	zassert_equal(mfd_npm1300_reg_write_fake.call_count, 1);
	zassert_equal_ptr(mfd_npm1300_reg_write_fake.arg0_val, G1_NPM1300_DEV);
	zassert_equal(mfd_npm1300_reg_write_fake.arg1_val, CHGR_BASE);
	zassert_equal(mfd_npm1300_reg_write_fake.arg2_val, CHGR_EN_SET);
	zassert_equal(mfd_npm1300_reg_write_fake.arg3_val, 0x01);
	zassert_equal(mfd_npm1300_reg_read_fake.call_count, 0, "a plain write, no read-back");
}

ZTEST(charger_box, test_enable_charger_write_error_returns_minus_1)
{
	mfd_npm1300_reg_write_fake.return_val = -EIO;

	zassert_equal(enable_charger(), -1);
}

static uint8_t chg_stat;

static int read_chg_stat(const struct device *dev, uint8_t base, uint8_t off, uint8_t *data)
{
	zassert_equal_ptr(dev, G1_NPM1300_DEV);
	zassert_equal(base, CHGR_BASE);
	zassert_equal(off, CHGR_CHG_STAT);
	*data = chg_stat;
	return 0;
}

ZTEST(charger_box, test_charger_status_is_0_while_charging_and_1_otherwise)
{
	static const struct {
		uint8_t stat;
		int ret;
		const char *what;
	} cases[] = {
		{STAT_DETECTED | STAT_TRICKLE, 0, "trickle"},
		{STAT_DETECTED | STAT_CC, 0, "constant current"},
		{STAT_DETECTED | STAT_CV, 0, "constant voltage"},
		{STAT_DETECTED | STAT_COMPLETED, 1, "completed"},
		{STAT_DETECTED, 1, "battery, charger idle"},
		{0x00, 1, "nothing"},
	};

	mfd_npm1300_reg_read_fake.custom_fake = read_chg_stat;
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		chg_stat = cases[i].stat;
		zassert_equal(set_charger_status(), cases[i].ret, "%s (%02x)", cases[i].what,
			      cases[i].stat);
	}
}

ZTEST(charger_box, test_charger_status_read_error_returns_minus_1)
{
	mfd_npm1300_reg_read_fake.custom_fake = NULL;
	mfd_npm1300_reg_read_fake.return_val = -EIO;

	zassert_equal(set_charger_status(), -1);
}
