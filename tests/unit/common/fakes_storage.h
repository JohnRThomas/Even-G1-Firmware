/*
 * RAM NOR flash (erased on reset, kept on reboot), flash_settings.c helpers and a RAM settings
 * store.
 */
#ifndef G1_UNIT_FAKES_STORAGE_H_
#define G1_UNIT_FAKES_STORAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#define G1_FAKE_FLASH_SIZE   0x800000
#define G1_FAKE_FLASH_SECTOR 0x1000

extern uint8_t g1_fake_flash_image[G1_FAKE_FLASH_SIZE];

DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_read, struct GlassesState *, uint32_t, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_write, struct GlassesState *, uint32_t, const void *,
			size_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_flash_erase, struct GlassesState *, uint32_t, size_t);

DECLARE_FAKE_VALUE_FUNC(int, flash_settings_read, uint32_t, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, flash_settings_write_and_verify, uint32_t, const void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, write_whitelist_to_flash, uint32_t, const uint8_t *, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, write_font, uint32_t, const void *, size_t);

DECLARE_FAKE_VOID_FUNC(get_demo_image_source_from_flash);

/* for suites without file_storage.c */
DECLARE_FAKE_VOID_FUNC(storage_commit_request, int, uint8_t);
extern struct k_sem flash_ops_sem;
extern uint8_t sys_settings_dirty;
extern uint32_t LAST_FILE_CRC;

DECLARE_FAKE_VALUE_FUNC(int, g1_fake_store_write, const char *, const void *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_store_load);
DECLARE_FAKE_VOID_FUNC(g1_fake_store_del, const char *);
/* returns the length or -ENOENT */
int g1_fake_store_get(const char *key, void *buf, size_t cap);

#endif /* G1_UNIT_FAKES_STORAGE_H_ */
