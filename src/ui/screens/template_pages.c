/*
 * Template pages: one static page per screen id, drawn through the panel's
 * default task callback (draw_template_page) when a screen is shown without its
 * own task, and the gyro read-out page.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "ui/screens/ui.h"
#include "ui/screens/template_pages.h"
#include "drivers/display/panel.h"
#include "services/notifications/notify.h"
#include "ui/gui/fonts/font_bitmap.h"
#include "ui/gui/gui.h"
#include "ui/screens/dashboard.h"
#include "ui/screens/demo_image.h"
#include "util/common_utils.h"
#include "util/utf8.h"
#include "ui/screens/ui_new_message.h"
#include "ui/screens/ui_bitmap.h"
#include "ui/screens/ui_teleprompter.h"
#include "ui/screens/ui_navigation_map.h"
#include "ui/screens/ui_translate.h"
#include "ui/screens/ui_notification.h"
#include "ui/screens/ui_countdown.h"
#include "drivers/display/panel_fb.h"

typedef int (*ui_task_fn)(struct jbd_panel_context *panel, void *arg);
typedef void (*ui_pre_fn)(void *arg);

struct ui_task {
	uint32_t id;
	uint32_t has_pre;
	ui_task_fn task;
	ui_pre_fn pre;
};

void dump_template_gyro_info(void *arg)
{
	struct GlassesState *state = get_dashboard_state();
	double yaw = state->ui.ypr.yaw;
	double pitch = state->ui.ypr.pitch;
	double roll = state->ui.ypr.roll;

	G1_LOG_INF("screen_id: %d, ypr: %.2f, %.2f, %.2f\n", ((uint8_t *)arg)[1], yaw, pitch, roll);
}

int draw_template_gyro_page(struct jbd_panel_context *panel, void *arg)
{
	uint8_t **fb = panel->panel_buffer;
	uint16_t count;
	char buf[10] = {0};
	uint16_t *codes;
	struct GlassesState *state;
	float ypr[3];
	int n;

	codes = utf8_to_unicode_buf("陀螺仪姿态解算:YPR", &count);
	for (int i = 0; i < count; i++) {
		copy_to_frame_buffer_raw(fb, find_chinese_bitmap_by_unicode(codes[i], 12, 36), 12,
					 36, i * 12 + 6, 0);
	}
	state = get_dashboard_state();
	memcpy(ypr, &state->ui.ypr, sizeof(ypr));
	for (int k = 0; k < 3; k++) {
		n = format_float_2dp(ypr[k], buf);
		for (int i = 0; i < n; i++) {
			copy_to_frame_buffer_raw(fb, find_chinese_bitmap_by_unicode(buf[i], 12, 36),
						 12, 36, i * 12 + 6, (k + 1) * 30);
		}
	}
	return 0;
}

static const struct ui_task ui_task_table[] = {
	{13, 0, draw_template_gyro_page, dump_template_gyro_info},
	{2, 0, draw_template_countdown_page, NULL},
	{3, 0, draw_template_dashboard_page, NULL},
	{4, 0, draw_template_notification_page, NULL},
	{5, 0, draw_template_new_message_page, NULL},
	{6, 0, draw_template_dashboard_page, NULL},
	{7, 0, draw_template_bitmap_page, NULL},
	{8, 0, draw_template_schedule_page, NULL},
	{9, 0, draw_template_teleprompter_page, NULL},
	{10, 0, draw_template_navigation_page, NULL},
	{11, 0, draw_template_translate_screen, NULL},
};

int draw_template_page(struct jbd_panel_context *panel, int idx, void *arg)
{
	const struct ui_task *t;

	if (!CONTAINER_OF(panel, struct GlassesState, panel)->panel_on) {
		return -1;
	}
	panel->current_row = 0;
	panel->current_column = 176;
	if (idx != 7) {
		/* called without an argument (clear_cb takes the panel) */
		((void (*)(void))panel->clear_cb)();
	}
	if (idx >= 1 && idx <= 20) {
		t = ui_task_table + idx - 1;
		if (t->has_pre && t->pre) {
			t->pre(arg);
		}
		t->task(panel, arg);
	}
	if (panel->init_done) {
		panel->draw_image_cb(panel);
	}
	return 0;
}
