/*
 * BLE application protocol (phone <-> glasses): what the rest of the firmware
 * needs from it.  Request dispatch and the work thread, the pkcs7 packet
 * reassembly the notification service shares, the hand-over of a staged
 * request to the master-side processing, and the dashboard / navigation data
 * the PUT handler receives from the phone and the screens read.  The files of
 * the module share proto_internal.h.
 */
#ifndef G1_BLE_PROTO_H_
#define G1_BLE_PROTO_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "util/date.h"
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include "system/glasses_state.h"

#include "system/logging.h"

#include "comm/ble/ble_cmd.h"

/* pkcs7 packet reassembly results */
#define PKCS7_PACK_DONE 0xc9 /* last packet received */
#define PKCS7_PACK_ERR  0xca
#define PKCS7_PACK_MORE 0xcb /* more packets follow */

/* dashboard lock bits (dashboard_lock_reported / dashboard_lock_requested /
 * dashboard_head_up_opened) */
#define DASHBOARD_LOCK_BIT 1 /* bit number */

/* request header handed to ble_process_req_dispatch() */
struct ble_req_hdr {
	uint8_t cmd;
	uint8_t source;
	uint16_t len;
};

/* ---- globals defined in proto.c ---- */
extern atomic_t dashboard_lock_reported;
extern atomic_t dashboard_lock_requested;

/* ---- proto_put_local.c ---- */
struct ble_req;
int master_process_put_req(struct GlassesState *state, struct ble_req *head, uint8_t *out,
			   uint32_t len);

/* ---- proto.c ---- */

/*
 * Runs the request staged in ctx->tx_buf (header plus payload) through the
 * master-side request processing and leaves the reply in buf; returns the
 * number of reply bytes.
 */
int ble_process_req_local(struct ble_context *ctx, uint8_t *buf, uint32_t len);
int parse_receiver_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len);
int parse_receiver_msg_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len);
int ble_process_req_dispatch(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data);
int start_ble_work_thread(struct ble_context *ctx);

#endif /* G1_BLE_PROTO_H_ */
