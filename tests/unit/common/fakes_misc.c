#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <string.h>
#include <zephyr/random/rand32.h>

#include "fakes_misc.h"
#include "harness.h"

#include "gui/gui.h"
#include "imu/imu_fusion.h"
#include "main.h"
#include "nfc/nfc_wlc.h"
#include "panel_display/panel.h"
#include "production_test/production_test.h"
#include "sensors_audio/sensors_audio.h"
#include "storage/storage.h"
#include "threads/threads.h"
#include "ui/ui.h"

DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_level);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_level_flag);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_pt_test_mode);
DEFINE_FAKE_VOID_FUNC(set_test_mode, uint8_t);

DEFINE_FAKE_VOID_FUNC(system_reboot_delayed);
DEFINE_FAKE_VALUE_FUNC(uint8_t, get_wear_state_event);
DEFINE_FAKE_VALUE_FUNC(int, trigger_imu_state_change, void *, struct GlassesState *, bool);
__weak uint8_t last_ble_cmd = 0xff;
__weak uint8_t msg_array_busy;
__weak uint8_t deferred_text_ack_cmd;
__weak uint8_t deferred_text_ack_idx;
__weak uint8_t deferred_text_ack_total;

DEFINE_FAKE_VALUE_FUNC(uint8_t, is_master);
DEFINE_FAKE_VALUE_FUNC(int, change_work_mode, int);
DEFINE_FAKE_VOID_FUNC(setEraseDFU, bool);
DEFINE_FAKE_VOID_FUNC(deinit_ncs_memory);
DEFINE_FAKE_VOID_FUNC(save_dashboard_ts_to_ncs_mem);
DEFINE_FAKE_VOID_FUNC(set_ncs_mem_initialized);
DEFINE_FAKE_VALUE_FUNC(int, set_shipmode, int);
__weak char net_build_time[32];

DEFINE_FAKE_VALUE_FUNC(int, opt3007_get_max_lux);
DEFINE_FAKE_VALUE_FUNC(int, is_in_box);
DEFINE_FAKE_VALUE_FUNC(uint32_t, chargerGetVdcRect);
__weak uint32_t imu_pitch_x100;
__weak atomic_t dashboard_head_up_opened;

DEFINE_FAKE_VOID_FUNC(set_brightness_to_panel_reg_in_running, uint8_t);
__weak uint32_t JDB_PANEL_ID;

DEFINE_FAKE_VALUE_FUNC(const char *, get_string, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, day_of_week, int, int, int);

DEFINE_FAKE_VALUE_FUNC(uint32_t, z_impl_sys_rand32_get);

/* harness reset zeroes it */
__weak volatile struct app_setting CURRENT_LANGUAGE;

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_panel_set_brightness, struct jbd_panel_context *, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_spi_init, struct spi_master_ctx *);
DEFINE_FAKE_VOID_FUNC(g1_fake_switch_to_dfu, struct k_sem *);
DEFINE_FAKE_VOID_FUNC(g1_fake_flash_ops_fn, struct flash_ops *);

static uint8_t is_master_default(void)
{
	return get_dashboard_state()->is_master;
}

static const char *get_string_default(uint8_t id)
{
	ARG_UNUSED(id);
	return "";
}

static int panel_set_brightness_default(struct jbd_panel_context *c, uint8_t level)
{
	if (c->panel_brightness_level != level) {
		c->panel_brightness_level = level;
	}
	return level;
}

static void fakes_misc_reset(bool keep_media)
{
	struct GlassesState *gs = get_dashboard_state();

	ARG_UNUSED(keep_media);

	RESET_FAKE(get_pt_level);
	RESET_FAKE(get_pt_level_flag);
	RESET_FAKE(get_pt_test_mode);
	RESET_FAKE(set_test_mode);

	RESET_FAKE(system_reboot_delayed);
	RESET_FAKE(get_wear_state_event);
	RESET_FAKE(trigger_imu_state_change);
	last_ble_cmd = 0xff;
	msg_array_busy = 0;
	deferred_text_ack_cmd = 0;
	deferred_text_ack_idx = 0;
	deferred_text_ack_total = 0;

	RESET_FAKE(is_master);
	is_master_fake.custom_fake = is_master_default;
	RESET_FAKE(change_work_mode);
	RESET_FAKE(setEraseDFU);
	RESET_FAKE(deinit_ncs_memory);
	RESET_FAKE(save_dashboard_ts_to_ncs_mem);
	RESET_FAKE(set_ncs_mem_initialized);
	RESET_FAKE(set_shipmode);
	memset(net_build_time, 0, sizeof(net_build_time));

	RESET_FAKE(opt3007_get_max_lux);
	RESET_FAKE(is_in_box);
	RESET_FAKE(chargerGetVdcRect);
	imu_pitch_x100 = 0;
	atomic_set(&dashboard_head_up_opened, 0);

	RESET_FAKE(set_brightness_to_panel_reg_in_running);
	JDB_PANEL_ID = 0;

	RESET_FAKE(get_string);
	get_string_fake.custom_fake = get_string_default;
	RESET_FAKE(day_of_week);
	RESET_FAKE(z_impl_sys_rand32_get);

	RESET_FAKE(g1_fake_panel_set_brightness);
	g1_fake_panel_set_brightness_fake.custom_fake = panel_set_brightness_default;
	RESET_FAKE(g1_fake_spi_init);
	RESET_FAKE(g1_fake_switch_to_dfu);
	RESET_FAKE(g1_fake_flash_ops_fn);

	/* as init_context() registers them */
	gs->panel.set_brightness_cb = g1_fake_panel_set_brightness;
	gs->spi.init_cb = g1_fake_spi_init;
}

G1_UNIT_RESET_HOOK(fakes_misc_reset);
