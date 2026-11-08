/*
 * External QSPI NOR flash access (ext_flash.c), installed as GlassesState's
 * flash_read / flash_write / flash_erase callbacks.
 */
#ifndef G1_DRIVERS_FLASH_EXT_FLASH_H_
#define G1_DRIVERS_FLASH_EXT_FLASH_H_

#include <stdint.h>
#include <stddef.h>
#include "system/glasses_state.h"

typedef int (*ext_flash_read_t)(struct GlassesState *gs, uint32_t offset, void *data, size_t len);
typedef int (*ext_flash_write_t)(struct GlassesState *gs, uint32_t offset, const void *data,
				 size_t len);
typedef int (*ext_flash_erase_t)(struct GlassesState *gs, uint32_t offset, size_t size);

int ext_flash_api_init(ext_flash_read_t *read, ext_flash_write_t *write, ext_flash_erase_t *erase);

#endif /* G1_DRIVERS_FLASH_EXT_FLASH_H_ */
