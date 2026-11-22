/*
 * Dashboard screen (DashBoard_Reflash) - module private declarations.
 */
#ifndef G1_DASHBOARD_H_
#define G1_DASHBOARD_H_

#include <g1_date.h>
#include <g1_log.h>
#include <glasses_state.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ui_event.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define GUI_SCREEN_WIDTH 640
#define GUI_FB_ROW_BYTES 320

/*
 * Dashboard page private state (dashboard_prv), defined
 * in dashboard.c.
 */
struct dashboard_prv_data {
  enum ui_page_state state;
  uint8_t notes_index;
  uint8_t notes_count;
  uint8_t stocks_index;
  uint8_t stocks_count;
  uint8_t news_index;
  uint8_t news_count;
  uint8_t calendar_last_index;
  uint8_t calendar_index;
  uint8_t calendar_count;      /* calendar pages */
  uint8_t notes_shown;
  uint8_t news_shown;
  uint8_t init_done;
  uint8_t power_info_sent;
  int64_t enter_ts;
  enum language language;      /* language of the "add note" hint on the screen */
  uint8_t display_mode;
  enum dashboard_pane pane_mode;
  uint32_t ble_connected;
  uint8_t calendar_disp_mode;
  uint8_t stocks_disp_mode;
  uint8_t news_disp_mode;
  uint8_t citywalk_disp_mode;
  uint8_t screen_cleared;      /* cleared by set_screen_cleared() */
  uint8_t msg_count;
};
extern struct dashboard_prv_data dashboard_prv;

/* every dashboard coordinate is written relative to the screen origin */
#define DB_X(n) (gui_screen_x_offset() + (n))
#define DB_Y(n) (gui_screen_y_offset() + (n))

/* dashboard.c */
extern uint8_t notes_index;
extern uint8_t stocks_index;
extern uint8_t news_index;
extern uint8_t calendar_index;
int ui_new_message_come_on_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);
void dashboard_draw_add_note_hint(void);
void dashboard_draw_add_note_hint_wide(void);
void init_dashboard_info(void);
uint8_t getNotesIndex(void);
int setNotesIndex(int idx);
void cleanNotesIndex(void);
enum dashboard_pane get_pane_mode(void);
void update_dashboard_clock(struct GlassesState *gs, uint32_t *total_s);
uint32_t get_dashboard_tx_ctx(void);
int draw_template_dashboard_page(struct jbd_panel_context *panel, void *arg);
int draw_template_schedule_page(struct jbd_panel_context *panel, void *arg);
int power_info_battery_ok(void);
void DashBoard_Reflash(struct db_info *db, const struct GlassesState *gs, enum ui_event event, uint8_t display_mode);
uint8_t getStocksIndex(void);
int setStocksIndex(int idx);
void cleanStocksIndex(void);
uint8_t getNewsIndex(void);
int setNewsIndex(int idx);
void cleanNewsIndex(void);
uint8_t getCalenadrIndex(void);
int setCalenadrIndex(int idx);
void cleanCalenadrIndex(void);
void set_screen_cleared(void);
int send_message_count_to_app(void);
int sync_dashboard_key_event_to_app(void);
int ui_DashBoard_task(struct jbd_panel_context *panel, void *arg, enum ui_event event);

#endif /* G1_DASHBOARD_H_ */
