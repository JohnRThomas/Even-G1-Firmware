#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "nor_flash.h"
#include "storage_fakes.h"

#include "sensors_audio/ext_flash.c"

ZTEST_SUITE(ext_flash, NULL, NULL, NULL, NULL, NULL);

static struct GlassesState *gs(void)
{
	return g1_state();
}

/* ---- ext_flash_api_init ---- */

/* 32 MiB / 4 KiB pages */
ZTEST(ext_flash, test_api_init_installs_the_wrappers_and_the_page_count)
{
	gs()->flash_read = NULL;
	gs()->flash_write = NULL;
	gs()->flash_erase = NULL;
	gs()->flash_page_count = 0;

	zassert_equal(ext_flash_api_init(&gs()->flash_read, &gs()->flash_write,
					 &gs()->flash_erase), 0);

	zassert_equal_ptr(gs()->flash_read, ext_flash_read);
	zassert_equal_ptr(gs()->flash_write, ext_flash_write);
	zassert_equal_ptr(gs()->flash_erase, ext_flash_erase);
	zassert_equal(gs()->flash_page_count, 8192);
	zassert_equal_ptr(ext_flash_dev, G1_NOR_DEV);
}

ZTEST(ext_flash, test_api_init_without_the_flash_returns_minus_one_and_installs_nothing)
{
	ext_flash_dev = NULL;
	gs()->flash_read = NULL;
	gs()->flash_write = NULL;
	gs()->flash_erase = NULL;
	gs()->flash_page_count = 0;
	g1_device_set_ready(G1_NOR_DEV, false);

	zassert_equal(ext_flash_api_init(&gs()->flash_read, &gs()->flash_write,
					 &gs()->flash_erase), -1);

	zassert_is_null(gs()->flash_read);
	zassert_is_null(gs()->flash_write);
	zassert_is_null(gs()->flash_erase);
	zassert_equal(gs()->flash_page_count, 0);
	zassert_is_null(ext_flash_dev);
}

/* ---- read / write / erase ---- */

ZTEST(ext_flash, test_write_lands_at_its_address_and_reads_back_unaligned)
{
	static const uint8_t data[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
	static const uint8_t want[10] = { 0xff, 0x11, 0x22, 0x33, 0x44,
					  0x55, 0x66, 0x77, 0x88, 0xff };
	uint8_t buf[10];

	zassert_equal(gs()->flash_write(gs(), 0x1004, data, sizeof(data)), 0);
	zassert_equal(gs()->flash_read(gs(), 0x1003, buf, sizeof(buf)), 0);

	zassert_mem_equal(buf, want, sizeof(want));
}

ZTEST(ext_flash, test_erase_clears_whole_sectors_only)
{
	uint8_t *m = g1_nor_mem(0x1fff, 0x1002);

	memset(m, 0x00, 0x1002);

	zassert_equal(gs()->flash_erase(gs(), 0x2000, 0x1000), 0);

	zassert_equal(m[0], 0x00, "byte before the sector");
	for (int i = 1; i <= 0x1000; i++) {
		zassert_equal(m[i], 0xff, "0x%x not erased", 0x1fff + i);
	}
	zassert_equal(m[0x1001], 0x00, "byte after the sector");
}

ZTEST(ext_flash, test_driver_errors_are_returned_unchanged)
{
	static const uint8_t data[4] = { 0 };
	uint8_t buf[4];

	g1_nor_fail(G1_NOR_READ, 1, -ETIMEDOUT);
	g1_nor_fail(G1_NOR_WRITE, 1, -EIO);
	g1_nor_fail(G1_NOR_ERASE, 1, -EBUSY);

	zassert_equal(gs()->flash_read(gs(), 0, buf, 4), -ETIMEDOUT);
	zassert_equal(gs()->flash_write(gs(), 0, data, 4), -EIO);
	zassert_equal(gs()->flash_erase(gs(), 0, G1_NOR_SECTOR), -EBUSY);
}

/* ---- power hooks ---- */

static int reads_at_power_up;
static int reads_at_power_down;

static void power_up_probe(void)
{
	reads_at_power_up = g1_nor_stats()->calls[G1_NOR_READ];
}

static void power_down_probe(void)
{
	reads_at_power_down = g1_nor_stats()->calls[G1_NOR_READ];
}

/* module_mask bit 4: the flash module */
ZTEST(ext_flash, test_power_hooks_wrap_each_access_while_the_flash_module_is_off)
{
	static const uint8_t data[4] = { 0 };
	uint8_t buf[4];

	gs()->module_mask &= ~BIT(4);
	ext_flash_power_up_fake.custom_fake = power_up_probe;
	ext_flash_power_down_fake.custom_fake = power_down_probe;

	zassert_equal(gs()->flash_read(gs(), 0, buf, 4), 0);
	zassert_equal(reads_at_power_up, 0, "powered up after the read");
	zassert_equal(reads_at_power_down, 1, "powered down before the read");

	zassert_equal(gs()->flash_write(gs(), 0, data, 4), 0);
	zassert_equal(gs()->flash_erase(gs(), 0, G1_NOR_SECTOR), 0);
	zassert_equal(ext_flash_power_up_fake.call_count, 3);
	zassert_equal(ext_flash_power_down_fake.call_count, 3);
}

ZTEST(ext_flash, test_power_hooks_are_skipped_while_the_flash_module_is_on)
{
	static const uint8_t data[4] = { 0 };
	uint8_t buf[4];

	zassert_true(gs()->module_mask & BIT(4));

	gs()->flash_read(gs(), 0, buf, 4);
	gs()->flash_write(gs(), 0, data, 4);
	gs()->flash_erase(gs(), 0, G1_NOR_SECTOR);

	zassert_equal(ext_flash_power_up_fake.call_count, 0);
	zassert_equal(ext_flash_power_down_fake.call_count, 0);
}

/* ---- serialisation ---- */

K_THREAD_STACK_DEFINE(writer_stack, 2048);
static struct k_thread writer;
static int writer_ret;

static void writer_fn(void *p1, void *p2, void *p3)
{
	static const uint8_t data[4] = { 0xa5, 0xa5, 0xa5, 0xa5 };

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	writer_ret = g1_state()->flash_write(g1_state(), 0x4000, data, sizeof(data));
}

ZTEST(ext_flash, test_an_access_waits_for_the_flash_mutex)
{
	writer_ret = 1;
	k_mutex_lock(&ext_flash_mutex, K_FOREVER);
	k_thread_create(&writer, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack), writer_fn,
			NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);

	g1_clock_advance_ms(10); /* the writer runs and blocks on the mutex */
	zassert_equal(g1_nor_stats()->calls[G1_NOR_WRITE], 0, "write went past the mutex");
	zassert_equal(*g1_nor_mem(0x4000, 4), 0xff);

	k_mutex_unlock(&ext_flash_mutex);
	zassert_ok(k_thread_join(&writer, K_MSEC(100)));
	zassert_equal(writer_ret, 0);
	zassert_equal(*g1_nor_mem(0x4000, 4), 0xa5);
}
