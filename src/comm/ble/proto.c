/*
 * BLE application protocol: the work thread that takes packets from the NUS
 * receive queue, the "#..." debug-command filter in front of it, pkcs7 packet
 * reassembly, dispatch of a request to the PUT / GET / POST / report handler
 * by command range, and the hand-over of a staged request to the master-side
 * request processing.  Owns the kernel objects the handlers share (the
 * navigation map mutex and the dashboard lock timer).
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "comm/ble/report.h"
#include "services/imu/imu_fusion.h"
#include "util/common_utils.h"
#include "drivers/mic/dmic.h"
#include "ui/screens/ui_navigation_map.h"

#define BLE_WORK_THREAD_STACK_SIZE 0x3000
#define BLE_WORK_THREAD_PRIORITY   (-15)

/* pkcs7 reassembly state (parse_receiver_pack_pkcs7) */
static uint16_t pack_offset;
static uint8_t pack_cmd;
/* pkcs7 message reassembly state (parse_receiver_msg_pack_pkcs7) */
static uint16_t msg_pack_offset;
static uint16_t msg_pack_id;
static uint8_t msg_pack_cmd;

/* multi-packet reassembly progress, shared by the PUT and POST handlers */
uint16_t multi_packet_len;   /* multi-packet: bytes received */
uint16_t multi_packet_cnt;   /* multi-packet: packets received */
uint16_t multi_packet_total; /* multi-packet: total packets */

atomic_t dashboard_lock_requested;
atomic_t dashboard_lock_reported;

static K_THREAD_STACK_DEFINE(ble_work_thread_stack, BLE_WORK_THREAD_STACK_SIZE);
static struct k_thread ble_work_thread_data;
struct k_timer DashboardLockTimer;

int parse_receiver_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len)
{
	uint8_t cmd, total, idx;

	if (req == NULL) {
		BLE_PRINTK("[%s-%d]error, req is NULL ! \n", __func__, __LINE__);
		return PKCS7_PACK_ERR;
	}

	if (origin_data == NULL) {
		BLE_PRINTK("[%s-%d]error, origin_data is NULL ! \n", __func__, __LINE__);
		return PKCS7_PACK_ERR;
	}

	if (req_len < 3) {
		BLE_PRINTK("[%s-%d]error, req_len is %d, too short ! \n", __func__, __LINE__,
			   req_len);
		return PKCS7_PACK_ERR;
	}

	cmd = req[0];
	total = req[1];
	idx = req[2];
	if (idx == 0) {
		pack_offset = 0;
		pack_cmd = cmd;
	}

	/*
	 * Chunk layout: req[0] command, req[1] total number of chunks,
	 * req[2] chunk index (0-based), payload from req[3].
	 *
	 * The first chunk resets the write offset and latches the command;
	 * every later chunk has to carry the same command.
	 */

	if (pack_cmd != cmd) {
		BLE_PRINTK("[%s-%d]error, curCmd is %d, but parseCmd is %d ! \n", __func__,
			   __LINE__, cmd, pack_cmd);
		return PKCS7_PACK_ERR;
	}
	memcpy(origin_data + pack_offset, req + 3, req_len - 3);
	pack_offset += req_len - 3;
	if (idx == total - 1) {
		return PKCS7_PACK_DONE;
	}
	return PKCS7_PACK_MORE;
}

