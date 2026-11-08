/*
 * Dashboard screen (DashBoard_Reflash) - module private declarations.
 */
#ifndef G1_DASHBOARD_H_
#define G1_DASHBOARD_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "system/logging.h"
#include "util/date.h"
#include "system/glasses_state.h"
#include "ui/gui/gui.h"
#include "ui/screens/ui.h"
#include "ui/screens/ui_navigation_map.h"

/*
 * Dashboard page private state (dashboard_prv), defined
 * in dashboard.c.
 */
struct dashboard_prv_data {
	uint8_t state;               /* 0 init, 1 running */
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
	uint8_t language;
	uint8_t display_mode;
	uint8_t pane_mode;
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

/* dashboard data received from the phone, filled by the BLE PUT handlers */
struct schedule_item {
	uint8_t valid;
	char title[0x41];
	char time[0x21];
	char location[0x41];
};

struct stock_item {
	uint8_t valid;
	uint8_t change_status;    /* 0 / 1 / 2 (dashboard trend arrow) */
	uint16_t image_len;
	char code[0x20];
	char company[0x40];
	char price[0x10];
	char price_change[0x10];
	char change_rate[0x10];
	uint8_t image[0x75e];
};

struct news_item {
	uint8_t valid;
	char source[0x40];
	char text[0x118];
};

extern struct stock_item stock_items[4];
extern struct news_item news_items[4];
extern struct schedule_item schedule_items[8];
extern uint8_t citywalk_canvas_valid; /* citywalk map valid */
/* inside panoramic_map_buf: citywalk map bitmap, arrow (wide layout), arrow (narrow layout) */
#define citywalk_canvas_bitmap (&panoramic_map_buf[0x1980])
#define citywalk_arrow_full    (&panoramic_map_buf[0x2d28])
#define citywalk_arrow_dual    (&panoramic_map_buf[0x3278])

/* dashboard.c */
extern uint8_t notes_index;
extern uint8_t stocks_index;
extern uint8_t news_index;
extern uint8_t calendar_index;
void dashboard_draw_add_note_hint(void);
void dashboard_draw_add_note_hint_wide(void);
void init_dashboard_info(void);
uint8_t getNotesIndex(void);
int setNotesIndex(int idx);
void cleanNotesIndex(void);
uint8_t get_pane_mode(void);
void update_dashboard_clock(struct GlassesState *gs, uint32_t *total_s);
uint32_t get_dashboard_tx_ctx(void);
int draw_template_dashboard_page(struct jbd_panel_context *panel, void *arg);
int draw_template_schedule_page(struct jbd_panel_context *panel, void *arg);
int power_info_battery_ok(void);
void DashBoard_Reflash(struct db_info *db, struct GlassesState *gs, int refresh,
		       uint8_t display_mode);
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
int ui_DashBoard_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_DASHBOARD_H_ */
