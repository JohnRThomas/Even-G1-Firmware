/*
 * Even Realities G1 - application core main.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdlib.h>

#define MODULE main
#include <caf/events/module_state_event.h>

#include "drivers/pmic/npm1300_charger.h"
#include "util/date.h"
#include "system/logging.h"
#include "main.h"
#include "system/power.h"
#include "system/retained_mem.h"
#include "system/work_mode.h"
#include "comm/esb/esb.h"
#include "comm/ipc/netcore_link.h"
#include "drivers/touch/aw9320x.h"
#include "comm/ble/ble.h"
#include "drivers/gpio/board_gpio.h"
#include "ui/screens/dashboard.h"
#include "comm/proxy_thread.h"
#include "services/imu/imu_fusion.h"
#include "comm/ipc/ipc_host.h"
#include "services/nfc_wlc/wlc.h"
#include "services/notifications/notify.h"
#include "drivers/display/panel.h"
#include "services/production_test/production_test.h"
#include "drivers/spi/spim.h"
#include "services/storage/storage.h"
#include "system/threads.h"
#include "ui/screens/ui.h"
#include "util/common_utils.h"
#include "system/clock.h"
#include "drivers/flash/ext_flash.h"
#include "drivers/ambient/opt3007.h"
#include "services/power/fuel_gauge.h"
#include "drivers/mic/dmic.h"
#include "services/power/charger_box.h"
#include "ui/screens/ui_onboarding.h"
#include "drivers/display/jbd_panel.h"
#include "services/storage/flash_settings.h"
#include "services/storage/settings_records.h"
#include "services/storage/setting_store.h"
#include "services/storage/quicknote_store.h"
#include "drivers/pmic/npm1300.h"

/* ------------------------------------------------------------------ */
/* globals                                                             */
/* ------------------------------------------------------------------ */
struct GlassesState *GLOBAL_STATE;
static int DEVICE_ROLE;
static uint8_t timer_count;

static void periodic_timer_expiry(struct k_timer *timer);

/* 60 s housekeeping timer */
static K_TIMER_DEFINE(periodic_timer, periodic_timer_expiry, NULL);

static void periodic_timer_expiry(struct k_timer *timer)
{
	if (get_pt_test_mode() == 1) {
		return;
	}
	timer_count++;
	if (timer_count > 9) {
		timer_count = 0;
		/* every 10 minutes: wake the storage thread, which has no handler for this
		 * command and just writes the settings that changed */
		GLOBAL_STATE->dashboard_cmd = 0x3e;
		k_sem_give(&GLOBAL_STATE->dashboard_position_sem);
	}
}

int getDeviceRole(void)
{
	return DEVICE_ROLE;
}

uint8_t is_master(void)
{
	return GLOBAL_STATE->is_master;
}

struct GlassesState *get_dashboard_state(void)
{
	return GLOBAL_STATE;
}

static void role_init(struct GlassesState *s)
{
	switch (DEVICE_ROLE) {
	case 0:
		s->is_master = 1;
		G1_LOG(0, "Master!------\n");
		break;
	case 1:
		s->is_master = 2;
		G1_LOG(0, "Slave!------\n");
		break;
	default:
		G1_LOG(0, "error --hw_id:0x%x\n", DEVICE_ROLE);
		break;
	}
	runtime_info_sync(s);

	switch (s->is_master) {
	case 1:
		s->master_fw_ver.major = FW_VER_MAJOR;
		s->master_fw_ver.minor = FW_VER_MINOR;
		s->master_fw_ver.patch = FW_VER_PATCH;
		s->sync.flags |= BIT(2);
		break;
	case 2:
		s->fw_ver.major = FW_VER_MAJOR;
		s->fw_ver.minor = FW_VER_MINOR;
		s->fw_ver.patch = FW_VER_PATCH;
		s->esb_pkt.flags |= BIT(2);
		break;
	default:
		break;
	}
}

static struct GlassesState *init_context(void)
{
	struct GlassesState *s;

	G1_LOG(1, "[cpuapp] global context init\n\n");

	s = malloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	memset(s->master_mac, 0xff, sizeof(s->master_mac));
	memset(s->slave_mac, 0xff, sizeof(s->slave_mac));
	GLOBAL_STATE = s;
	s->is_master = 1;

	k_sem_init(&s->ipc_sem, 0, 1);
	k_sem_init(&s->sem_1, 0, 10);
	k_sem_init(&s->sem_2, 0, 1);
	k_sem_init(&s->screen_on_sem, 0, 1);
	k_sem_init(&s->imu_sem, 0, 1);
	k_sem_init(&s->main_sem, 0, 1);
	k_sem_init(&s->dashboard_position_sem, 0, 1);
	k_sem_init(&s->sem_7, 0, 1);
	k_sem_init(&s->touch_sem, 0, 1);

	s->work_mode = 0xff;
	s->module_mask = 0;
	change_work_mode(0);

