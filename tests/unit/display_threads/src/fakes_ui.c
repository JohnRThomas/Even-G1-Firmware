#include <string.h>

#include "fakes_ui.h"
#include "harness.h"

#include "dashboard/dashboard.h"
#include "gui/gui.h"
#include "notify/demo_image.h"
#include "notify/notify.h"
#include "panel_display/panel.h"
#include "sensors_audio/sensors_audio.h"
#include "storage/storage.h"
#include "threads/threads.h"
#include "ui/ui.h"

struct g1_ui_call g1_ui_calls[G1_UI_CALLS_MAX];
int g1_ui_call_count;

static void ui_log(const char *page, int event, void *panel, void *arg)
{
	/* Runs on the display thread: no zassert here. */
	if (g1_ui_call_count >= G1_UI_CALLS_MAX) {
		return;
	}
	g1_ui_calls[g1_ui_call_count].page = page;
	g1_ui_calls[g1_ui_call_count].event = event;
	g1_ui_calls[g1_ui_call_count].panel = panel;
	g1_ui_calls[g1_ui_call_count].arg = arg;
	g1_ui_calls[g1_ui_call_count].uptime_ms = k_uptime_get();
	g1_ui_call_count++;
}

void g1_ui_calls_clear(void)
{
	g1_ui_call_count = 0;
	memset(g1_ui_calls, 0, sizeof(g1_ui_calls));
}

int g1_ui_calls_count(const char *page, int event)
{
	int n = 0;

	for (int i = 0; i < g1_ui_call_count; i++) {
		if (strcmp(g1_ui_calls[i].page, page) == 0 &&
		    (event < 0 || g1_ui_calls[i].event == event)) {
			n++;
		}
	}
	return n;
}

#define G1_UI_PAGE_DEFINE(name)                                                                    \
	DEFINE_FAKE_VALUE_FUNC(int, name, struct jbd_panel_context *, void *, int);                \
	static int name##_default(struct jbd_panel_context *panel, void *arg, int event)           \
	{                                                                                          \
		ui_log(#name, event, panel, arg);                                                  \
		return name##_fake.return_val;                                                     \
	}
G1_UI_PAGE_FAKES(G1_UI_PAGE_DEFINE)

DEFINE_FAKE_VALUE_FUNC(int, g1_fake_default_task, struct jbd_panel_context *, int, void *);

static int default_task_default(struct jbd_panel_context *panel, int screen_id, void *ctx)
{
	ui_log("default_task", screen_id, panel, ctx);
	return g1_fake_default_task_fake.return_val;
}

DEFINE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, set_frame_buffer, uint8_t **);
DEFINE_FAKE_VOID_FUNC(set_hide_icon_on_exit);

DEFINE_FAKE_VALUE_FUNC(int, upgradeAppLanguageInfoToFlash, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, upgradeDashboardStartupModeInfoToFlash, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, upgradeDoubleTapCustomizeToFlash, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, upgradeLongpressTapCustomizeToFlash, uint32_t);

DEFINE_FAKE_VALUE_FUNC(atomic_val_t, setAudioBlockInUse, atomic_val_t);
DEFINE_FAKE_VALUE_FUNC(atomic_val_t, clearAudioBlockInUse);

atomic_t dashboard_flags;

#define G1_UI_PAGE_RESET(name)                                                                     \
	RESET_FAKE(name);                                                                          \
	name##_fake.custom_fake = name##_default;

static void fakes_ui_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	G1_UI_PAGE_FAKES(G1_UI_PAGE_RESET)
	RESET_FAKE(g1_fake_default_task);
	g1_fake_default_task_fake.custom_fake = default_task_default;
	g1_ui_calls_clear();

	RESET_FAKE(jbd_clear_cache_sync);
	RESET_FAKE(set_frame_buffer);
	RESET_FAKE(set_hide_icon_on_exit);

	RESET_FAKE(upgradeAppLanguageInfoToFlash);
	RESET_FAKE(upgradeDashboardStartupModeInfoToFlash);
	RESET_FAKE(upgradeDoubleTapCustomizeToFlash);
	RESET_FAKE(upgradeLongpressTapCustomizeToFlash);

	RESET_FAKE(setAudioBlockInUse);
	RESET_FAKE(clearAudioBlockInUse);

	atomic_set(&dashboard_flags, 0);

	get_dashboard_state()->panel.default_task = g1_fake_default_task;
}

G1_UNIT_RESET_HOOK(fakes_ui_reset);
