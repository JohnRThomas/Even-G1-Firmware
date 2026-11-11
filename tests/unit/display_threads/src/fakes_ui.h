/* UI fakes; every page call is also logged to g1_ui_calls in order. */
#ifndef G1_UNIT_DISPLAY_THREADS_FAKES_UI_H_
#define G1_UNIT_DISPLAY_THREADS_FAKES_UI_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#define G1_UI_PAGE_FAKES(X)                                                                        \
	X(ui_demo_image_task)                                                                      \
	X(ui_bitmap_task)                                                                          \
	X(ui_teleprompter_task)                                                                    \
	X(ui_DashBoard_task)                                                                       \
	X(ui_QuickNote_task)                                                                       \
	X(ui_translate_task)                                                                       \
	X(ui_even_ai_task)                                                                         \
	X(ui_even_ai_v2_info_task)                                                                 \
	X(ui_navigation_task)                                                                      \
	X(ui_ancs_notificaton_task)                                                                \
	X(ui_new_message_come_on_task)                                                             \
	X(ui_onboarding_task)                                                                      \
	X(ui_raster_height_task)                                                                   \
	X(ui_set_imu_pitch_task)                                                                   \
	X(ui_prompt_info_task)                                                                     \
	X(ui_transcribe_info_task)

#define G1_UI_PAGE_DECLARE(name)                                                                   \
	DECLARE_FAKE_VALUE_FUNC(int, name, struct jbd_panel_context *, void *, int);
G1_UI_PAGE_FAKES(G1_UI_PAGE_DECLARE)

DECLARE_FAKE_VALUE_FUNC(int, g1_fake_default_task, struct jbd_panel_context *, int, void *);

DECLARE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, set_frame_buffer, uint8_t **);
DECLARE_FAKE_VOID_FUNC(set_hide_icon_on_exit);

DECLARE_FAKE_VALUE_FUNC(int, upgradeAppLanguageInfoToFlash, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, upgradeDashboardStartupModeInfoToFlash, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, upgradeDoubleTapCustomizeToFlash, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, upgradeLongpressTapCustomizeToFlash, uint32_t);

DECLARE_FAKE_VALUE_FUNC(atomic_val_t, setAudioBlockInUse, atomic_val_t);
DECLARE_FAKE_VALUE_FUNC(atomic_val_t, clearAudioBlockInUse);

extern atomic_t dashboard_flags;

#define G1_UI_CALLS_MAX 64

struct g1_ui_call {
	const char *page;
	int event;
	void *panel;
	void *arg;
	int64_t uptime_ms;
};

extern struct g1_ui_call g1_ui_calls[G1_UI_CALLS_MAX];
extern int g1_ui_call_count;

void g1_ui_calls_clear(void);
/* event < 0: any event */
int g1_ui_calls_count(const char *page, int event);

#endif /* G1_UNIT_DISPLAY_THREADS_FAKES_UI_H_ */
