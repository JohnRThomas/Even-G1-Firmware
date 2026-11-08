/*
 * JBD013VGA micro-LED panel layer: panel context callbacks and brightness
 * (the frame buffer clear is in panel_fb.c, the flush in jbd_panel_flush.c).
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "drivers/display/panel.h"
#include "drivers/display/jbd_panel.h"
#include "ui/screens/ui.h"
#include "main.h"
#include "services/notifications/notify.h"
#include "system/power.h"
#include "drivers/ambient/opt3007.h"
#include "ui/screens/template_pages.h"
#include "drivers/display/panel_fb.h"
#include "drivers/display/jbd_panel_flush.h"
#include "drivers/display/jbd013_cmd.h"
#include "drivers/display/jbd013_spi.h"
#include "drivers/pmic/npm1300.h"

static int JBD_LUM_REG_VAL;
static int JBD_CUR_REG_VAL;

/* the SPI interface of the GlassesState holding the panel context */
#define PANEL_SPI(c) (&CONTAINER_OF(c, struct GlassesState, panel)->spi)

static int panel_off(struct jbd_panel_context *c);
static int panel_on(struct jbd_panel_context *c);

static int panel_set_brightness_level(struct jbd_panel_context *c, uint8_t level)
{
	if (c->panel_brightness_level != level) {
		c->panel_brightness_level = level;
		G1_LOG_INF("new: %d\n", level);
	}
	return level;
}

void set_brightness_to_panel_reg(uint8_t level)
{
	uint16_t lum;
	uint8_t cur;

	G1_LOG_INF("set brightness level = %d\n", level);
	update_brightness_level_for_jdb_panel(level, &lum, &cur);
	send_cmd(SPI_WR_ENABLE);
	send_cmd(SPI_DISPLAY_DISABLE);
	wr_lum_reg(lum);
	wr_cur_reg(cur);
	wr_status_reg(SPI_WR_STATUS_REG2, 0x04);
	send_cmd(SPI_DISPLAY_ENABLE);
	send_cmd(SPI_SYNC);
	delay_ms(1);
}

void wr_lum_and_cur_reg(uint16_t lum_value, uint8_t cur_value)
{
	if (lum_value != JBD_LUM_REG_VAL) {
		wr_lum_reg(lum_value);
		JBD_LUM_REG_VAL = lum_value;
	}
	if (cur_value != JBD_CUR_REG_VAL) {
		wr_cur_reg(cur_value);
		JBD_CUR_REG_VAL = cur_value;
	}
}

void set_brightness_to_panel_reg_in_running(uint8_t level)
{
	uint16_t lum;
	uint8_t cur;

	G1_LOG_INF("set brightness level = %d\n", level);
	if (!get_dashboard_state()->panel.init_done) {
		return;
	}
	update_brightness_level_for_jdb_panel(level, &lum, &cur);
	send_cmd(SPI_WR_ENABLE);
	send_cmd(SPI_DISPLAY_DISABLE);
	wr_lum_and_cur_reg(lum, cur);
	wr_status_reg(SPI_WR_STATUS_REG2, 0x04);
	send_cmd(SPI_DISPLAY_ENABLE);
	send_cmd(SPI_SYNC);
	delay_ms(1);
}

static int panel_suspend(struct jbd_panel_context *c)
{
	c->spi_callbacks = PANEL_SPI(c);
	G1_LOG_INF("panel_suspend enter!\n");
	jbd_panel_suspend();
	panel_off(c);
	c->init_done = 0;
	return 0;
}

int panel_off(struct jbd_panel_context *c)
{
	c->spi_callbacks = PANEL_SPI(c);
	if (PANEL_SPI(c)->is_initialized) {
		c->init_done = 0;
		G1_LOG_INF("panel_suspend enter!\n");
		if (getDeviceRole() == 0) {
			opt3007_chip_deinit();
		}
		PANEL_SPI(c)->deinit_cb(c->spi_callbacks);
		power_down_panel();
	}
	return 0;
}

static int panel_resume(struct jbd_panel_context *c)
{
	c->spi_callbacks = PANEL_SPI(c);
	if (!(CONTAINER_OF(c, struct GlassesState, panel)->module_mask & LP_JBD_BIT)) {
		G1_LOG_ERR("panel_resume exit due to LP_JBD_BIT\n");
		return -1;
	}
	G1_LOG_INF("panel_resume enter!\n");
	if (c->init_done) {
		G1_LOG_ERR("panel_resume exit due to init_done\n");
		return -1;
	}
	if (c->spi_callbacks->is_initialized) {
		jbd_panel_resume(c);
		if (spi_read_id() != JBD_PANEL_DEVICE_ID) {
			G1_LOG_ERR("JBD PANEL init failure!\n");
		}
		c->init_done = 1;
	} else {
		panel_on(c);
		set_brightness_to_panel_reg(c->panel_brightness_level);
	}
	return 0;
}

int panel_on(struct jbd_panel_context *c)
{
	c->spi_callbacks = PANEL_SPI(c);
	if (!PANEL_SPI(c)->is_initialized) {
		G1_LOG_INF("panel_on enter!\n");
		power_for_panel();
		PANEL_SPI(c)->init_cb(c->spi_callbacks);
		panel_init();
		c->init_done = 1;
		if (getDeviceRole() == 0) {
			opt3007_chip_init(NULL);
		}
	}
	return 0;
}

static int jdb_panel_init(struct jbd_panel_context *c)
{
	c->spi_callbacks = PANEL_SPI(c);
	panel_init();
	set_brightness_to_panel_reg(c->panel_brightness_level);
	JDB_PANEL_ID = spi_read_id();
	if (JDB_PANEL_ID != JBD_PANEL_DEVICE_ID) {
		G1_LOG_ERR("JBD PANEL init failure!, device id: 0x%x\n", JDB_PANEL_ID);
		return -1;
	}
	G1_LOG_ERR("JBD PANEL init ok!, device id: 0x%x\n", JDB_PANEL_ID);
	c->init_done = 1;
	return 0;
}

void register_jdb_panel_context(struct jbd_panel_context *c)
{
	uint8_t *buf;
	int i;

	k_mutex_init(&lcd_mutex);
	c->init_cb = jdb_panel_init;
	c->suspend_cb = panel_suspend;
	c->resume_cb = panel_resume;
	c->off_cb = panel_off;
	c->on_cb = panel_on;
	c->default_task = draw_template_page;
	c->draw_image_cb = panel_draw_cb;
	c->clear_cb = clear_panel;
	c->set_brightness_cb = panel_set_brightness_level;
	/* 5 bytes command header in front of the frame buffer, 3 behind */
	buf = malloc(PANEL_FB_SIZE + 8);
	c->current_buffer = buf + 5;
	for (i = 0; i < PANEL_FB_ROWS; i++) {
		c->panel_buffer[i] = buf + 5 + i * PANEL_FB_STRIDE;
	}
	c->current_buffer_size = PANEL_FB_SIZE;
}
