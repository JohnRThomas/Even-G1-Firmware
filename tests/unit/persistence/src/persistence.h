/* Record addresses hard-coded so a moved record fails. */
#ifndef PERSISTENCE_H_
#define PERSISTENCE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/ztest.h>

#define expect_str(got, want)                                                                      \
	zassert_true(strcmp((got), (want)) == 0, "\"%s\", want \"%s\"", (got), (want))

#define FLASH_SYS_SETTINGS 0x130000
#define FLASH_USR_SETTINGS 0x134000
#define FLASH_BURIAL_POINT 0x135000
#define FLASH_SETTING      0x13e000
#define FLASH_QUICKNOTES   0x403000

#define SYS_SETTINGS_LEN 0x98
#define USR_SETTINGS_LEN 0x2c
#define BURIAL_POINT_LEN 0x11c
#define QUICKNOTE_LEN    0x200

void expect_flash(uint32_t addr, const char *hex);
void expect_flash_fill(uint32_t addr, size_t len, uint8_t v);
void put_flash(uint32_t addr, const char *hex);
void fill_flash(uint32_t addr, size_t len, uint8_t v);

/* Stands in for flash_ops_thread(); returns the wake-up count. */
int run_flash_ops_thread(void);

#endif /* PERSISTENCE_H_ */