int parse_receiver_msg_pack_pkcs7(uint8_t *origin_data, const uint8_t *req, uint16_t req_len)
{
	uint8_t cmd, msg_id, total, idx;

	if (req == NULL) {
		BLE_PRINTK("[%s-%d]error, req is NULL ! \n", __func__, __LINE__);
		return PKCS7_PACK_ERR;
	}

	if (origin_data == NULL) {
		BLE_PRINTK("[%s-%d]error, origin_data is NULL ! \n", __func__, __LINE__);
		return PKCS7_PACK_ERR;
	}

	if (req_len < 4) {
		BLE_PRINTK("[%s-%d]error, req_len is %d, too short ! \n", __func__, __LINE__,
			   req_len);
		return PKCS7_PACK_ERR;
	}

	cmd = req[0];
	msg_id = req[1];
	total = req[2];
	idx = req[3];
	if (idx == 0) {
		msg_pack_offset = 0;
		msg_pack_id = msg_id;
		msg_pack_cmd = cmd;
	}

	/*
	 * Chunk layout: req[0] command, req[1] message id, req[2] total
	 * number of chunks, req[3] chunk index (0-based), payload from
	 * req[4].
	 *
	 * The first chunk resets the write offset and latches the command
	 * and the message id; every later chunk has to carry the same
	 * command and message id.  The payloads are appended to
	 * origin_data in the order of arrival; the chunk with index
	 * total - 1 completes the message.
	 */
	if (msg_pack_cmd != cmd) {
		BLE_PRINTK("[%s-%d]error, curCmd is %d, but parseCmd is %d ! \n", __func__,
			   __LINE__, cmd, msg_pack_cmd);
		return PKCS7_PACK_ERR;
	}

	if (msg_pack_id != msg_id) {
		BLE_PRINTK("[%s-%d]error, curMsgId is %d, but parseMsgId is %d ! \n", __func__,
			   __LINE__, msg_id, msg_pack_id);
		return PKCS7_PACK_ERR;
	}
	memcpy(origin_data + msg_pack_offset, req + 4, req_len - 4);
	msg_pack_offset += req_len - 4;
	if (idx != total - 1) {
		return PKCS7_PACK_MORE;
	}
	BLE_PRINTK("origin_data is %s \n", origin_data);
	return PKCS7_PACK_DONE;
}

int ble_process_req_local(struct ble_context *ctx, uint8_t *buf, uint32_t len)
{
	struct GlassesState *state = CONTAINER_OF(ctx, struct GlassesState, ble_context);
	int ret;

	if (LOG_LEVEL == 5) {
		printk("spim tx(len=%d): %02x,%02x,%02x,%02x, %02x,%02x,%02x,%02x\n", ctx->tx_len,
		       ctx->tx_buf[0], ctx->tx_buf[1], ctx->tx_buf[2], ctx->tx_buf[3],
		       ctx->tx_buf[0], ctx->tx_buf[1], ctx->tx_buf[2], ctx->tx_buf[3]);
	}
	ret = master_process_put_req(state, (struct ble_req *)ctx->tx_buf, buf, len);
	if (LOG_LEVEL == 5) {
		printk("spim ret(len=%d): %02x,%02x,%02x,%02x, %02x,%02x,%02x,%02x\n", len, buf[0],
		       buf[1], buf[2], buf[3], buf[0], buf[1], buf[2], buf[3]);
	}
	return ret;
}

void ble_dispatch_raw_packet(uint8_t *data, uint32_t len)
{
	struct ble_req_hdr hdr;

	struct ble_context *ctx = get_ble_context();

	hdr.cmd = data[0];
	hdr.source = 1;
	hdr.len = len;
	ble_process_req_dispatch(ctx, &hdr, data);
}

static void ble_handle_rx_data(uint8_t *data, uint32_t len, int dump)
{
	if (dump) {
		dump_hex("BLE-rec", data, len);
	}
	if (spec_ble_command_hook(data, len)) {
		ble_dispatch_raw_packet(data, len);
	}
}

static void DashboardLockTimerExpiry_callback(struct k_timer *timer)
{
	G1_LOG_INF("dashboard lock timeout,release .\n");
	atomic_clear_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT);
	atomic_clear_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT);
	atomic_clear_bit(&dashboard_head_up_opened, DASHBOARD_LOCK_BIT);
}

