/*
 * Declarations of ui_navigation_map.c.
 */
#ifndef G1_UI_SCREENS_UI_NAVIGATION_MAP_H_
#define G1_UI_SCREENS_UI_NAVIGATION_MAP_H_

#include <zephyr/kernel.h>
#include "ui/screens/ui.h"

/* navigation private data, shared by the navigation task and the navigation_*_display() functions
 */
struct nav_prv_data {
	uint32_t state;                  /* 0 init, 1 running, 2 abnormal exit, 3 arrived */
	uint8_t panoramic_shown;
	uint8_t overview_shown;
	uint8_t synced;                  /* init sync with the phone done */
	uint16_t hour;          /* last drawn clock */
	uint16_t minute;
	uint8_t arrived;        /* 1 arriving, 2 arrived */
	uint8_t arrived_cleared;
	int64_t timeout;        /* auto exit delay after arrival */
	int64_t arrived_ts;
	uint8_t imu_action;              /* 2 = panoramic map */
	uint8_t sync_cnt;       /* heart beat counter */
	int64_t ts;
};

#define PANORAMIC_MAP_SIZE 0x40d0

/* navigation data received from the phone, filled by the BLE PUT handlers */
extern struct k_mutex nav_map_mutex; /* guards the map buffers */
extern int nav_app_sync_count;       /* phone heart beat seen */
extern uint8_t nav_pos_updated;
extern uint8_t nav_panoramic_map_updated;
extern uint8_t nav_overview_map_updated;
extern uint8_t nav_text_updated;
extern uint8_t nav_panoramic_map_ready;
extern uint8_t nav_overview_map_ready;
extern uint8_t nav_overview_map_buf[0x1210];          /* overview map as received */
extern uint8_t panoramic_map_buf[PANORAMIC_MAP_SIZE]; /* panoramic map; the citywalk bitmaps
							 of the dashboard share it */
#define panoramic_map_mask (&panoramic_map_buf[0x2068]) /* panoramic map mask */
void nav_data_init(void);

extern struct nav_prv_data nav_prv;
extern uint8_t nav_panoramic_drawn;
extern uint8_t nav_overview_decoded;
extern uint8_t nav_map_bitmaps[2][0x908];   /* navigation bitmaps (overview / arrow) */
int nav_prv_reset(void);
void navigation_direction_img_display(int direction);
void navigation_overview_map_display(struct GlassesState *gs, const uint8_t *nav);
void navigation_panoramic_map_display(struct GlassesState *gs, const uint8_t *nav);
int draw_template_navigation_page(struct jbd_panel_context *panel, void *arg);
void nav_draw_service_lost(void);
void nav_draw_route_generating(void);
int ui_navigation_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_NAVIGATION_MAP_H_ */
