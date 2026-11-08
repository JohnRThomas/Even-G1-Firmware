/*
 * JBD panel frame buffer flush (reflash_fb_data_to_lcd / _ex), deep power
 * down / resume and the LCD mutex.
 */
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "drivers/display/panel.h"
#include "drivers/display/jbd_panel_flush.h"
#include "drivers/display/jbd_panel.h"
#include "drivers/display/panel_fb.h"
#include "drivers/display/jbd013_cmd.h"
#include "drivers/display/jbd013_spi.h"

uint32_t JDB_PANEL_ID;
struct k_mutex lcd_mutex;

int reflash_fb_data_to_lcd(uint32_t row, uint32_t column, int x_start, int y_start, int x_end,
			   int y_end)
{
	uint8_t rx[1] = {0};
	struct jbd_panel_context *c = get_jdb_panel_context();
	int ret;

	if (!c->init_done) {
		G1_LOG_ERR("c->init_done = 0,init failed, can't write data!\n");
		return -1;
	}
	if (y_end >= PANEL_FB_ROWS) {
		y_end = PANEL_FB_ROWS;
	}
	if (x_end >= PANEL_WIDTH) {
		x_end = PANEL_WIDTH;
	}
	for (; y_start <= y_end; y_start++) {
		uint8_t *p = c->current_buffer + y_start * PANEL_FB_STRIDE + x_start / 2 - 5;
		int len = x_end / 2 - x_start / 2 + 5;
		uint32_t addr = (((column + y_start) << 10) & 0x7fc00) | ((x_start + row) & 0x3ff);
		uint8_t backup[5];
		uint8_t tail;

		memcpy(backup, p, sizeof(backup));
		p[0] = SPI_WR_BUFFER;
		p[1] = (addr >> 16) & 0xff;
		p[2] = (addr >> 8) & 0xff;
		p[3] = addr & 0xff;
		p[4] = 0xff;
		tail = p[len];
		p[len] = 0;
		k_mutex_lock(&lcd_mutex, K_FOREVER);
		ret = c->spi_callbacks->xfer_cb(c->spi_callbacks, p, len, rx, 1);
		k_mutex_unlock(&lcd_mutex);
		if (ret) {
			G1_LOG_ERR("spi transfer error....\n");
		}
		p[len] = tail;
		memcpy(p, backup, sizeof(backup));
	}
	spi_send_cmd(SPI_SYNC);
	delay_ms(1);
	return 0;
}

int reflash_fb_data_to_lcd_ex(uint32_t row, uint32_t column, int x_start, int y_start, int x_end,
			      int y_end, int offset)
{
	uint8_t rx[1] = {0};
	struct jbd_panel_context *c = get_jdb_panel_context();
	int ret;

	if (!c->init_done) {
		G1_LOG_ERR("c->init_done = 0,init failed, can't write data!\n");
		return -1;
	}
	if (y_end >= PANEL_FB_ROWS) {
		y_end = PANEL_FB_ROWS;
	}
	if (x_end >= PANEL_WIDTH) {
		x_end = PANEL_WIDTH;
	}
	for (; y_start <= y_end; y_start++) {
		uint8_t *p =
			c->current_buffer + (y_start + offset) * PANEL_FB_STRIDE + x_start / 2 - 5;
		int len = x_end / 2 - x_start / 2 + 5;
		uint32_t addr = (((column + y_start) << 10) & 0x7fc00) | ((x_start + row) & 0x3ff);
		uint8_t backup[5];
		uint8_t tail;

		memcpy(backup, p, sizeof(backup));
		p[0] = SPI_WR_BUFFER;
		p[1] = (addr >> 16) & 0xff;
		p[2] = (addr >> 8) & 0xff;
		p[3] = addr & 0xff;
		p[4] = 0xff;
		tail = p[len];
		p[len] = 0;
		k_mutex_lock(&lcd_mutex, K_FOREVER);
		ret = c->spi_callbacks->xfer_cb(c->spi_callbacks, p, len, rx, 1);
		k_mutex_unlock(&lcd_mutex);
		if (ret) {
			G1_LOG_ERR("spi transfer error....\n");
		}
		p[len] = tail;
		memcpy(p, backup, sizeof(backup));
	}
	spi_send_cmd(SPI_SYNC);
	delay_ms(1);
	return 0;
}

int jbd_panel_suspend(void)
{
	uint8_t param = 0xff;

	G1_LOG_INF("SPI_DEEP_POWER_DOWN\n");
	spi_tx_frame(SPI_DEEP_POWER_DOWN, &param, 1);
	return 0;
}

int jbd_panel_resume(struct jbd_panel_context *c)
{
	G1_LOG_INF("release from deep power down\n");
	panel_init();
	set_brightness_to_panel_reg(c->panel_brightness_level);
	G1_LOG_INF("resume completed\n");
	return 0;
}

void lock_lcd_mutex(k_timeout_t timeout)
{
	k_mutex_lock(&lcd_mutex, timeout);
}

int unlock_lcd_mutex(void)
{
	return k_mutex_unlock(&lcd_mutex);
}
