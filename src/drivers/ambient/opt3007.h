/*
 * OPT3007 ambient light sensor (opt3007.c): lux reading and the brightness level
 * derived from it.
 */
#ifndef G1_DRIVERS_AMBIENT_OPT3007_H_
#define G1_DRIVERS_AMBIENT_OPT3007_H_

#include <stdint.h>
#include "system/glasses_state.h"

uint8_t opt3007_get_brightness_level(struct opt3007_context *ctx);
int opt3007_read(int arg);
int opt3007_get_max_lux(void);
int opt3007_chip_init(struct opt3007_context *ctx);
void opt3007_chip_deinit(void);
void register_opt3007_context(struct opt3007_context *ctx);

#endif /* G1_DRIVERS_AMBIENT_OPT3007_H_ */
