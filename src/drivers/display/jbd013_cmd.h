/*
 * Declarations of jbd013_cmd.c.
 */
#ifndef G1_DRIVERS_PANEL_JBD013_CMD_H_
#define G1_DRIVERS_PANEL_JBD013_CMD_H_

#include "drivers/display/panel.h"

void send_cmd(uint8_t cmd);
void wr_status_reg(uint8_t reg, uint8_t data);
void wr_offset_reg(uint8_t row, uint8_t col);
void wr_cur_reg(uint8_t param);
void wr_lum_reg(uint16_t param);
void clr_cache(uint8_t val);
void jbd_clear_cache_sync(uint8_t val);
void display_image(uint32_t col, uint32_t row, uint8_t *buf, int len);
void panel_rst(void);
void panel_init(void);

#endif /* G1_DRIVERS_PANEL_JBD013_CMD_H_ */
