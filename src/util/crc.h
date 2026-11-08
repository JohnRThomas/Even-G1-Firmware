/*
 * CRC-32 and CRC-16 (crc.c).
 */
#ifndef G1_UTIL_CRC_H_
#define G1_UTIL_CRC_H_

#include <stdint.h>
#include <stdbool.h>

void crc32_init(void); /* builds the table; call once before crc32() */
uint32_t crc32(uint32_t crc, const uint8_t *buf, int len);
/* generic CRC-16 (init, poly, xorout, reflect in / out) */
uint16_t crc16_generic(uint16_t crc, uint16_t poly, uint16_t xorout, int refin, bool refout,
		       const uint8_t *data, int len);
uint16_t crc16_usb(uint16_t crc, const uint8_t *buf, int len);

#endif /* G1_UTIL_CRC_H_ */
