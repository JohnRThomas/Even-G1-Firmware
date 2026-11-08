/*
 * Thread start-up: creates the firmware's worker threads in the order the boot
 * sequence needs, by role and production-test mode.
 */
#include <zephyr/kernel.h>
#include "system/threads.h"
#include "system/monitor.h"
#include "system/watchdog.h"
#include "comm/esb/esb.h"
#include "comm/proxy_thread.h"
#include "services/nfc_wlc/nfc_thread.h"
#include "services/production_test/production_test.h"
#include "services/storage/storage.h"
#include "ui/display_thread.h"
#include "ui/input.h"
#include "ui/screen_dispatch.h"
#include "ui/screen_task.h"
#include "services/storage/file_storage.h"
#include "services/production_test/pt_aging.h"
#include "services/production_test/pt_mic.h"

void init_threads(struct GlassesState *state)
{
	screen_dispatch_init();
	start_flash_storage_threads(state);
	init_watchdog();
	monitor_start(state);
	if (!get_pt_test_mode()) {
		esb_sync_start(state);
	} else {
		start_aging_mode_thread();
		start_mic_transm_thread();
	}
	nfc_thread_start();
	if (state->is_master == 1) {
		screen_dispatch_start(state);
		screen_task_start(state);
		start_proxy_thread_handler(state);
		start_display_thread_handler(state);
	} else if (state->is_master == 2) {
		screen_dispatch_start(state);
		screen_task_start(state);
		esb_slave_start(state);
		start_display_thread_handler(state);
	}
	input_start(state);
}
