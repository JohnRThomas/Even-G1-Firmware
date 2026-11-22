/* SPI master (nrfx SPIM) used by the display panel (spim.c) */
#ifndef G1_SPIM_H_
#define G1_SPIM_H_

#include <glasses_state.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* spim.c */
void spi_master_register_context(struct spi_master_ctx *ctx);

#endif /* G1_SPIM_H_ */