static void ble_work_thread(void *p1, void *p2, void *p3)
{
	struct ble_context *ctx = p1;
	struct bt_data_msg msg;
	int err;

	G1_LOG_INF("start\n\n");
	err = ble_link_start(ctx);
	debug_init();
	if (err) {
		G1_LOG_INF("err:%d\n\n", err);
	}
	ctx->tx_buf = malloc(BLE_TX_BUF_SIZE);
	G1_LOG_INF("tx_size:%d\n\n", BLE_TX_BUF_SIZE);

	while (1) {
		if (!(get_dashboard_state()->module_mask & BIT(7))) {
			k_msleep(1000);
			continue;
		}
		if (dequeue_bt_data(&msg) == 0) {
			if (msg.len > BLE_SMALL_PKT_MAX) {
				memcpy(ctx->rx_data, msg.data, msg.len);
				ctx->rx_len = msg.len;
			} else {
				memcpy(ctx->small_buf, msg.data, msg.len);
				ctx->rx_pending = 1;
				ctx->small_len = msg.len;
				ctx->rx_len = 0;
			}
		} else {
			ctx->receiver_thd_is_running = 0;
			k_sem_take(&ctx->rx_sem, K_FOREVER);
			ctx->receiver_thd_is_running = 1;
			if (ctx->rx_pending == 0 && ctx->rx_len == 0) {
				if (last_ble_cmd != 0xff) {
					ctx->rx_pending = 1;
					ctx->small_buf[0] = 0xf5;
					ctx->small_buf[1] = last_ble_cmd;
					last_ble_cmd = 0xff;
					ctx->small_buf[2] = PKCS7_PACK_MORE;
					ctx->rx_len = 0;
					ctx->small_len = 3;
					if (ctx->small_buf[1] == 9 || ctx->small_buf[1] == 10) {
						ctx->small_buf[2] = ctx->charging;
					}
				} else if (dmic_send_pending) {
					ctx->rx_pending = 1;
					ctx->small_buf[0] = 0xf1;
					dmic_send_pending = 0;
					ctx->small_buf[1] = 0;
					ctx->small_buf[2] = 0xcc;
					ctx->rx_len = 0;
				} else if (debug_pending) {
					ctx->rx_pending = 1;
					ctx->small_buf[0] = 0xf4;
					ctx->small_buf[1] = 0;
					ctx->small_buf[2] = PKCS7_PACK_MORE;
					debug_pending = 0;
					ctx->rx_len = 0;
				} else {
					continue;
				}
			}
		}

		if (ctx->restart_adv) {
			err = stop_ble_advertising();
			if (err) {
				G1_LOG_WRN("bt_le_adv_stop failed!!! err=%d\n\n", err);
			} else {
				G1_LOG_INF("charge plug off, call bt_start. \n\n");
				err = bt_start();
				if (err) {
					printk("Advertising failed to start (err %d)\n", err);
				}
			}
			ctx->restart_adv = 0;
			continue;
		}
		if (ctx->rx_len) {
			ble_handle_rx_data(ctx->rx_data, ctx->rx_len, 0);
			memset(ctx->rx_data, 0, sizeof(ctx->rx_data));
		} else {
			ble_handle_rx_data(ctx->small_buf, ctx->small_len, 0);
		}
		ctx->rx_len = 0;
		ctx->rx_pending = 0;
	}
}

int ble_process_req_dispatch(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data)
{
	uint8_t cmd = hdr->cmd;

	if (cmd >= 0x01 && cmd <= 0x27) {
		ble_process_put_req(ctx, hdr, data);
		return 0;
	}
	if (cmd >= 0x29 && cmd <= 0x45) {
		ble_process_get_req(ctx, hdr, data);
		return 0;
	}
	if (cmd >= 0x47 && cmd <= 0x56) {
		return ble_process_post_req(ctx, hdr, data);
	}
	if (cmd >= 0xf1 && cmd <= 0xf6) {
		return ble_process_report_response(ctx, hdr, data);
	}
	G1_LOG_ERR("tx error req_type. %d\n", cmd);
	ctx->write("NN", 3);
	return 0;
}

int start_ble_work_thread(struct ble_context *ctx)
{
	nav_data_init();
	k_timer_init(&DashboardLockTimer, DashboardLockTimerExpiry_callback, NULL);
	k_thread_create(&ble_work_thread_data, ble_work_thread_stack,
			K_THREAD_STACK_SIZEOF(ble_work_thread_stack), ble_work_thread, ctx, NULL,
			NULL, BLE_WORK_THREAD_PRIORITY, 0, K_NO_WAIT);
	return 0;
}
