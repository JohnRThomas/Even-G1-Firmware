#ifndef STORAGE_FAKES_H_
#define STORAGE_FAKES_H_

#include <stdint.h>
#include <zephyr/fff.h>

#define G1_TEST_WHITELIST_SIZE 0x15e2

DECLARE_FAKE_VOID_FUNC(ext_flash_power_up);
DECLARE_FAKE_VOID_FUNC(ext_flash_power_down);
DECLARE_FAKE_VALUE_FUNC(int, watchdog_feed);
DECLARE_FAKE_VALUE_FUNC(uint8_t *, get_app_whitelist_info);

extern uint8_t watchdog_enabled;
extern uint8_t fuel_gauge_stuck;
extern uint8_t g1_test_whitelist[G1_TEST_WHITELIST_SIZE];

#endif /* STORAGE_FAKES_H_ */