	s->dashboard_ts = malloc(sizeof(struct dashboard_ts_context));
	if (s->dashboard_ts) {
		memset(s->dashboard_ts, 0, sizeof(struct dashboard_ts_context));
	}
	set_date(1704067200); /* 2024-01-01 00:00:00 */

	s->countdown_ts = malloc(sizeof(struct countdown_ts_context));
	s->task_status = malloc(sizeof(struct msg_status));
	if (s->task_status) {
		s->task_status->task = 0;
		s->task_status->status = 0;
	}
	s->teleprompter = malloc(sizeof(struct teleprompter_info));
	if (s->teleprompter) {
		memset(s->teleprompter, 0, sizeof(struct teleprompter_info));
	}
	s->nav = malloc(sizeof(struct nav_info));
	s->translate_src = malloc(sizeof(struct translate_buf));
	s->translate_dst = malloc(sizeof(struct translate_buf));
	s->dash_buf = malloc(sizeof(struct dash_data_buf));
	s->evenai_buf = malloc(sizeof(struct evenai_buf));
	if (s->evenai_buf) {
		memset(s->evenai_buf, 0, sizeof(struct evenai_buf));
	}
	s->quicknote = malloc(sizeof(struct quicknote_ctx));
	if (s->quicknote) {
		memset(s->quicknote, 0, sizeof(struct quicknote_ctx));
	}
	s->quicknote_show_info = malloc(sizeof(struct quicknote_show_info));
	if (s->quicknote_show_info) {
		memset(s->quicknote_show_info, 0, sizeof(struct quicknote_show_info));
	}
	s->esb_up_snapshot = malloc(ESB_SNAPSHOT_SIZE);
	if (s->esb_up_snapshot) {
		memset(s->esb_up_snapshot, 0, ESB_SNAPSHOT_SIZE);
	}
	s->esb_down_snapshot = malloc(ESB_SNAPSHOT_SIZE);
	if (s->esb_down_snapshot) {
		memset(s->esb_down_snapshot, 0, ESB_SNAPSHOT_SIZE);
	}
	s->user_sched_info = malloc(sizeof(struct user_schedule_context));
	if (s->user_sched_info) {
		s->user_sched_info->f_168 = 0;
		s->user_sched_info->ts = 0;
	}
	s->schedule_notif = malloc(sizeof(struct schedule_notif));
	s->bitmap_info = malloc(sizeof(struct bitmap_show_info));
	if (s->bitmap_info) {
		memset(s->bitmap_info, 0, sizeof(struct bitmap_show_info));
	}
	s->onboarding = malloc(sizeof(struct onboarding_ctx));
	init_onboarding_info();
	s->raster_height_cfg = malloc(sizeof(uint8_t));
	if (s->raster_height_cfg) {
		*s->raster_height_cfg = 0;
	}
	s->horiz_pitch_cfg = malloc(HORIZ_PITCH_CFG_SIZE);
	if (s->horiz_pitch_cfg) {
		memset(s->horiz_pitch_cfg, 0, HORIZ_PITCH_CFG_SIZE);
	}
	s->ts_info = malloc(sizeof(struct prompt_info));
	if (s->ts_info) {
		s->ts_info->init.word_a = 0;
		s->ts_info->init.word_b = 0;
		s->ts_info->init.half_c = 0;
	}
	s->transcribe_buf = malloc(sizeof(struct transcribe_buf));
	if (s->transcribe_buf) {
		memset(s->transcribe_buf, 0, sizeof(struct transcribe_buf));
	}
	memset(&s->bonded_addr, 0xff, sizeof(s->bonded_addr));
	s->bond_flags = 0;

	register_ipc_service_context(&s->ipc_ctx);
	s->ipc_ep[0].id = 1;
	s->ipc_ep[0].name = "cpuapp-hw-id";
	s->ipc_ep[0].recv_cb = netcore_ctrl_recv;
	s->ipc_ctx.register_recv_cb(&s->ipc_ep[0]);
	s->ipc_ep[1].id = 6;
	s->ipc_ep[1].name = "cpunet-esbm-cpu-sync";
	s->ipc_ep[1].recv_cb = esb_master_recv;
	s->ipc_ep[2].id = 4;
	s->ipc_ep[2].name = "cpunet-esbs";
	s->ipc_ep[2].recv_cb = esb_slave_recv;
	s->ipc_ep[3].id = 5;
	s->ipc_ep[3].name = "cpunet-esbs-ctrl";
	s->ipc_ep[3].recv_cb = esb_ctrl_recv;
	s->ipc_ctx.register_recv_cb(&s->ipc_ep[1]);
	s->ipc_ctx.register_recv_cb(&s->ipc_ep[2]);
	s->ipc_ctx.register_recv_cb(&s->ipc_ep[3]);

	s->spi.is_initialized = 0;
	s->spi.pins[0] = 4;
	s->spi.pins[1] = 8;
	s->spi.pins[2] = 9;
	s->spi.pins[3] = 10;
	s->spi.ss = 11;
	s->spi.speed_mhz = 0x20;
	spi_master_register_context(&s->spi);

