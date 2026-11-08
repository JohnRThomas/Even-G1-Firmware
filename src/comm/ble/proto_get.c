/*
 * BLE "GET" requests from the phone (commands 0x29..0x45): device and battery
 * information, settings, notification whitelist, usage statistics, file
 * transfer status and similar read-outs.  Every reply is built from the
 * master-side state and written back over the link.
 */
#include <string.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "comm/proxy_thread.h"
#include "services/notifications/notify.h"
#include "services/notifications/whitelist.h"
#include "util/common_utils.h"
#include "services/tasks/tasks.h"
#include "ui/screens/ui_even_ai_v2_info.h"
#include "services/storage/setting_store.h"

void ble_process_get_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data)
{
	struct GlassesState *globle = CONTAINER_OF(ctx, struct GlassesState, ble_context);
	uint8_t resp[21] = {0};
	uint8_t rx[0x11c];

	memcpy(ctx->tx_buf, hdr, sizeof(*hdr));
	ctx->tx_len = hdr->len;
	resp[0] = hdr->cmd;

	switch (hdr->cmd) {
	case BLE_CMD_NOTIFI_WHITELIST_GET:
		get_whitelist_app_from_device(ctx, resp);
		return;
	case BLE_CMD_BRIGHTNESS_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x65;
		resp[2] = rx[0];
		resp[3] = rx[1];
		G1_LOG_INF("BLE_REQ_GET_BRIGHTNESS: 0x%02x,0x%02x,0x%02x,0x%02x,\n", resp[0],
			   resp[1], resp[2], resp[3]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_DEVICE_INFO_GET:
		if (data[1] == 1) {
			globle->phone_is_ios = 0;
		} else if (data[1] == 2) {
			globle->phone_is_ios = 1;
		}
		G1_LOG_INF("%s mobile phone is connected,raw_data[1] = %d\n",
			   globle->phone_is_ios ? "IOS" : "Android", data[1]);
		ble_process_req_local(ctx, rx, 0x18);
		resp[1] = 0x66;
		memcpy(&resp[2], rx, 17);
		G1_LOG_INF("BLE_REQ_GET_DEVICE_INFO: CHG:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x "
			   "M_SW_VER:v%d.%d.%d S_SW_VER:v%d.%d.%d BLE_SW_VER:v%d.%d\n",
			   rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rx[8], rx[9],
			   rx[10], rx[15], rx[16]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_MAC_GET:
		ble_process_req_local(ctx, rx, 12);
		resp[1] = 0x67;
		dump_hex("get_mn_mac", rx, 12);
		memcpy(&resp[2], &rx[0], 6);
		memcpy(&resp[8], &rx[6], 6);
		G1_LOG_INF("BLE_REQ_GET_M_N_S_MAC: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, "
			   "0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
			   resp[0], resp[1], resp[2], resp[3], resp[4], resp[5], resp[6], resp[7],
			   resp[8], resp[9], resp[10], resp[11]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_SILENT_MODE_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x69;
		resp[2] = rx[0];
		resp[3] = rx[1];
		G1_LOG_INF("BLE_REQ_GET_DISPLAY_MODE: 0x%02x,0x%02x,\n", resp[0], resp[1]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_ANTI_SHAKE_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x68;
		resp[2] = rx[0];
		G1_LOG_INF("BLE_REQ_GET_ANTI_SHAKE_ENABLE: 0x%02x,0x%02x,\n", resp[0], resp[1]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_HEAD_UP_ANGLE_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x6d;
		resp[2] = rx[0];
		resp[3] = rx[1];
		G1_LOG_INF("BLE_REQ_GET_WAKEUP_ANGLE: 0x%02x,0x%02x,0x%02x,0x%02x,\n", rx[0], rx[1],
			   rx[2], rx[3]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_DEVICE_SN_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x34;
		memcpy(&resp[2], rx, 16);
		G1_LOG_INF("BLE_REQ_GET_DEVICE_SN: %s\n", rx);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_GLASSES_SN_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x33;
		memcpy(&resp[2], rx, 16);
		G1_LOG_INF("BLE_REQ_GET_GLASSES_SN: %s\n", rx);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_ESB_CHANNEL_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = PKCS7_PACK_DONE;
		resp[2] = rx[0];
		G1_LOG_INF("BLE_REQ_GET_ESB_CHANNEL: %d\n", resp[2]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_TIME_SINCE_BOOT_GET:
		ble_process_req_local(ctx, rx, 1);
		resp[1] = 0x37;
		memcpy(&resp[2], rx, 5);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_ANCS_ENABLE_GET:
		resp[1] = PKCS7_PACK_DONE;
		resp[2] = globle->bonded;
		G1_LOG_ERR("globle->enable_ancs %d\n", resp[2]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_NOTIF_COUNT_GET:
		get_notification_counts_cmd_process(ctx, rx, resp);
		return;
	case BLE_CMD_WEAR_DETECTION_GET:
		resp[1] = PKCS7_PACK_DONE;
		resp[2] = globle->check_mode;
		G1_LOG_ERR("globle->check_mode %d\n", resp[2]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_RUNNING_APP_GET:
		if (*(uint16_t *)&data[1] != hdr->len) {
			G1_LOG_ERR(
				"get device  information packet length error,input data length = "
				"%d,packet data length = %d\n",
				hdr->len, *(uint16_t *)&data[1]);
			memset(&resp[6], 0, 15);
			memcpy(resp, data, 5);
			resp[5] = 0xff;
		} else {
			memset(&resp[5], 0, 16);
			memcpy(resp, data, 5);
			if (is_idle()) {
				resp[5] = 0;
				G1_LOG_INF("return system status to app,current running app is "
					   "E_ID_SCREEN_IDLE\n");
			} else {
				resp[5] = globle->dashboard_mode;
				G1_LOG_INF(
					"return system status to app,current running app is %d\n",
					globle->dashboard_mode);
			}
		}
		ctx->write(resp, 6);
		return;
	case BLE_CMD_RASTER_CONFIG_GET:
		resp[1] = PKCS7_PACK_DONE;
		resp[2] = globle->panel.raster_height_offset;
		resp[3] = globle->panel.canvas_distance_gear;
		G1_LOG_INF("BLE_GET_RASTER_CONFIG:raster_height_gear:%d,canvas_distance_gear:%d\n",
			   resp[2], resp[3]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_NOTIF_AUTO_DISPLAY_GET:
		resp[1] = PKCS7_PACK_DONE;
		resp[2] = globle->message_mode;
		resp[3] = globle->message_stay_time;
		G1_LOG_ERR("globle->message_mode %d globle->message_stay_time %d\n", resp[2],
			   resp[3]);
		ctx->write(resp, 20);
		return;
	case BLE_CMD_LANGUAGE_SET: {
		uint16_t data_len = *(uint16_t *)&data[1];
		uint8_t lang;
		uint8_t master;

		if (data_len != hdr->len) {
			G1_LOG_WRN(
				"packet length error,input data length = %d,packet data length = "
				"%d\n",
				hdr->len, data_len);
			return;
		}
		lang = data[5];
		memset(resp, 0, sizeof(resp));
		memcpy(resp, data, data_len);
		master = is_master();
		CURRENT_LANGUAGE.magic = lang;
		if (master == 1) {
			SendSystemLanguageInfoToSlave(lang);
		}
		ctx->write(resp, data_len);
		return;
	}
	case BLE_CMD_BURIAL_POINT_GET: {
		/*
		 * Usage statistics are not collected.  The phone still asks for the
		 * record, so it gets an empty one of the size it expects: no id, no
		 * totals, no per-day entries.
		 */
		uint8_t buf[2 + USAGE_STATS_RECORD_SIZE] = {0};

		buf[0] = hdr->cmd;
		buf[1] = PKCS7_PACK_DONE;
		buf[2 + USAGE_STATS_VERSION_OFFSET] = USAGE_STATS_VERSION;
		buf[2 + USAGE_STATS_VALID_OFFSET] = 1;
		ctx->write(buf, sizeof(buf));
		return;
	}
	case BLE_CMD_HARDWARE_GET:
		resp[1] = data[1];
		resp[2] = PKCS7_PACK_DONE;
		switch (data[1]) {
		case HW_SUB_DOUBLE_TAP_ACTION:
			resp[3] = globle->double_tap_customize;
			break;
		case HW_SUB_LUM_COEFFICIENT:
			resp[3] = globle->panel.brightness_percent;
			break;
		case HW_SUB_LONG_PRESS_ENABLE:
			resp[3] = globle->longpress_tap_customize;
			break;
		case HW_SUB_MIC_ON_HEAD_UP:
			resp[3] = globle->head_up_mic_disabled;
			break;
		default:
			resp[2] = PKCS7_PACK_ERR;
			break;
		}
		ctx->write(resp, 20);
		return;
	default:
		G1_LOG_WRN("#tx error get req_type: %d\n", hdr->cmd);
		resp[1] = PKCS7_PACK_ERR;
		resp[2] = 20;
		strcpy((char *)&resp[2], "error get req.");
		ctx->write(resp, 20);
		return;
	}
}
