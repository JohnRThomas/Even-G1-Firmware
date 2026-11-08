/*
 * "New message" screen: the incoming-notification hint shown over the dashboard
 * and its template page.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include "ui/screens/ui.h"
#include "ui/screens/ui_new_message.h"
#include "system/logging.h"
#include "comm/esb/esb.h"
#include "drivers/display/panel.h"
#include "services/notifications/notify.h"
#include "services/tasks/tasks.h"
#include "ui/display_thread.h"
#include "ui/gui/gui.h"
#include "ui/screen_task.h"
#include "ui/screens/dashboard.h"
#include "drivers/display/jbd_panel_flush.h"

static uint8_t new_msg_drawn;
static uint8_t new_msg_entered;
int ui_new_message_come_on_task(struct jbd_panel_context *panel, void *arg, int event)
{
	struct GlassesState *state = get_dashboard_state();
	uint8_t type;

	if (get_msg_show_state() == 4) {
		return 0;
	}
	if (event == 2) {
		BLE_PRINTK("ui_new_message_come_on_task exit !");
		gui_screen_clear();
		new_msg_drawn = 0;
		new_msg_entered = 0;
		return 0;
	}
	if (event == 0) {
		return 0;
	}
	if (new_msg_drawn == 0) {
		new_msg_drawn = 1;
		set_frame_buffer(panel->panel_buffer);
		set_showing_notification_on_gui();
		gui_screen_clear();
		new_msg_entered = 0;
	}
	if (state->ui.db.new_msg_count < 2) {
		new_msg_entered = 0;
		gui_screen_clear();
		return 0;
	}
	type = parse_message_type((uint8_t)(get_dashboard_state()->unread_msg_num - 1));
	BLE_PRINTK("new msgType is %d \n", type);
	if (new_msg_entered) {
		return 0;
	}
	clear_showing_notification_on_gui();
	for (int step = 0; step < 8; step++) {
		switch (type) {
		case 1:
			gui_bmp_bitmap_draw(0x37, gui_screen_x_offset(), gui_screen_y_offset() + 58,
					    0, 0, 0);
			break;
		case 2:
			gui_bmp_bitmap_draw(0x36, gui_screen_x_offset(), gui_screen_y_offset() + 58,
					    0, 0, 0);
			break;
		case 3:
			gui_bmp_bitmap_draw(0x3a, gui_screen_x_offset(), gui_screen_y_offset() + 58,
					    0, 0, 0);
			break;
		case 4:
			gui_bmp_bitmap_draw(0x39, gui_screen_x_offset(), gui_screen_y_offset() + 58,
					    0, 0, 0);
			break;
		default:
			gui_bmp_bitmap_draw(0x38, gui_screen_x_offset(), gui_screen_y_offset() + 58,
					    0, 0, 0);
			break;
		}
		for (int y = 0; y < 199; y++) {
			for (int x = 0; x < 320; x++) {
				uint8_t level[8] = {1, 2, 3, 4, 5, 6, 7, 7};

				if (panel->panel_buffer[y][x]) {
					panel->panel_buffer[y][x] &=
						gui_fade_mask_at(y % 26, level[step], x);
				}
			}
		}
		reflash_fb_data_to_lcd(get_dashboard_state()->panel.current_row,
				       get_dashboard_state()->panel.current_column, 0, 0, 640, 199);
	}
	set_showing_notification_on_gui();
	new_msg_entered = 1;
	return 0;
}

int draw_template_new_message_page(struct jbd_panel_context *panel, void *arg)
{
	return 0;
}