	DEVICE_ROLE = button_init();
	s->ext_flash_ok = ext_flash_api_init(&s->flash_read, &s->flash_write, &s->flash_erase);
	s->work_mode = 0;
	sett_init(&s->store);
	load_settings_from_flash(s);

	role_init(s);

	k_sem_take(&s->ipc_sem, K_FOREVER);
	if (s->work_mode == 1) {
		return s;
	}

	ble_context_init(&s->ble_context);
	if (s->is_master == 1) {
		k_timer_start(&periodic_timer, K_SECONDS(60), K_SECONDS(60));
		s->imu.dev = DEVICE_DT_GET(DT_NODELABEL(lsm6dso));
		register_imu_funsion_context(&s->imu);
		register_opt3007_context(&s->opt3007_context);
	} else if (s->is_master == 2) {
		s->imu.dev = DEVICE_DT_GET(DT_NODELABEL(lsm6dso));
	}
	register_jdb_panel_context(&s->panel);
	init_box_status();
	nfc_init();
	init_dashboard_info();
	getDashboardStartUpModeInfofromFlash();
	getAppLanguageInfofromFlash();
	clearSettingStorageQueue();
	clearQuickNoteQueue();
	s->user_data_reset_pending = 0;
	s->panel.brightness_max = s->panel.lum_gear;
	s->mic_nocompress = 0;
	G1_LOG(1, "exit!\n");
	return s;
}

static void component_init(struct GlassesState *s)
{
	const struct device *regulators = DEVICE_DT_GET(DT_NODELABEL(pmic_regulators));
	const struct device *charger = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));
	int ret;

	G1_LOG(1, "enter\n");
	get_settings_from_flash(s);
	ret = s->ble_context.init_cb(&s->ble_context);
	if (ret) {
		G1_LOG(1, "[ble_context init] fail: 0x%x\n\n", ret);
	}
	if (s->is_master == 1) {
		ret = s->imu.init_cb(&s->imu);
		if (ret) {
			G1_LOG(1, "[imu_context init] fail: 0x%x\n\n", ret);
		}
		ret = s->opt3007_context.init_cb(&s->opt3007_context);
		if (ret) {
			G1_LOG(1, "[opt3007 init] fail: 0x%x\n\n", ret);
		}
	}

	if (!device_is_ready(regulators)) {
		printk("pmic regulator not ready.\n");
		return;
	}
	printk("pmic regulator %s is ready to use.\n", regulators->name);
	if (!device_is_ready(charger)) {
		printk("Charger device not ready.\n");
		return;
	}
	if (fuel_gauge_init(charger) < 0) {
		printk("Could not initialise fuel gauge.\n");
		return;
	}
	if (npm1300_charger_init(charger) < 0) {
		printk("Could not initialise charger.\n");
		return;
	}
	printk("Initialise charger.\n");
	printk("PMIC device ok\n");
	dump_pmic_state();
	if (is_old_board()) {
		printk("Old board, need to restrain Vterm to 4.2V\n");
		change_termination_voltage();
	}
	power_for_panel();
	ret = s->spi.init_cb(&s->spi);
	if (ret) {
		G1_LOG(1, "[spi_master init] fail: 0x%x\n\n", ret);
	}
	ret = s->panel.init_cb(&s->panel);
	if (ret) {
		G1_LOG(1, "[panel_controler init] fail: 0x%x\n\n", ret);
	}
	aw93203_init();
	app_list_init();
	G1_LOG(1, "exit\n");
	s->aw_diff = 0;
	aw9320x_diff_get(&s->aw_diff);
	set_pt_level(NCS_MEM->pt_level);
}

int main(void)
{
	struct GlassesState *s;

	init_boot_seconds();
	module_set_state(MODULE_STATE_READY);
	G1_LOG(0, "Synchronization using %s driver\n", "mbox");
	rtc_sync_start();
	serialization_init();
	G1_LOG(1, "[Build Info: Ver:v%d.%d.%d Commit:%s] [Build Time:%s]\n", FW_VER_MAJOR,
	       FW_VER_MINOR, FW_VER_PATCH, G1_GIT_COMMIT, G1_BUILD_TIME);

	s = init_context();
	component_init(s);

	init_threads(s);
	k_msleep(2000);
	enable_charger();

	for (;;) {
		k_sem_take(&s->main_sem, K_FOREVER);
		if (runtime_mode == 1) {
			switch_to_dfu_mode(&s->main_sem);
		}
		while (s->pending) {
			if (s->pending & BIT(0)) {
				save_usr_settings(s, 0);
				s->pending &= ~BIT(0);
			} else if (s->pending & BIT(1)) {
				s->display_mode = 11;
				s->pending &= ~BIT(1);
			} else if (s->pending & BIT(2)) {
				dmic_stream_start();
				s->pending &= ~BIT(2);
			} else if (s->pending & BIT(3)) {
				erase_dfu_flash();
				s->pending &= ~BIT(3);
			}
		}
		k_msleep(1);
	}
	return 0;
}
