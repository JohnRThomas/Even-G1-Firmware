/*
 * mx25r64 as a RAM flash with the argument rules of nrf_qspi_nor.c (else -EINVAL). The reset erases
 * only what was touched, so g1_nor_mem() takes the length the test will use.
 * g1_nor_fail(G1_NOR_WRITE, n, 0) drops the data but reports success.
 */
#ifndef G1_UNIT_NOR_FLASH_H_
#define G1_UNIT_NOR_FLASH_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include "devices.h"

#define G1_NOR_DEV    DEVICE_DT_GET(DT_NODELABEL(mx25r64))
#define G1_NOR_SIZE   DT_PROP(DT_NODELABEL(mx25r64), size)
#define G1_NOR_SECTOR 4096

enum g1_nor_op {
	G1_NOR_READ,
	G1_NOR_WRITE,
	G1_NOR_ERASE,
	G1_NOR_OPS
};

struct g1_nor_stats {
	int calls[G1_NOR_OPS];
	int rejected[G1_NOR_OPS];
	uint32_t last_addr[G1_NOR_OPS];
	size_t last_len[G1_NOR_OPS];
};

uint8_t *g1_nor_mem(uint32_t addr, size_t len);
void g1_nor_fail(enum g1_nor_op op, int count, int err);
const struct g1_nor_stats *g1_nor_stats(void);

#endif /* G1_UNIT_NOR_FLASH_H_ */
