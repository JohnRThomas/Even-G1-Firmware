/* SPI master (nrfx SPIM) used by the display panel (spim.c) */
#ifndef G1_DRIVERS_SPI_SPIM_H_
#define G1_DRIVERS_SPI_SPIM_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include "system/glasses_state.h"

/* spim.c */
void spi_master_register_context(struct spi_master_ctx *ctx);

#endif /* G1_DRIVERS_SPI_SPIM_H_ */
