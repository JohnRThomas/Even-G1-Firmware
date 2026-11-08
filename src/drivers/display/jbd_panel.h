/*
 * Declarations of jbd_panel.c.
 */
#ifndef G1_DRIVERS_PANEL_JBD_PANEL_H_
#define G1_DRIVERS_PANEL_JBD_PANEL_H_

#include "drivers/display/panel.h"

void set_brightness_to_panel_reg(uint8_t level);
void wr_lum_and_cur_reg(uint16_t lum_value, uint8_t cur_value);
void set_brightness_to_panel_reg_in_running(uint8_t level);
void register_jdb_panel_context(struct jbd_panel_context *c);

#endif /* G1_DRIVERS_PANEL_JBD_PANEL_H_ */
