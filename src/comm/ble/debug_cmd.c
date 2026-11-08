/*
 * Test commands typed into the phone app's debug console.  They arrive as
 * plain "#..." strings instead of protocol packets and are filtered out in
 * front of the request dispatch: they inject fake notifications, dashboard
 * and navigation content, toggle the BLE log channel, print build and
 * radio information, and exercise the audio upload path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <cJSON.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "services/imu/imu_fusion.h"
#include "services/nfc_wlc/wlc.h"
#include "services/notifications/notify.h"
#include "util/common_utils.h"
#include "system/work_mode.h"
#include "system/system_state.h"
#include "comm/ipc/netcore_link.h"
#include "drivers/ambient/opt3007.h"
#include "drivers/mic/dmic.h"
#include "ui/screens/ui_even_ai_v2_info.h"
#include "drivers/display/jbd_panel_flush.h"
#include "services/storage/file_storage.h"

typedef void (*ble_send_fn_t)(uint8_t *buf, uint32_t len);

static int subcontracing_send_data_pkcs7(const uint8_t *data, int len, uint8_t cmd,
					 ble_send_fn_t send)
{
	uint8_t buf[20] = {0};
	int integer_num = len / 17;
	int tail_len = len % 17;
	int i;
	int n;

	buf[1] = integer_num + 1;
	buf[0] = cmd;
	G1_LOG_INF("integer_num: %d, tail_len: %d, raw_data_len: %d\n", integer_num + 1, tail_len,
		   len);
	for (i = 0; i < integer_num; i++) {
		buf[2] = i;
		memcpy(&buf[3], data + i * 17, 17);
		G1_LOG_INF("#发送: %d/%d\n", i + 1, integer_num + 1);
		dump_hex("body", buf, sizeof(buf));
		send(buf, sizeof(buf));
	}
	n = i + 1;
	buf[2] = i;
	if (tail_len == 0) {
		/* PKCS#7 padding: a full block of 17 x 0x11 */
		memset(&buf[3], 17, 17);
		G1_LOG_INF("PKCS-7#发送: %d/%d\n", n, integer_num + 1);
		dump_hex("tail", buf, sizeof(buf));
	} else {
		memset(&buf[3] + tail_len, 17 - tail_len, 17 - tail_len);
		memcpy(&buf[3], data + integer_num * 17, tail_len);
		G1_LOG_INF("PKCS-7#发送tail: %d/%d\n", n, integer_num + 1);
		dump_hex("tail", buf, sizeof(buf));
	}
	G1_LOG_INF("total-size:%d\n", len);
	send(buf, sizeof(buf));
	return 0;
}

static int mobile_send_notification_to_ble_device_test(const char *title, const char *message,
						       ble_send_fn_t send)
{
	cJSON *notif = cJSON_CreateObject();
	cJSON *root;
	char *str;

	cJSON_AddStringToObject(notif, "app_identifier", "com.tencent.xin");
	cJSON_AddStringToObject(notif, "title", title);
	cJSON_AddStringToObject(notif, "subtitle", "aaa");
	cJSON_AddStringToObject(notif, "message", message);
	cJSON_AddStringToObject(notif, "date", "08-04 09:50");
	cJSON_AddStringToObject(notif, "display_name", "微信");
	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "ncs_notification", notif);
	str = cJSON_Print(root);
	G1_LOG_INF("%s\n", str);
	subcontracing_send_data_pkcs7((uint8_t *)str, strlen(str), 0x4b, send);
	cJSON_Delete(root);
	return 0;
}

static void simulator_ancs_calendar_schedule_trigger(void)
{
	struct ble_context *ctx = get_ble_context();

	G1_LOG_INF("enter\n");
	memset(&ctx->notif, 0, sizeof(ctx->notif));
	strncpy(ctx->notif.app_identifier, "com.apple.mobilecal",
		sizeof(ctx->notif.app_identifier));
	strncpy(ctx->notif.title, "日程1", sizeof(ctx->notif.title));
	strncpy(ctx->notif.message, "今天 16:35", sizeof(ctx->notif.message));
	strncpy(ctx->notif.display_name, "日历", sizeof(ctx->notif.display_name));
	dump_ancs("calendar-sched", &ctx->notif);
	k_sem_give(&ctx->ancs_get_attr_data_sem);
}

