#include <string.h>

#include "harness.h"
#include "storage_fakes.h"

DEFINE_FAKE_VOID_FUNC(ext_flash_power_up);
DEFINE_FAKE_VOID_FUNC(ext_flash_power_down);
DEFINE_FAKE_VALUE_FUNC(int, watchdog_feed);
DEFINE_FAKE_VALUE_FUNC(uint8_t *, get_app_whitelist_info);

uint8_t watchdog_enabled;
uint8_t fuel_gauge_stuck;
uint8_t g1_test_whitelist[G1_TEST_WHITELIST_SIZE];

static uint8_t *whitelist_default(void)
{
	return g1_test_whitelist;
}

static void storage_fakes_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(ext_flash_power_up);
	RESET_FAKE(ext_flash_power_down);
	RESET_FAKE(watchdog_feed);
	RESET_FAKE(get_app_whitelist_info);
	get_app_whitelist_info_fake.custom_fake = whitelist_default;
	watchdog_enabled = 0;
	fuel_gauge_stuck = 0;
	memset(g1_test_whitelist, 0, sizeof(g1_test_whitelist));
}

G1_UNIT_RESET_HOOK(storage_fakes_reset);
