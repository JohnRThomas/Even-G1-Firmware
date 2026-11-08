/*
 * BLE application protocol (phone <-> glasses): request dispatch, get/put
 * request handlers, pkcs7 packet reassembly, BLE debug log channel and the
 * BLE work thread.
 */
#ifndef G1_BLE_PROTO_H_
#define G1_BLE_PROTO_H_

#include <g1_date.h>
#include <glasses_state.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "g1_log.h"

#include "ble_cmd.h"

/* pkcs7 packet reassembly results */
#define PKCS7_PACK_DONE 0xc9 /* last packet received */
#define PKCS7_PACK_ERR  0xca
#define PKCS7_PACK_MORE 0xcb /* more packets follow */

/* destination buffers of parse_receiver_pack_pkcs7() and parse_receiver_msg_pack_pkcs7() */
#define PKCS7_PACK_BUF_SIZE     0x1800
#define PKCS7_MSG_PACK_BUF_SIZE 0x800

/* Usage-statistics record sent in reply to BLE_CMD_BURIAL_POINT_GET (always empty) */
#define USAGE_STATS_RECORD_SIZE    0x11c
#define USAGE_STATS_VERSION_OFFSET 0x14
#define USAGE_STATS_VALID_OFFSET   0x16
#define USAGE_STATS_VERSION        7

/* Debug log message queue (ble_printk -> phone) */
#define DEBUG_MSG_SIZE 200
#define DEBUG_MSGQ_MAX 20

/* dashboard lock bits (dashboard_lock_reported / dashboard_lock_requested / dashboard_head_up_opened) */
#define DASHBOARD_LOCK_BIT 1 /* bit number */

#define BLE_TX_BUF_SIZE    0x2b8
#define BLE_SMALL_PKT_MAX  20
#define BLE_SMALL_BUF_SIZE (BLE_SMALL_PKT_MAX + 1)

/* request header handed to ble_process_req_dispatch() */
struct ble_req_hdr {
  uint8_t cmd;
  uint8_t source;
  uint16_t len;
};

#define BLE_WORK_THREAD_STACK_SIZE 0x3000
#define BLE_WORK_THREAD_PRIORITY   (-15)

/* ---- types used by ble_process_put_req() ---- */

/* cmd 6 / sub 1: time sync */
struct put_time_sync {
  uint8_t hdr[5];
  uint32_t sec_ts;
  int64_t ms_ts;
} __packed;

/* dashboard schedule entry (schedule_items[8], also read by DashBoard_Reflash) */
struct schedule_item {
  uint8_t valid;
  char title[0x41];
  char time[0x21];
  char location[0x41];
};

/* dashboard stock entry (stock_items[4]) */
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

/* dashboard news entry (news_items[4]) */
struct news_item {
  uint8_t valid;
  char source[0x40];
  char text[0x118];
};

/* quick notes (GlassesState->quicknote) */
struct qn_title {
  char str[0x29];
};

#define PANORAMIC_MAP_SIZE 0x40d0

/* ---- globals defined in ble_proto.c ---- */
extern int tp_app_sync_count;                /* app sync request seen */
extern int nav_app_sync_count;                /* phone heart beat seen */
extern atomic_t dashboard_lock_reported;
extern atomic_t dashboard_lock_requested;
extern struct k_mutex nav_map_mutex;
extern uint16_t multi_packet_total; /* multi-packet: total packets */
extern uint16_t multi_packet_cnt;   /* multi-packet: packets received */
extern uint16_t multi_packet_len;   /* multi-packet: bytes received */
/* navigation (ble_process_put_req -> ui_navigation_map.c) */
extern uint8_t nav_pos_updated;
extern uint8_t nav_panoramic_map_updated;
extern uint8_t nav_overview_map_updated;
extern uint8_t nav_text_updated;
extern uint8_t nav_panoramic_map_ready;
extern uint8_t nav_overview_map_ready;
extern uint8_t tp_scroll_pending;            /* teleprompter text scrolled */
extern int debug_pending;                    /* ble_printk() woke the work thread */
extern uint8_t BIT_MAP_CHUNK_ID;             /* last file upload chunk accepted, 0xff before the first */
extern uint8_t nav_overview_map_buf[0x1210];    /* navigation overview map as received */
/* phone data buffers filled by ble_process_put_req (read by DashBoard_Reflash) */
extern struct stock_item stock_items[4];
extern struct news_item news_items[4];
extern struct schedule_item schedule_items[8];
extern uint8_t citywalk_canvas_valid;
extern uint8_t panoramic_map_buf[PANORAMIC_MAP_SIZE]; /* panoramic map / citywalk data */
/* inside panoramic_map_buf: citywalk map bitmap, arrow (wide layout), arrow (narrow layout) */
#define citywalk_canvas_bitmap (&panoramic_map_buf[0x1980])
#define citywalk_arrow_full    (&panoramic_map_buf[0x2d28])
#define citywalk_arrow_dual    (&panoramic_map_buf[0x3278])
/* inside panoramic_map_buf: panoramic map mask */
#define panoramic_map_mask     (&panoramic_map_buf[0x2068])

/* ---- ble_proto.c ---- */
int spim_transfer(struct ble_context *ctx, uint8_t *buf, uint32_t len);
int parse_receiver_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len);
int parse_receiver_msg_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len);
int ble_process_req_dispatch(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);
int start_ble_work_thread(struct ble_context *ctx);
int send_dmic_msg(struct ble_context *ctx);
int send_debug_msg(struct ble_context *ctx);
int deal_event_to_phone(struct ble_context *ctx, uint8_t *evt);
typedef void (*ble_send_fn_t)(uint8_t *buf, uint32_t len);
int subcontracing_send_data_pkcs7(const uint8_t *data, int len, uint8_t cmd, ble_send_fn_t send);
void ble_handle_rx_data(uint8_t *data, uint32_t len, int dump);

#endif /* G1_BLE_PROTO_H_ */
