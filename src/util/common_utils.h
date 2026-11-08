/*
 * Small helpers (common_utils.c): number formatting, byte order, RLE decoding.
 */
#ifndef UTILS_COMMON_UTILS_H_
#define UTILS_COMMON_UTILS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include "system/glasses_state.h"

/* common_utils.c */
int format_float_2dp(float val, char *buf);
int uint32_to_little_endian(uint8_t *buf, uint32_t val);
int rle_decode(const uint8_t *src, uint32_t len, uint8_t *dst);

#endif
