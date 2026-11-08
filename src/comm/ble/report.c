/*
 * Reports the glasses send to the phone on their own (commands 0xf1..0xf6):
 * microphone audio, debug log lines and state events (battery, wear state,
 * brightness, ...), together with the handling of the phone's replies to
 * those reports.  The work thread requests a report by queueing a small 0xf1
 * / 0xf4 / 0xf5 packet for itself; the phone's acknowledgement then drives
 * the next chunk.
 */
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/report.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "services/production_test/production_test.h"
#include "ui/screen_dispatch.h"
#include "services/tasks/tasks.h"
#include "drivers/mic/dmic.h"
#include "services/production_test/pt_mic.h"

static int dmic_send_count;
static int dmic_pkt_count;
static int send_fail_count;
static int dashboard_send_fail_count;
static uint8_t dmic_seq;
uint8_t last_ble_cmd = 0xff;

static int send_dmic_msg(struct ble_context *ctx)
{
	uint8_t buf[203] = {0};
	int ret;

	buf[0] = 0xf1;
	do {
		buf[1] = dmic_seq;
		if (get_dmic_num_messages() == 0) {
			return 0;
		}
		ret = dequeue_dmic(&buf[2]);
		if (ret) {
			return ret;
		}
		ret = ctx->write(buf, 0xca);
		if (ret < 0) {
			if (get_dashboard_state()->dashboard_mode == 6) {
				if (dashboard_send_fail_count < 100) {
					dashboard_send_fail_count++;
				}
				send_fail_count = 0;
			} else {
				send_fail_count++;
				dashboard_send_fail_count = 0;
			}
			if (send_fail_count >= 10) {
				G1_LOG_ERR("BLE send failed for %d times\n", send_fail_count);
				send_fail_count = 0;
				exit_running_task(get_dashboard_state(), 1);
				return ret;
			}
		} else if (send_fail_count > 0) {
			send_fail_count--;
		}
		if (++dmic_pkt_count >= 10) {
			dmic_pkt_count = 0;
			int64_t now = k_uptime_ticks();

			dmic_send_count++;
			G1_LOG_ERR("time %d dmic_send_count %d send_fail_count %d\n",
				   (uint32_t)k_ticks_to_ms_ceil64(now), dmic_send_count * 10,
				   get_dashboard_state()->dashboard_mode == 6
					   ? dashboard_send_fail_count
					   : send_fail_count);
		}
		dmic_seq++;
	} while (dmic_seq & 3);
	return 0;
}

static int send_debug_msg(struct ble_context *ctx)
{
	char buf[DEBUG_MSG_SIZE + 4];

	while (1) {
		memset(&buf[1], 0, 0xca);
		buf[0] = 0xf4;
		if (dequeue_debug(&buf[1])) {
			break;
		}
		ctx->write(buf, strlen(buf) + 1);
	}
	return 0;
}

static int deal_event_to_phone(struct ble_context *ctx, uint8_t *evt)
{
	struct GlassesState *state = CONTAINER_OF(ctx, struct GlassesState, ble_context);
	uint8_t buf[21] = {0};

	switch (evt[1]) {
	case 0xf0:
		buf[0] = 0x0d;
		buf[1] = PKCS7_PACK_DONE;
		buf[2] = state->esb_down_id;
		buf[3] = deferred_text_ack_total;
		buf[4] = deferred_text_ack_idx;
		deferred_text_ack_total = 0;
		deferred_text_ack_idx = 0;
		break;
	case 0xf1:
		buf[0] = 0x0f;
		buf[1] = PKCS7_PACK_DONE;
		buf[2] = state->esb_up_id;
		buf[3] = deferred_text_ack_total;
		buf[4] = deferred_text_ack_idx;
		deferred_text_ack_total = 0;
		deferred_text_ack_idx = 0;
		break;
	case 0xf2:
		buf[0] = 0x4e;
		buf[1] = PKCS7_PACK_DONE;
		buf[2] = state->esb_up_id;
		buf[3] = deferred_text_ack_total;
		buf[4] = deferred_text_ack_idx;
		deferred_text_ack_total = 0;
		deferred_text_ack_idx = 0;
		break;
	case 9:
		memcpy(buf, evt, 3);
		break;
	case 0x0e:
		buf[0] = 0xf5;
		buf[1] = 0x0e;
		buf[2] = get_pt_level_flag();
		break;
	case 0x0f:
		buf[0] = 0xf5;
		buf[1] = 0x0f;
		buf[2] = get_pt_level();
		break;
	case 10:
		buf[0] = 0xf5;
		buf[1] = 10;
		switch (state->battery_left) {
		case 0x5d:
			buf[2] = state->battery_soc_flag ? 0x5f : 0x5e;
			break;
		case 0x5e:
			buf[2] = state->battery_soc_flag ? 0x61 : 0x60;
			break;
		case 0x5f:
			buf[2] = 0x62;
			break;
		case 0x60:
			buf[2] = 0x63;
			break;
		default:
			buf[2] = state->battery_left > 0x60 ? 0x64 : state->battery_left;
			break;
		}
		break;
	case 0x12:
		buf[0] = 0xf5;
		buf[1] = 0x12;
		buf[2] = state->panel.panel_brightness_level;
		break;
	case 0x1e:
		G1_LOG_WRN("send dashboard start...\n");
		__fallthrough;
	default:
		buf[0] = 0xf5;
		buf[1] = evt[1];
		break;
	}
	ctx->write(buf, sizeof(buf));
	return 0;
}

int ble_process_report_response(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data)
{
	uint8_t cmd = hdr->cmd;
	uint8_t resp[21] = {0};

	switch (cmd) {
	case BLE_CMD_AUDIO:
		if (data[2] == PKCS7_PACK_DONE || data[2] == PKCS7_PACK_MORE) {
			return 0;
		}
		if (data[2] == 0xcc) {
			send_dmic_msg(ctx);
			return 0;
		}
		break;
	case BLE_CMD_DEBUG:
		send_debug_msg(ctx);
		return 0;
	case BLE_CMD_EVENT:
		deal_event_to_phone(ctx, data);
		return 0;
	default:
		break;
	}
	G1_LOG_WRN("@tx error post req_type: %d\n", cmd);
	resp[1] = PKCS7_PACK_ERR;
	resp[2] = 0x15;
	strcpy((char *)&resp[2], "error post req.");
	ctx->write(resp, 20);
	return 0;
}

void report_event_to_phone(int cmd)
{
	struct ble_context *ctx = get_ble_context();

	if (cmd >= 0xf0 && cmd <= 0xf2 && last_ble_cmd != 0xff) {
		return;
	}
	last_ble_cmd = cmd;
	if (ctx->receiver_thd_is_running || k_sem_count_get(&ctx->rx_sem)) {
		uint8_t buf[3];

		buf[0] = 0xf5;
		buf[1] = cmd;
		buf[2] = 0xcb;
		enqueue_bt_data(buf, 3);
		last_ble_cmd = 0xff;
	} else if (!ctx->receiver_thd_is_running) {
		k_sem_give(&ctx->rx_sem);
		k_msleep(1);
	}
}
