/*
 * Declarations of flash_settings.c.
 */
#ifndef G1_SERVICES_STORAGE_FLASH_SETTINGS_H_
#define G1_SERVICES_STORAGE_FLASH_SETTINGS_H_

#include "services/storage/storage.h"

int local_store_write(const char *key, const void *val, size_t len);
void sett_init(struct local_store *st);
int flash_settings_read(uint32_t addr, void *buf, size_t len);
int flash_settings_write_and_verify(uint32_t addr, const void *data, size_t len);
int write_whitelist_to_flash(uint32_t addr, const uint8_t *data, uint32_t size);
int write_font(uint32_t addr, const void *data, size_t len);
int calc_flash_crc(uint16_t *crc, int addr, uint32_t size);

#endif /* G1_SERVICES_STORAGE_FLASH_SETTINGS_H_ */
