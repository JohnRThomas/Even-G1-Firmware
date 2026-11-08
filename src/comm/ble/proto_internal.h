/*
 * Shared between the files of the BLE application protocol only (proto.c,
 * proto_put.c, proto_get.c, proto_post.c, report.c, debug_cmd.c): the request
 * handlers the dispatcher calls, the kernel objects and reassembly counters
 * that more than one handler touches, and the ESB receive-state helpers.
 * Other modules use proto.h.
 */
#ifndef G1_BLE_PROTO_INTERNAL_H_
#define G1_BLE_PROTO_INTERNAL_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "system/glasses_state.h"
#include "comm/ble/proto.h"

/* ---- proto.c: work thread and dispatch ---- */

/* hands a complete packet to ble_process_req_dispatch() */
void ble_dispatch_raw_packet(uint8_t *data, uint32_t len);
/* releases the dashboard lock one second after the phone requested it */
extern struct k_timer DashboardLockTimer;
/* multi-packet reassembly progress, shared by the PUT and POST handlers */
extern uint16_t multi_packet_len;   /* bytes received */
extern uint16_t multi_packet_cnt;   /* packets received */
extern uint16_t multi_packet_total; /* total packets */

/* ---- proto_put.c: PUT requests (0x01..0x27), ESB receive state ---- */
void ble_process_put_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);
int save_esb_snapshot(const void *data, size_t len, uint8_t arg, int sel);
int set_esb_recv_state(uint8_t a, uint8_t b, int sel);
void set_esb_up_recv_state(uint8_t a, uint8_t b);

/* ---- proto_get.c: GET requests (0x29..0x45) ---- */
void ble_process_get_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);

/* ---- proto_post.c: POST requests (0x47..0x56) ---- */
int ble_process_post_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);

/* ---- report.c: the phone's replies to glasses-initiated reports (0xf1..0xf6) ---- */
int ble_process_report_response(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);

/* ---- debug_cmd.c: "#..." console commands ---- */

/* returns non-zero if data is not a test command and is a normal request */
int spec_ble_command_hook(uint8_t *data, uint32_t len);

/* Usage-statistics record sent in reply to BLE_CMD_BURIAL_POINT_GET (always empty) */
#define USAGE_STATS_RECORD_SIZE    0x11c
#define USAGE_STATS_VERSION_OFFSET 0x14
#define USAGE_STATS_VALID_OFFSET   0x16
#define USAGE_STATS_VERSION        7

#endif /* G1_BLE_PROTO_INTERNAL_H_ */
