#define DT_DRV_COMPAT vnd_g1_unit_nor_flash

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/flash.h>

#include "harness.h"
#include "nor_flash.h"
#include "sensors_audio/sensors_audio.h"

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "one mx25r64");
BUILD_ASSERT(G1_NOR_SIZE % G1_NOR_SECTOR == 0, "size must be whole sectors");

static uint8_t nor_mem[G1_NOR_SIZE];
/* touched since reset */
static uint32_t dirty_lo = UINT32_MAX;
static uint32_t dirty_hi;

static struct g1_nor_stats stats;
static int fail_count[G1_NOR_OPS];
static int fail_err[G1_NOR_OPS];

static void mark_dirty(uint32_t addr, size_t len)
{
	dirty_lo = MIN(dirty_lo, addr);
	dirty_hi = MAX(dirty_hi, (uint32_t)(addr + len));
}

static bool in_range(off_t addr, size_t len)
{
	return addr >= 0 && len <= G1_NOR_SIZE && (size_t)addr <= G1_NOR_SIZE - len;
}

static bool injected(enum g1_nor_op op, int *ret)
{
	if (fail_count[op] == 0) {
		return false;
	}
	fail_count[op]--;
	*ret = fail_err[op];
	return true;
}

static void count(enum g1_nor_op op, off_t addr, size_t len)
{
	stats.calls[op]++;
	stats.last_addr[op] = (uint32_t)addr;
	stats.last_len[op] = len;
}

static int reject(enum g1_nor_op op)
{
	stats.rejected[op]++;
	return -EINVAL;
}

static int nor_read(const struct device *dev, off_t addr, void *data, size_t len)
{
	int ret;

	ARG_UNUSED(dev);
	count(G1_NOR_READ, addr, len);
	if (data == NULL) {
		return reject(G1_NOR_READ);
	}
	if (len == 0) {
		return 0;
	}
	if (!in_range(addr, len)) {
		return reject(G1_NOR_READ);
	}
	if (injected(G1_NOR_READ, &ret)) {
		return ret;
	}
	memcpy(data, &nor_mem[addr], len);
	return 0;
}

static int nor_write(const struct device *dev, off_t addr, const void *data, size_t len)
{
	const uint8_t *src = data;
	int ret;

	ARG_UNUSED(dev);
	count(G1_NOR_WRITE, addr, len);
	/* nrf_qspi_nor.c write rules */
	if (data == NULL || len == 0 || (len > 4 && len % 4 != 0) || addr % 4 != 0 ||
	    !in_range(addr, len)) {
		return reject(G1_NOR_WRITE);
	}
	if (injected(G1_NOR_WRITE, &ret)) {
		return ret;
	}
	mark_dirty(addr, len);
	for (size_t i = 0; i < len; i++) {
		nor_mem[addr + i] &= src[i];
	}
	return 0;
}

static int nor_erase(const struct device *dev, off_t addr, size_t len)
{
	int ret;

	ARG_UNUSED(dev);
	count(G1_NOR_ERASE, addr, len);
	if (!in_range(addr, len) || addr % G1_NOR_SECTOR != 0 || len == 0 ||
	    len % G1_NOR_SECTOR != 0) {
		return reject(G1_NOR_ERASE);
	}
	if (injected(G1_NOR_ERASE, &ret)) {
		return ret;
	}
	mark_dirty(addr, len);
	memset(&nor_mem[addr], 0xff, len);
	return 0;
}

static const struct flash_parameters nor_parameters = {
	.write_block_size = 4,
	.erase_value = 0xff,
};

static const struct flash_parameters *nor_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &nor_parameters;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static const struct flash_pages_layout nor_layout = {
	.pages_count = G1_NOR_SIZE / G1_NOR_SECTOR,
	.pages_size = G1_NOR_SECTOR,
};

static void nor_page_layout(const struct device *dev, const struct flash_pages_layout **layout,
			    size_t *layout_size)
{
	ARG_UNUSED(dev);
	*layout = &nor_layout;
	*layout_size = 1;
}
#endif

static const struct flash_driver_api nor_api = {
	.read = nor_read,
	.write = nor_write,
	.erase = nor_erase,
	.get_parameters = nor_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = nor_page_layout,
#endif
};

static int nor_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	memset(nor_mem, 0xff, sizeof(nor_mem));
	return 0;
}

DEVICE_DT_INST_DEFINE(0, nor_init, NULL, NULL, NULL, POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY,
		      &nor_api);

uint8_t *g1_nor_mem(uint32_t addr, size_t len)
{
	__ASSERT(in_range(addr, len), "g1_nor_mem(0x%x, %u) outside the flash", addr,
		 (unsigned int)len);
	mark_dirty(addr, len);
	return &nor_mem[addr];
}

void g1_nor_fail(enum g1_nor_op op, int n, int err)
{
	__ASSERT(op < G1_NOR_OPS, "op %d", op);
	fail_count[op] = n;
	fail_err[op] = err;
}

const struct g1_nor_stats *g1_nor_stats(void)
{
	return &stats;
}

#pragma weak ext_flash_api_init

static void nor_reset(bool keep_media)
{
	struct GlassesState *gs = get_dashboard_state();

	if (!keep_media && dirty_lo < dirty_hi) {
		memset(&nor_mem[dirty_lo], 0xff, dirty_hi - dirty_lo);
		dirty_lo = UINT32_MAX;
		dirty_hi = 0;
	}
	memset(&stats, 0, sizeof(stats));
	memset(fail_count, 0, sizeof(fail_count));
	if (ext_flash_api_init) {
		gs->ext_flash_ok = ext_flash_api_init(&gs->flash_read, &gs->flash_write,
						      &gs->flash_erase);
	}
}

/* after the fakes' hooks */
static int nor_reset_register(void)
{
	g1_unit_register_reset(nor_reset);
	return 0;
}

SYS_INIT(nor_reset_register, APPLICATION, 1);
