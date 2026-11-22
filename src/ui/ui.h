/*
 * UI screen tasks (quick note, Even AI, bitmap, teleprompter, navigation,
 * translate, onboarding, raster height) - module private declarations.
 */
#ifndef G1_UI_H_
#define G1_UI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "../gui/gui.h"
#include "../main.h"
#include "g1_log.h"
#include "ui_event.h"

/* canvas origin (depends on the panel position settings) */
#define X0 gui_screen_x_offset()
#define Y0 gui_screen_y_offset()

/* state of the navigation page (struct nav_prv_data.state) */
enum nav_state {
  NAV_STATE_INIT = 0,          /* waiting for the first sync with the phone and for both maps */
  NAV_STATE_RUNNING = 1,       /* the vendor's NAVIGATION_RUNNING_STATUS */
  NAV_STATE_ABNORMAL_EXIT = 2, /* the phone stopped answering: "service lost" for 8 s, then the page closes */
  NAV_STATE_ARRIVED = 3,
};

/* navigation private data, shared by the navigation task and the navigation_*_display() functions */
struct nav_prv_data {
  uint32_t state;                  /* enum nav_state */
  uint8_t panoramic_shown;
  uint8_t overview_shown;
  uint8_t synced;                  /* init sync with the phone done */
  uint16_t hour;          /* last drawn clock */
  uint16_t minute;
  uint8_t arrived;        /* 1 arriving, 2 arrived */
  uint8_t arrived_cleared;
  int64_t timeout;        /* auto exit delay after arrival */
  int64_t arrived_ts;
  enum imu_attitude imu_action;    /* head up: panoramic map */
  uint8_t sync_cnt;       /* heart beat counter */
  int64_t ts;
};

/* ui_quicknote.c */
extern uint8_t quicknote_result_shown;
void dmic_record_start(void);
void quicknote_stop_record(void);
void quicknote_timeout_reset(void);
int ui_QuickNote_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);
int day_of_week(int year, int month, int day); /* day of week (Zeller) */
void draw_countdown_running_status(uint8_t **fb, void *arg, int a, int b, int c);
int draw_template_bitmap_page(struct jbd_panel_context *panel, void *arg);

/* ui_even_ai.c */
extern uint8_t even_ai_scrolling;
int ui_even_ai_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);

/* ui_bitmap.c */
int ui_bitmap_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);
void show_not_disturb_bitmap(uint8_t mode);
int sync_not_disturb_bitmap(uint8_t mode);
void request_not_disturb_bitmap(uint8_t mode);

/* ui_teleprompter.c */
void teleprompter_state_reset(void);
int ui_teleprompter_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);
int draw_template_teleprompter_page(struct jbd_panel_context *panel, void *arg);

/* ui_navigation_map.c */
extern struct nav_prv_data nav_prv;
extern uint8_t nav_panoramic_drawn;
extern uint8_t nav_overview_decoded;
extern uint8_t nav_map_bitmaps[2][0x908];   /* navigation bitmaps (overview / arrow) */
int nav_prv_reset(void);
void navigation_direction_img_display(int direction);
void navigation_overview_map_display(const uint8_t *nav);
void navigation_panoramic_map_display(const uint8_t *nav);
int draw_template_navigation_page(struct jbd_panel_context *panel, void *arg);
void nav_draw_service_lost(void);
void nav_draw_route_generating(void);
int ui_navigation_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);

/* ui_translate.c */
int draw_template_translate_screen(struct jbd_panel_context *panel, void *arg);
void translate_draw_hint_page(int which);
int ui_translate_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);

/* ui_onboarding.c */
extern uint8_t onboarding_shown_sub_step;             /* onboarding sub-step shown (slave_display_thread) */
int init_onboarding_info(void);
void onboarding_request_redraw(void);
void onboarding_draw_static_page(void);
void onboarding_draw_animated_page(void);
void onboarding_check_step_finished(void);
int ui_onboarding_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);
struct ble_req_hdr;
int onboarding_ble_process(struct GlassesState *gs, const struct ble_req_hdr *hdr, const uint8_t *req, uint8_t *rsp);
int onboarding_sync_data(const uint8_t *data, uint32_t len);
void onboarding_deactivate(void);

/* ui_raster_height.c */
void cal_panel_canvas_coord(int *horizontal_offset, int *vertical_offset);
int ui_raster_height_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);

#endif /* G1_UI_H_ */