/*
 * Test commands typed into the phone app's debug console (plain strings
 * instead of protocol packets).  Returns non-zero if the data is not a test
 * command and must be handled as a normal request.
 */
/* audio upload test ("#a"): payload bytes per packet */
#define AUDIO_TEST_CHUNK_LEN 0x68

/* Test arguments that do not parse read as 0. */
/* NOLINTBEGIN(bugprone-unchecked-string-to-number-conversion) */
int spec_ble_command_hook(uint8_t *data, uint32_t len)
{
	struct ble_context *ctx = get_ble_context();
	struct GlassesState *state = CONTAINER_OF(ctx, struct GlassesState, ble_context);
	char buf[256] = {0};
	char *pos;
	int val;

	memcpy(buf, data, len);

	if (strcmp(buf, "#c") == 0) {
		G1_LOG_INF("test:BLE_REQ_POST_BT_UNPAIR\n\n");
		buf[0] = 0x47;
		ble_dispatch_raw_packet((uint8_t *)buf, 1);
		return 0;
	}

	pos = strstr(buf, "#1@");
	if (pos) {
		char title[32] = {0};
		char message[128] = {0};
		char *p;

		pos += 3;
		for (p = pos; *p != '@'; p++) {
		}
		strncpy(title, pos, p - pos);
		strcpy(message, p + 1);
		G1_LOG_INF("模拟接收到新通知 title:%s, message:%s\n", title, message);
		return mobile_send_notification_to_ble_device_test(title, message,
								   ble_dispatch_raw_packet);
	}

	pos = strstr(buf, "#2@");
	if (pos) {
		pos += 3;
		val = atoi(pos);
		G1_LOG_INF("模拟抬头/低头触发:pos:%s, val=%d\n\n", pos, val);
		buf[0] = 5;
		buf[1] = 1;
		buf[2] = val != 0;
		ble_dispatch_raw_packet((uint8_t *)buf, 3);
		return 0;
	}

	pos = strstr(buf, "#3@");
	if (pos) {
		pos += 3;
		val = atoi(pos);
		G1_LOG_INF("pos:%s, new screen_id=%d\n\n", pos, val);
		buf[0] = 5;
		buf[1] = 3;
		buf[2] = val;
		ble_dispatch_raw_packet((uint8_t *)buf, 3);
		return 0;
	}

	pos = strstr(buf, "#4@");
	if (pos) {
		pos += 3;
		val = atoi(pos);
		if (val > 2) {
			BLE_PRINTK("%s(): pos:%s, new log_level_set=%d\n\n", __func__, pos, val);
		}
		buf[0] = 5;
		buf[1] = 2;
		buf[2] = val;
		ble_dispatch_raw_packet((uint8_t *)buf, 3);
		return 0;
	}

	pos = strstr(buf, "#5@");
	if (pos) {
		char str[16] = {0};
		char *p, *q;
		int sec, enable;

		memcpy(str, pos + 3, 4);
		G1_LOG_INF("buf:%s\n\n", str);
		p = str;
		while (*p != '\0') {
			if (*++p == '@') {
				break;
			}
		}
		char cmd[4] = {0};

		memcpy(cmd, str, p - str);
		G1_LOG_INF("cmd:%s\n\n", cmd);
		p++;
		char en[4] = {0};

		for (q = p; *q != '\0'; q++) {
		}
		memcpy(en, p, q - p);
		G1_LOG_INF("en:%s\n\n", en);
		sec = atoi(cmd) * 60 + 3610;
		buf[0] = 7;
		memcpy(&buf[1], &sec, sizeof(sec));
		enable = atoi(en);
		buf[5] = enable;
		G1_LOG_INF("enable: %d, sec:%d\n\n", enable, sec);
		ble_dispatch_raw_packet((uint8_t *)buf, 6);
		return 0;
	}

	pos = strstr(buf, "#cmd@");
	if (pos) {
		char *p;
		int n, cmd;

		pos += 5;
		G1_LOG_INF("pos=%s, tx_data_len=%d\n\n", pos, len);
		char cmd_type[4] = {0};
		char value[128] = {0};

		dump_hex("ble", (uint8_t *)pos, 8);
		for (p = pos; *p != '@'; p++) {
		}
		n = p - pos;
		G1_LOG_INF("11 len=%d\n", n);
		memcpy(cmd_type, pos, n);
		cmd = atoi(cmd_type);
		G1_LOG_INF("cmd=%d\n", cmd);
		if (cmd == 4) {
			G1_LOG_INF("BLE_REQ_PUT_NOTIFI_WHIHTLIST_APP需要单独处理.\n\n");
			char json[256] = {0};

			strcpy(json,
			       "{\"whitelist_app\":\"com.apple.MobileSMS:com.tencent.xin:com.tencent.mqq\"}");
			n = strlen(json);
			G1_LOG_INF("cmd_type:%s, value=%s, tx_len=%d, jstr_len=%d, jsonstr=%s\n\n",
				   cmd_type, value, 0, n, json);
			subcontracing_send_data_pkcs7((uint8_t *)json, n, 4,
						      ble_dispatch_raw_packet);
			return 0;
		}
		if (cmd == 9) {
			cJSON *info, *root;
			char *str;

			G1_LOG_INF("simulator->BLE_REQ_PUT_TELEPROMPTER_INFO.\n\n");
			info = cJSON_CreateObject();
			cJSON_AddNumberToObject(info, "enable", 1);
			cJSON_AddStringToObject(
				info, "text",
				"提词器功能很简单,我们只需要把文档或者TXT文件上传到手机APP中,眼镜上就显示相应的文字，我们可以用手机当遥控器翻...");
			root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "teleprompter_info", info);
			str = cJSON_Print(root);
			n = strlen(str);
			G1_LOG_INF("cmd_type:%s, value=%s, tx_len=%d, jstr_len=%d, jsonstr=%s\n\n",
				   cmd_type, value, 0, n, str);
			subcontracing_send_data_pkcs7((uint8_t *)str, n, 9,
						      ble_dispatch_raw_packet);
			cJSON_Delete(root);
			return 0;
		}
		if (cmd == 10) {
			cJSON *info, *root;
			char *str;

			G1_LOG_INF("simulator->BLE_REQ_PUT_NAVIGATION_INFO.\n\n");
			info = cJSON_CreateObject();
			cJSON_AddNumberToObject(info, "enable", 1);
			cJSON_AddNumberToObject(info, "direction", 2);
			cJSON_AddStringToObject(info, "info", "前面100米路口左拐进入留仙大道辅道.");
			root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "navigation_info", info);
			str = cJSON_Print(root);
			n = strlen(str);
			G1_LOG_INF("cmd_type:%s, value=%s, tx_len=%d, jstr_len=%d, jsonstr=%s\n\n",
				   cmd_type, value, 0, n, str);
			subcontracing_send_data_pkcs7((uint8_t *)str, n, 10,
						      ble_dispatch_raw_packet);
			cJSON_Delete(root);
			return 0;
		}
		if (cmd == 0x58) {
			G1_LOG_INF("simulator:SCHEDULE 触发.\n\n");
			simulator_ancs_calendar_schedule_trigger();
			return 0;
		}
		if (cmd == 0x4b) {
			char title[32];
			char msg[128];
			char *q;

			p++;
			for (q = p; *q != '\0'; q++) {
			}
			n = q - p;
			G1_LOG_INF("22 len=%d\n", n);
			memcpy(value, p, n);
			for (q = value; *q != '#'; q++) {
			}
			strncpy(title, value, q - value);
			strcpy(msg, q + 1);
			G1_LOG_INF("#cmd_type:%s, title=%s, msg=%s\n\n", cmd_type, title, msg);
			return mobile_send_notification_to_ble_device_test(title, msg,
									   ble_dispatch_raw_packet);
		}
		buf[0] = cmd;
		buf[1] = atoi(value);
		G1_LOG_INF("**cmd_type:%s, value=%s, tx_len=%d\n\n", cmd_type, value, 2);
		ble_dispatch_raw_packet((uint8_t *)buf, 2);
		return 0;
	}

	if (strncmp(buf, "#a", 2) == 0) {
		printk("\n*****test callback audio data*****\n");
		char req[20] = {0};

		req[0] = 0x6e;
		while (ctx->write(req, sizeof(req)) != 0) {
			k_sleep(K_MSEC(10));
			if (!ctx->connected || !ctx->send_enabled) {
				G1_LOG_ERR(
					"unable to change MTU for audio transfer, due to no connected or "
					"notification disabled\n");
				return 0;
			}
		}
		{
			uint8_t pkt[3 + AUDIO_TEST_CHUNK_LEN];
			uint16_t err_cnt = 0;
			int64_t t0;
			int i;

			k_sem_take(&ctx->tx_sem, K_FOREVER);
			printk("\n*****start upload audio*****\n");
			char audio[1024] = {0};
			const char *p = audio;

			for (i = 0; i < sizeof(audio); i++) {
				audio[i] = (i & 0xf) + '0';
			}
			t0 = k_uptime_get();
			for (i = 0; i < 9; i++) {
				pkt[0] = 0xf1;
				pkt[1] = 0x0a;
				pkt[2] = i;
				memcpy(&pkt[3], p, AUDIO_TEST_CHUNK_LEN);
				while (ctx->write(pkt, sizeof(pkt)) != 0) {
					k_sleep(K_MSEC(10));
					err_cnt++;
					if (!ctx->connected || !ctx->send_enabled) {
						G1_LOG_ERR("unable to audio transfer, due to no "
							   "connected or notification disabled\n");
						return 0;
					}
				}
				p += AUDIO_TEST_CHUNK_LEN;
			}
			printk("t0 = %d error count = %d \n", (int)(k_uptime_get() - t0), err_cnt);
		}
		return 0;
	}

	if (strncmp(buf, "#b", 2) == 0) {
		val = atoi(&buf[2]);
		state->esb_channel = val;
		printk("\n*****test set esb channel***** %d\n", (uint8_t)val);
		state->dashboard_cmd = 0;
		sys_settings_dirty = 1;
		k_sem_give(&state->dashboard_position_sem);
		return 0;
	}

	if (strncmp(buf, "#d", 2) == 0) {
		val = atoi(&buf[2]);
		state->display_mode = val;
		printk("\n*****display mode***** %d\n", (uint8_t)val);
		if (state->display_mode == 11) {
			state->imu.mode = 2;
			change_work_mode(2);
			if (state->is_master == 2) {
				k_sem_give(&state->sem_1);
			}
		} else {
			state->imu.mode = 1;
			change_work_mode(3);
		}
		state->dashboard_cmd = 0;
		k_sem_give(&state->dashboard_position_sem);
		return 0;
	}

	if (strncmp(buf, "#e", 2) == 0) {
		if (buf[2] == '0') {
			dmic_stream_stop();
		} else {
			state->pending |= 4;
		}
		k_sem_give(&state->main_sem);
		return 0;
	}

	if (strncmp(buf, "#f", 2) == 0) {
		state->pending |= 8;
		k_sem_give(&state->main_sem);
		return 0;
	}

	if (strncmp(buf, "#g", 2) == 0) {
		char out[200] = {0};

		sprintf(out, "TX:%d,fail:%d,RX:%d,Signal:%d", esb_tx_count, esb_tx_fail_count,
			esb_rx_count, esb_signal);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#r", 2) == 0) {
		G1_LOG_ERR("sys reboot because reboot cmd\n");
		system_reboot_delayed();
		return 0;
	}

	if (strncmp(buf, "#l", 2) == 0) {
		if (atoi(&buf[2]) == 1) {
			BLE_DEBUG = 0;
		} else {
			BLE_DEBUG = 1;
		}
		return 0;
	}

	if (strncmp(buf, "#o", 2) == 0) {
		char out[20] = {0};

		state->imu.ref = imu_pitch_x100;
		sprintf(out, "base: %d", imu_pitch_x100);
		ctx->write(out, sizeof(out));
		state->dashboard_cmd = 0;
		sys_settings_dirty = 1;
		k_sem_give(&state->dashboard_position_sem);
		return 0;
	}

	if (strncmp(buf, "#i", 2) == 0) {
		char out[20] = {0};
		uint32_t lux = opt3007_get_max_lux();

		if (buf[2] != '\0') {
			if (atoi(&buf[2]) < 2U) {
				state->opt3007_context.auto_brightness = atoi(&buf[2]);
			} else {
				state->opt3007_context.auto_brightness = 0;
				state->panel.panel_brightness_level = atoi(&buf[2]);
			}
		}
		if (lux == 0xffffffff) {
			sprintf(out, "base: %d", 80000);
		} else {
			sprintf(out, "base: %d", lux);
		}
		ctx->write(out, sizeof(out));
		state->dashboard_cmd = 0;
		k_sem_give(&state->dashboard_position_sem);
		return 0;
	}

	if (strncmp(buf, "#y", 2) == 0) {
		char out[20] = {0};

		sprintf(out, "level: %d", state->panel.panel_brightness_level);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#u", 2) == 0) {
		char out[20] = {0};

		val = atoi(&buf[2]);
		state->imu.level = val;
		sprintf(out, "level: %d", (uint8_t)val);
		ctx->write(out, sizeof(out));
		state->dashboard_cmd = 0;
		k_sem_give(&state->dashboard_position_sem);
		return 0;
	}

	if (strncmp(buf, "#p", 2) == 0) {
		char out[20] = {0};

		sprintf(out, "pitch: %d", imu_pitch_x100);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#t", 2) == 0) {
		char out[200] = {0};

		sprintf(out,
			"net build time: %s, app build time %s, ver %d.%d.%d, JBD DeviceID %x\n\r",
			net_build_time, G1_BUILD_TIME, FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH,
			JDB_PANEL_ID);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#v", 2) == 0) {
		char out[200] = {0};

		sprintf(out, "B:%d, V:%d, A:%d, T:%d", state->battery_left,
			state->battery_voltage + 200, state->battery_current - 128,
			state->battery_temp);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#k", 2) == 0) {
		char out[64] = {0};

		sprintf(out, "VADC:%d TMR:%d", chargerGetVdcRect(), 0);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#j", 2) == 0) {
		char out[20] = {0};

		if (atoi(&buf[2]) == 1) {
			state->spi.init_cb(&state->spi);
			state->flash_ops.fn(&state->flash_ops);
			strcpy(out, "init flash");
		} else {
			state->spi.init_cb(&state->spi);
			state->flash_ops.fn(&state->flash_ops);
			strcpy(out, "deinit flash");
		}
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#s", 2) == 0) {
		char out[20] = {0};

		change_work_mode(buf[2] != '\0' ? atoi(&buf[2]) : 7);
		ctx->write(out, sizeof(out));
		return 0;
	}

	if (strncmp(buf, "#h", 2) == 0) {
		char out[64] = {0};

		switch (atoi(&buf[2])) {
		case 1:
			state->dashboard_cmd = 0x19;
			sys_settings_dirty = 1;
			k_sem_give(&state->dashboard_position_sem);
			break;
		case 2:
			memset(state->master_mac, 0xff, sizeof(state->master_mac));
			memset(state->slave_mac, 0xff, sizeof(state->slave_mac));
			state->dashboard_cmd = 0x1a;
			sys_settings_dirty = 1;
			k_sem_give(&state->dashboard_position_sem);
			break;
		default:
			sprintf(out,
				"master mac: %02X:%02X:%02X:%02X:%02X:%02X, slave mac: "
				"%02X:%02X:%02X:%02X:%02X:%02X",
				state->master_mac[0], state->master_mac[1], state->master_mac[2],
				state->master_mac[3], state->master_mac[4], state->master_mac[5],
				state->slave_mac[0], state->slave_mac[1], state->slave_mac[2],
				state->slave_mac[3], state->slave_mac[4], state->slave_mac[5]);
			break;
		}
		ctx->write(out, sizeof(out));
		return 0;
	}

	return -1;
}

/* NOLINTEND(bugprone-unchecked-string-to-number-conversion) */
