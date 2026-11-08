/*
 * Production-test touch handling: click counting on the touch bar with the test
 * actions behind each count, and the silent (demo) mode toggled by a triple click.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include "services/production_test/production_test.h"
#include "services/production_test/pt_touch.h"
#include "system/logging.h"
#include "system/system_state.h"
#include "system/work_mode.h"
#include "drivers/gpio/board_gpio.h"
#include "drivers/touch/aw9320x.h"
#include "services/storage/storage.h"
#include "ui/display_thread.h"
#include "ui/gui/fonts/font_bitmap.h"
#include "ui/screens/demo_image.h"
#include "services/storage/settings_records.h"
#include "services/production_test/pt_aging.h"

static uint8_t normal_mode;
static void on_double_click(void)
{
	BLE_PRINTK(
		"#############################on_double_click################################\n");
	set_aging_mode(2);
}

void exit_silent_mode(void)
{
	if (demo_mode) {
		demo_mode = 0;
	}
	change_work_mode(2);
	global_system_resume(get_dashboard_state());
	get_dashboard_state()->imu.mode = 2;
	get_dashboard_state()->dashboard_mode = 3;
	display_reflash_screen(get_dashboard_state()->dashboard_mode);
	normal_mode = 1;
}

static void on_triple_click(void)
{
	BLE_PRINTK(
		"#############################on_triple_click################################\n");
	if (!normal_mode) {
		exit_silent_mode();
	} else {
		enter_silent_mode();
	}
}

void enter_silent_mode(void)
{
	get_dashboard_state()->imu.mode = 1;
	display_close_screen(3);
	get_dashboard_state()->dashboard_mode = 0;
	free_demo_line_buf();
	global_system_suspend(get_dashboard_state());
	normal_mode = 0;
}

void product_test_touch_key_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	uint32_t press_t = 0;
	uint32_t release_t = 0;
	uint32_t now, t1, t2;
	int clicks = 0;
	int mic_on = 0;
	int dur;

	for (;;) {
		k_sem_take(&state->touch_sem, K_MSEC(500));
		if (state->work_mode == 1 || state->work_mode == 8) {
			k_msleep(5000);
			continue;
		}
		if (!(state->module_mask & BIT(0))) {
			continue;
		}
		if (touch_irq_pending) {
			sys_clock_tick_get_32();
			aw9320x_get_irq_stat();
			sys_clock_tick_get_32();
			touch_irq_pending = 0;
		}
		switch (aw9320x_wearing) {
		case IN_EAR:
			now = sys_clock_tick_get_32();
			aw9320x_diff_get(&state->aw_diff);
			if ((int)(now - press_t) > 30000) {
				clicks = 0;
				release_t = 0;
			}
			press_t = now;
			clicks++;
			aw9320x_wearing = UNKNOWN;
			break;
		case OUT_EAR:
			release_t = sys_clock_tick_get_32();
			aw9320x_wearing = UNKNOWN;
			break;
		default:
			break;
		}
		t1 = sys_clock_tick_get_32();
		t2 = sys_clock_tick_get_32();
		if (clicks == 1 && !mic_on && release_t == 0 && (int)(t1 - press_t) > 30000) {
			printk("########################turn on the mic and start to speak! holdtime %d\n",
			       t1 - press_t);
			mic_on = 1;
		}
		if (clicks == 0) {
			continue;
		}
		if ((int)press_t >= (int)release_t || (int)(t2 - release_t) <= 10000) {
			continue;
		}
		if (get_aging_mode() != 1) {
			dur = release_t - press_t;
			if (dur > 150000) {
				printk("#############################long press################################\n");
				aw9320x_diff_get(&state->aw_diff);
				printk("aw9320x_diff_get: %d\n", state->aw_diff);
			} else if (dur > 90000) {
				printk("#############################short press################################\n");
			} else {
				switch (clicks) {
				case 1:
					if (mic_on) {
						printk("turn off mic now\n");
					} else {
						printk("#############################single click################################\n");
						printk("aw9320x_diff_get: %d\n", state->aw_diff);
					}
					break;
				case 2:
					on_double_click();
					break;
				case 3:
					on_triple_click();
					break;
				case 4:
					G1_LOG_ERR(
						"sys reboot because touch event, %d click, reboot now\n\n",
						4);
					k_msleep(500);
					sys_reboot(SYS_REBOOT_COLD);
				case 5:
					enter_aging_mode();
					break;
				case 10:
					reset_all_usr_data(state, 1);
					break;
				default:
					break;
				}
			}
		} else if (get_aging_phase() == 2 &&
			   get_dashboard_state()->aging_touch_count < 0xfa) {
			get_dashboard_state()->aging_touch_count++;
		}
		mic_on = 0;
		clicks = 0;
	}
}
