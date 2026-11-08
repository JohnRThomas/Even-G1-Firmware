/*
 * Declarations of jbd013_spi.c.
 */
#ifndef G1_DRIVERS_PANEL_JBD013_SPI_H_
#define G1_DRIVERS_PANEL_JBD013_SPI_H_

#include "drivers/display/panel.h"

void delay_ms(uint32_t ms);
void spi_send_cmd(uint8_t cmd);
int spi_tx_data(const uint8_t *buf, uint32_t len);
void spi_tx_frame(uint8_t cmd, const uint8_t *param, uint32_t len);
void spi_wr_buffer(uint16_t col, uint16_t row, uint8_t *buf, uint32_t len);
uint16_t spi_read_id(void);

#endif /* G1_DRIVERS_PANEL_JBD013_SPI_H_ */
