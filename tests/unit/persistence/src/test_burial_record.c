/* update_burial_point_to_flash() returns nothing on success; only its error return is checked. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_misc.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

int clear_hardware_settings(struct GlassesState *gs);

static const char lens_record[] =
	/* 000 */ "d4 84 d6 e0 40 56 00 00 08 00 00 00 e4 0e 00 00"
	/* 010 */ "83 01 00 00 07 03 01 00 00 00 00 00 00 00 00 00"
	/* 020 */ "00 00 00 00 e9 07 0c 1f 38 04 00 00 02 00 00 00"
	/* 030 */ "ea 07 01 01 b4 4b 00 00 05 00 00 00 ea 07 01 02"
	/* 040 */ "54 06 00 00 01 00 00 00 00 00 00 00 00 00 00 00"
	/* 050 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 060 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 070 */ "00 00 00 00 00 00 00 00 e9 07 0c 1f a8 01 00 00"
	/* 080 */ "3a 00 00 00 ea 07 01 01 8e 09 00 00 ec 00 00 00"
	/* 090 */ "ea 07 01 02 ae 03 00 00 5d 00 00 00 00 00 00 00"
	/* 0a0 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 0b0 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 0c0 */ "43 00 00 00 08 00 00 00 00 00 00 00 00 00 00 00"
	/* 0d0 */ "00 00 00 00 e9 07 0c 1f 0d 00 00 00 04 00 00 00"
	/* 0e0 */ "ea 07 01 01 36 00 00 00 04 00 00 00 ea 07 01 02"
	/* 0f0 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 100 */ "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	/* 110 */ "00 00 00 00 00 00 00 00 00 00 00 00";

static void load_lens_record_into_ram(void)
{
	uint8_t rec[BURIAL_POINT_LEN];

	zassert_equal(g1_hex_parse(lens_record, rec, sizeof(rec)), BURIAL_POINT_LEN);
	memcpy(&g1_state()->bp, rec, sizeof(rec));
}

ZTEST(persistence, test_burial_point_is_written_verbatim_at_0x135000)
{
	load_lens_record_into_ram();

	update_burial_point_to_flash(g1_state());

	expect_flash(FLASH_BURIAL_POINT, lens_record);
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 1);
	zassert_equal(flash_settings_write_and_verify_fake.arg0_val, FLASH_BURIAL_POINT);
	zassert_equal(flash_settings_write_and_verify_fake.arg2_val, BURIAL_POINT_LEN);
}

ZTEST(persistence, test_burial_point_survives_a_reboot)
{
	struct bp_context *bp;

	load_lens_record_into_ram();
	update_burial_point_to_flash(g1_state());

	g1_unit_reboot();
	zassert_equal(load_burial_point(g1_state()), 0);

	bp = &g1_state()->bp;
	zassert_equal(bp->id, 0xe0d684d4);
	zassert_equal(bp->wear_sum_time, 0x5640);
	zassert_equal(bp->dashboard_count, 0x183);
	zassert_equal(bp->version, 7);
	zassert_equal(bp->day_index, 3);
	zassert_equal(bp->valid, 1);
	zassert_equal(bp->wear_day[3].year, 2026);
	zassert_equal(bp->wear_day[3].month, 1);
	zassert_equal(bp->wear_day[3].day, 2);
	zassert_equal(bp->wear_day[3].sum_time, 1620);
	zassert_equal(bp->wear_day[3].count, 1);
	zassert_equal(bp->notification_sum_time, 0x43);
	zassert_equal(bp->notification_count, 8);
	/* existing record keeps its id */
	zassert_equal(z_impl_sys_rand32_get_fake.call_count, 0);
}

ZTEST(persistence, test_unchanged_burial_point_is_not_rewritten)
{
	load_lens_record_into_ram();
	update_burial_point_to_flash(g1_state());
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 1);

	update_burial_point_to_flash(g1_state());
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 1, "rewritten");

	g1_state()->bp.wear_sum_time += 60;
	update_burial_point_to_flash(g1_state());
	zassert_equal(flash_settings_write_and_verify_fake.call_count, 2, "change not written");
	expect_flash(FLASH_BURIAL_POINT + 4, "7c 56 00 00");
}

ZTEST(persistence, test_burial_point_read_error_writes_nothing)
{
	load_lens_record_into_ram();
	flash_settings_read_fake.custom_fake = NULL;
	flash_settings_read_fake.return_val = -EIO;

	zassert_equal(update_burial_point_to_flash(g1_state()), -1);

	zassert_equal(flash_settings_write_and_verify_fake.call_count, 0);
	expect_flash_fill(FLASH_BURIAL_POINT, BURIAL_POINT_LEN, 0xff);
}

ZTEST(persistence, test_erased_burial_point_starts_a_new_record)
{
	static const uint8_t zero[BURIAL_POINT_LEN - 0x18];
	struct bp_context *bp = &g1_state()->bp;

	z_impl_sys_rand32_get_fake.return_val = 0x5eed1234;
	memset(bp, 0xa5, sizeof(*bp));

	zassert_equal(load_burial_point(g1_state()), 0);

	zassert_equal(bp->id, 0x5eed1234);
	zassert_equal(bp->version, 7);
	zassert_equal(bp->valid, 1);
	zassert_equal(bp->day_index, 0);
	zassert_equal(bp->wear_sum_time + bp->wear_count + bp->dashboard_sum_time +
			      bp->dashboard_count,
		      0);
	zassert_mem_equal((uint8_t *)bp + 0x18, zero, sizeof(zero));
	zassert_equal(z_impl_sys_rand32_get_fake.call_count, 1);
}

ZTEST(persistence, test_burial_point_with_valid_0_starts_a_new_record)
{
	struct bp_context *bp = &g1_state()->bp;

	put_flash(FLASH_BURIAL_POINT, lens_record);
	put_flash(FLASH_BURIAL_POINT + 0x16, "00");
	z_impl_sys_rand32_get_fake.return_val = 0x0badcafe;

	zassert_equal(load_burial_point(g1_state()), 0);

	zassert_equal(bp->id, 0x0badcafe);
	zassert_equal(bp->version, 7);
	zassert_equal(bp->valid, 1);
	zassert_equal(bp->wear_sum_time, 0);
	zassert_equal(bp->notification_count, 0);
	zassert_equal(bp->wear_day[3].year, 0);
}

ZTEST(persistence, test_burial_point_load_read_error_returns_minus_1)
{
	flash_settings_read_fake.custom_fake = NULL;
	flash_settings_read_fake.return_val = -EIO;

	zassert_equal(load_burial_point(g1_state()), -1);

	zassert_equal(z_impl_sys_rand32_get_fake.call_count, 0);
}

ZTEST(persistence, test_clear_hardware_settings_erases_the_burial_point)
{
	static uint8_t ff[sizeof(struct bp_context)];

	memset(ff, 0xff, sizeof(ff));
	load_lens_record_into_ram();
	update_burial_point_to_flash(g1_state());

	zassert_equal(clear_hardware_settings(g1_state()), 0);

	expect_flash_fill(FLASH_BURIAL_POINT, BURIAL_POINT_LEN, 0xff);
	zassert_mem_equal(&g1_state()->bp, ff, sizeof(ff));
}
