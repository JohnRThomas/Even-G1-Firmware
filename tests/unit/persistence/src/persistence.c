#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_storage.h"
#include "persistence.h"
#include "storage/storage.h"

#define MAX_RECORD 0x400

void expect_flash(uint32_t addr, const char *hex)
{
	static uint8_t want[MAX_RECORD];
	static char want_txt[3 * 64 + 8];
	static char got_txt[3 * 64 + 8];
	const uint8_t *got = &g1_fake_flash_image[addr];
	int n = g1_hex_parse(hex, want, sizeof(want));

	zassert_true(n > 0, "bad hex in test (%d)", n);
	for (int i = 0; i < n; i++) {
		if (got[i] != want[i]) {
			int from = i & ~0xf;
			int len = MIN(16, n - from);

			g1_hex_format(&want[from], len, want_txt, sizeof(want_txt));
			g1_hex_format(&got[from], len, got_txt, sizeof(got_txt));
			zassert_unreachable("flash 0x%06x (record byte 0x%02x): 0x%02x, want 0x%02x\n"
					    "  record bytes 0x%02x..: want %s\n"
					    "                          got  %s",
					    addr + i, i, got[i], want[i], from, want_txt, got_txt);
		}
	}
}

void expect_flash_fill(uint32_t addr, size_t len, uint8_t v)
{
	for (size_t i = 0; i < len; i++) {
		zassert_equal(g1_fake_flash_image[addr + i], v,
			      "flash 0x%06x (byte 0x%x of %u): 0x%02x, want 0x%02x",
			      (unsigned int)(addr + i), (unsigned int)i, (unsigned int)len,
			      g1_fake_flash_image[addr + i], v);
	}
}

void put_flash(uint32_t addr, const char *hex)
{
	static uint8_t data[MAX_RECORD];
	int n = g1_hex_parse(hex, data, sizeof(data));

	zassert_true(n > 0, "bad hex in test (%d)", n);
	memcpy(&g1_fake_flash_image[addr], data, n);
}

void fill_flash(uint32_t addr, size_t len, uint8_t v)
{
	memset(&g1_fake_flash_image[addr], v, len);
}

int run_flash_ops_thread(void)
{
	int wakeups = 0;

	while (k_sem_take(&flash_ops_sem, K_NO_WAIT) == 0) {
		wakeups++;
		while (getQuickNoteQueueNum() != 0) {
			QuickNoteStoreHandler();
		}
		while (getSettingStorageQueueNum()) {
			SettingStoreHandler();
		}
	}
	return wakeups;
}
