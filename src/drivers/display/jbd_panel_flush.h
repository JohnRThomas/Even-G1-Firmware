/*
 * Declarations of jbd_panel_flush.c.
 */
#ifndef G1_DRIVERS_PANEL_JBD_PANEL_FLUSH_H_
#define G1_DRIVERS_PANEL_JBD_PANEL_FLUSH_H_

#include "drivers/display/panel.h"

extern uint32_t JDB_PANEL_ID;
extern struct k_mutex lcd_mutex;
int reflash_fb_data_to_lcd(uint32_t row, uint32_t column, int x_start, int y_start, int x_end,
			   int y_end);
int reflash_fb_data_to_lcd_ex(uint32_t row, uint32_t column, int x_start, int y_start, int x_end,
			      int y_end, int offset);
int jbd_panel_suspend(void);
int jbd_panel_resume(struct jbd_panel_context *c);
void lock_lcd_mutex(k_timeout_t timeout);
int unlock_lcd_mutex(void);

#endif /* G1_DRIVERS_PANEL_JBD_PANEL_FLUSH_H_ */
