/*
 * BLE "PUT" requests from the phone (commands 0x01..0x27): settings, time
 * sync, dashboard content (schedule, stocks, news, citywalk), countdown,
 * teleprompter, navigation, translate, microphone, quick notes / audio, file
 * transfer and onboarding.  The dashboard and navigation data received here
 * is kept in this file and read by the screens through proto.h.
 */
#include <string.h>
#include <zephyr/kernel.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "util/date.h"
#include "services/production_test/production_test.h"
#include "ui/screens/dashboard.h"
#include "ui/display_thread.h"
#include "comm/proxy_thread.h"
#include "services/imu/imu_fusion.h"
#include "services/notifications/whitelist.h"
#include "system/retained_mem.h"
#include "system/clock.h"
#include "ui/screen_dispatch.h"
#include "services/tasks/tasks.h"
#include "drivers/mic/dmic.h"
#include "services/audio/audio_stream.h"
#include "ui/screens/ui_quicknote.h"
#include "ui/screens/ui_onboarding.h"
#include "drivers/display/jbd_panel.h"
#include "services/storage/file_storage.h"
#include "services/storage/setting_store.h"
#include "services/storage/quicknote_store.h"
#include "services/production_test/pt_mic.h"
#include "ui/screens/ui_navigation_map.h"
#include "ui/screens/ui_teleprompter.h"

/* cmd 6 / sub 1: time sync */
struct put_time_sync {
	uint8_t hdr[5];
	uint32_t sec_ts;
	int64_t ms_ts;
} __packed;

/* quick notes (GlassesState->quicknote) */
struct qn_title {
	char str[0x29];
};

static uint16_t sched_cnt;
static uint16_t sched_total;
static uint16_t sched_len;
static uint16_t stock_cnt;
static uint16_t stock_total;
static uint16_t stock_len;
static uint16_t news_cnt;
static uint16_t news_total;
static uint16_t news_len;
static uint16_t citywalk_len;
static uint16_t tp_len;
static uint16_t pano_len;
static uint16_t pano_cnt;
static uint16_t pano_total;
static uint16_t qn_len;
static uint16_t qn_cnt;
static uint16_t qn_total;
static uint16_t nav_last_x;
static uint16_t nav_last_y;

static uint8_t citywalk_reload = 1;
static uint8_t BIT_MAP_CHUNK_ID = 0xff;
static uint8_t sched_buf[0x538];
static uint8_t stock_buf[0x812];
static uint8_t news_buf[0x161];
static uint8_t citywalk_last_disp_mode;
static uint8_t citywalk_bg_ready;
static struct teleprompter_info tp_buf;
static struct qn_title qn_title_buf;
static uint8_t qn_text_buf[0x119];
static uint8_t qn_num;
static uint8_t text_packet_buf[0x158];         /* translate / quick note packet buffer */

/* dashboard packet header (cmd 6 and most multi-packet commands) */
struct put_pkt_hdr {
	uint8_t cmd;
	uint16_t len;
	uint8_t sync_id;
	uint8_t sub;
	uint16_t total;
	uint16_t idx;      /* 1-based */
} __packed;

/* response helpers: copy the first n bytes of the request, set the status */
#define PUT_RSP(n, status)                                                                         \
	do {                                                                                       \
		memset(resp, 0, sizeof(resp));                                                     \
		memcpy(resp, data, n);                                                             \
		resp[n] = (status);                                                                \
	} while (0)

/* same without clearing the rest of resp first */
#define PUT_RSP_NC(n, status)                                                                      \
	do {                                                                                       \
		memcpy(resp, data, n);                                                             \
		resp[n] = (status);                                                                \
	} while (0)

/* quick note list entry (re-read through globle->quicknote on every access) */
#define QN_ITEM(n) (globle->quicknote->notes[n])

#define POST_TO_HOST(n)                                                                            \
	do {                                                                                       \
		ret = ctx->write(resp, n);                                                         \
		if (ret < 0) {                                                                     \
			G1_LOG_ERR("post_to_host failed ret = %d\n", ret);                         \
		}                                                                                  \
	} while (0)

/* little endian helpers */
#define GET_LE32(p)                                                                                \
	((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) |                   \
	 ((uint32_t)(p)[3] << 24))
#define GET_LE64(p)                                                                                \
	((uint64_t)(p)[0] | ((uint64_t)(p)[1] << 8) | ((uint64_t)(p)[2] << 16) |                   \
	 ((uint64_t)(p)[3] << 24) | ((uint64_t)(p)[4] << 32) | ((uint64_t)(p)[5] << 40) |          \
	 ((uint64_t)(p)[6] << 48) | ((uint64_t)(p)[7] << 56))

int save_esb_snapshot(const void *data, size_t len, uint8_t arg, int sel)
{
	uint8_t *buf;

	if (sel) {
		buf = get_dashboard_state()->esb_up_snapshot;
	} else {
		buf = get_dashboard_state()->esb_down_snapshot;
	}
	memcpy(buf, data, len);
	buf[ESB_SNAPSHOT_SEQ] = arg;
	return 0;
}

void set_esb_up_recv_state(uint8_t a, uint8_t b)
{
	get_dashboard_state()->esb_up_recving = b;
	get_dashboard_state()->esb_up_id = a;
}

int set_esb_recv_state(uint8_t a, uint8_t b, int sel)
{
	if (sel) {
		set_esb_up_recv_state(a, b);
	} else {
		get_dashboard_state()->esb_down_recving = b;
		get_dashboard_state()->esb_down_id = a;
	}
	return 0;
}

static void lock_nav_map_mutex(k_timeout_t timeout)
{
	k_mutex_lock(&nav_map_mutex, timeout);
}

static void unlock_nav_map_mutex(void)
{
	k_mutex_unlock(&nav_map_mutex);
}

/*
 * Length of the string at s including its terminating NUL, 0 if the string
 * does not end within max bytes.
 */
static uint32_t nav_field_len(const uint8_t *s, uint32_t max)
{
	uint32_t len;

	for (len = 0; len < max; len++) {
		if (s[len] == '\0') {
			return len + 1;
		}
	}
	return 0;
}

void ble_process_put_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data)
{
	struct GlassesState *globle = CONTAINER_OF(ctx, struct GlassesState, ble_context);
	uint8_t *tx = ctx->tx_buf;
	uint8_t *msg;
	uint8_t resp[20] = {0};
	uint8_t rx[0x100];
	int ret;
	uint32_t data_len;
	uint16_t total, idx;
	uint8_t i, num, sub;
	uint16_t dlen;

	memcpy(tx, hdr, sizeof(*hdr));
	msg = tx + sizeof(*hdr);
	ctx->tx_len = hdr->len + 4;
	resp[0] = hdr->cmd;

	switch (hdr->cmd) {
	case BLE_CMD_BRIGHTNESS_SET:
	case BLE_CMD_ANTI_SHAKE_SET:
	case BLE_CMD_SILENT_MODE_SET:
	case BLE_CMD_DEBUG_CONTROL:
	case BLE_CMD_HEAD_UP_ANGLE_SET:
	case BLE_CMD_NOTIFY_EN:
		memcpy(msg, &data[1], hdr->len - 1);
		G1_LOG_INF("msg_data:%02x, %02x, %02x, %02x\n", msg[0], msg[1], msg[2], msg[3]);
		ble_process_req_local(ctx, rx, 8);
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		break;
	case BLE_CMD_HEAD_UP_CALIBRATION:
		dlen = *(uint16_t *)&data[1];
		resp[1] = 6;
		resp[3] = data[3];
		resp[2] = 0;
		resp[4] = data[4];
		if (hdr->len == dlen && data[4] <= 2) {
			switch (data[4]) {
			case 1:
				if (get_dashboard_state()->imu.base != 0) {
					G1_LOG_WRN("ble clear user horiz pitch\n");
					get_dashboard_state()->imu.base = 0;
					get_dashboard_state()->dashboard_cmd = 0;
					k_sem_give(&get_dashboard_state()->dashboard_position_sem);
				}
				break;
			case 2:
				if (data[5] == 1) {
					get_dashboard_state()->horiz_pitch_cfg[0] = 1;
					get_dashboard_state()->horiz_pitch_cfg[1] = data[6];
					if (get_dashboard_state()->horiz_pitch_cfg[1] == 1) {
						G1_LOG_WRN("ble set user horiz pitch:%d\n",
							   imu_pitch_x100);
						get_dashboard_state()->imu.base = imu_pitch_x100;
						get_dashboard_state()->dashboard_cmd = 0;
						k_sem_give(&get_dashboard_state()
								    ->dashboard_position_sem);
					}
					globle->esb_down_id = data[3];
					update_persist_task_status(get_dashboard_state(), 0x11, 2);
				} else {
					get_dashboard_state()->horiz_pitch_cfg[0] = 0;
				}
				break;
			default:
				break;
			}
			resp[5] = PKCS7_PACK_DONE;
		} else {
			resp[5] = PKCS7_PACK_ERR;
		}
		ctx->write(resp, 20);
		break;
	case BLE_CMD_NOTIFI_WHITELIST_SET:
		put_whitelist_app_from_app(ctx, msg, data, (uint8_t)hdr->len, resp);
		break;
	case BLE_CMD_HEAD_UP_ACTION_SET:
		data_len = *(uint16_t *)&data[1];
		if (data_len != hdr->len) {
			G1_LOG_WRN(
				"packet length error,input data length = %d,packet data length = "
				"%d\n",
				hdr->len, data_len);
			break;
		}
		sub = data[4];
		G1_LOG_INF("received dashboard details packet\n");
		switch (sub) {
		case 4:
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, data_len);
			resp[5] = get_dashboard_startup_mode();
			ctx->write(resp, data_len);
			break;
		case 3:
			if (data[5] > 2) {
				G1_LOG_ERR("dashboard details packet error,invalid data\n");
				break;
			}
			if (is_master() == 1) {
				SendDashBoardStartupModeInfoToSlave(data[5]);
			}
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, data_len);
			ctx->write(resp, data_len);
			break;
		default:
			break;
		}
		break;
	case BLE_CMD_COUNTDOWN_TIMER:
		G1_LOG_INF("#BLE_REQ_PUT_COUNTDOWN_TIMER:\n");
		*(uint32_t *)&msg[0] = *(uint32_t *)&data[1];
		msg[4] = data[5];
		((struct ble_req_hdr *)tx)->len = 5;
		ctx->tx_len = 9;
		G1_LOG_INF("countdown expect_ts:%d, enable:%d\n", *(uint32_t *)&msg[0], msg[4]);
		G1_LOG_INF("msg_data: %02x,%02x,%02x,%02x,%02x\n", msg[0], msg[1], msg[2], msg[3],
			   msg[4]);
		ble_process_req_local(ctx, rx, 8);
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		break;
	case BLE_CMD_DASHBOARD_SET: {
		struct put_pkt_hdr *ph = (struct put_pkt_hdr *)data;

		data_len = ph->len;
		if (data_len != hdr->len) {
			G1_LOG_ERR("dashboard information packet length error,input data length = "
				   "%d,packet data length = %d\n",
				   hdr->len, data_len);
			PUT_RSP(5, 1);
			ctx->write(resp, 20);
			return;
		}
		G1_LOG_INF("received sync id = %d\n", data[3]);
		switch (data[4]) {
		case DASHBOARD_SUB_TIME_WEATHER: {
			int64_t ms_ts = ((struct put_time_sync *)data)->ms_ts;
			uint32_t sec_ts;
			struct bp_date date;

			set_wall_clock_ms(ms_ts);
			sec_ts = ((struct put_time_sync *)data)->sec_ts;
			G1_LOG_INF("origin ms timestamp = %lld,origin second timestamp = %d\n",
				   ms_ts, sec_ts);
			globle->dashboard_ts->weather_icon = data[0x11];
			globle->dashboard_ts->temperature = data[0x12];
			globle->dashboard_ts->ts = sec_ts;
			globle->dashboard_ts->ms_ts = ms_ts;
			memset(&date, 0, sizeof(date));
			timestamp_to_date(sec_ts, &date);
			G1_LOG_INF("date: %u: %04d/%02d/%02d-%02d:%02d:%02d\n", sec_ts, date.year,
				   date.month, date.day, date.hour, date.minute, date.second);
			accumulate_elapsed_seconds((uint32_t *)(void *)globle->dashboard_ts, 0, 1);
			globle->dashboard_ts->temp_unit = data[0x13];
			globle->dashboard_ts->time_disp_mode = data[0x14];
			if (data_len > 0x15 && globle->dashboard_ts->date_format != data[0x15]) {
				globle->dashboard_ts->date_format = data[0x15];
				storage_commit_request(0, 0);
			}
			G1_LOG_INF(
				"time display format :%s hours, temperature display format %s \n",
				globle->dashboard_ts->time_disp_mode ? "12" : "24",
				globle->dashboard_ts->time_disp_mode ? "Fahrenheit"
								     : "centigrade degree");
			save_dashboard_ts_to_ncs_mem();
			set_ncs_mem_initialized();
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 5);
			ctx->write(resp, 6);
			break;
		}
		case DASHBOARD_SUB_WEATHER:
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 5);
			ctx->write(resp, 6);
			break;
		case DASHBOARD_SUB_PANE_MODE:
			globle->dashboard_ts->disp_mode = data[5];
			globle->dashboard_ts->area = data[6];
			if (globle->dashboard_ts->area == 4 &&
			    globle->dashboard_ts->disp_mode != citywalk_last_disp_mode) {
				citywalk_last_disp_mode = globle->dashboard_ts->disp_mode;
				get_dashboard_state()->dashboard_ts->citywalk_mode = 0;
				citywalk_reload = 1;
				citywalk_bg_ready = 0;
			}
			G1_LOG_INF("Dashboard display mode :%d custom display Area value : %d\n",
				   globle->dashboard_ts->disp_mode, globle->dashboard_ts->area);
			save_dashboard_ts_to_ncs_mem();
			set_ncs_mem_initialized();
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 5);
			ctx->write(resp, 6);
			break;
		case DASHBOARD_SUB_PANE_CALENDAR: /* schedule */
			idx = *(uint16_t *)&data[7];
			total = *(uint16_t *)&data[5];
			if (total == 1) {
				const uint8_t *p;

				get_dashboard_state()->dashboard_ts->disp_mode = data[9];
				get_dashboard_state()->dashboard_ts->area = data[10];
				get_dashboard_state()->dashboard_ts->sched_mode = data[0xb];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("schedule display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->sched_mode);
				if (get_dashboard_state()->dashboard_ts->sched_mode == 3) {
					struct schedule_item *item;

					num = data[0xc];
					G1_LOG_INF("schedule total record num = %d\n", num);
					p = &data[0xd];
					if (num > 8) {
						G1_LOG_ERR("schedule data oversize!!!!\n");
						return;
					}
					memset(schedule_items, 0, sizeof(schedule_items));
					item = schedule_items;
					for (i = 0; i != num; i++, item++) {
						if (p[0] == 1) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"schedule title data format error!!!\n");
							}
							memset(item, 0, sizeof(*item));
							memcpy(item->title, &p[2], MIN(p[1], 0x40));
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"schedule title data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							if (p[1] > 0x20) {
								G1_LOG_WRN(
									"schedule time data format error!!!\n");
							}
							memcpy(item->time, &p[2], MIN(p[1], 0x20));
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"schedule time data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 3) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"schedule location data format error!!!\n");
							}
							item->valid = 1;
							memcpy(item->location, &p[2],
							       MIN(p[1], 0x40));
							p += p[1] + 2;
							G1_LOG_INF(
								"schedule title : %s , time :%s , location : %s \n",
								item->title, item->time,
								item->location);
						} else {
							G1_LOG_ERR(
								"schedule time data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
					}
					if (get_dashboard_state()->dashboard_ts->sched_num != i) {
						cleanCalenadrIndex();
					}
					get_dashboard_state()->dashboard_ts->sched_num = i;
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
					return;
				}
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
				return;
			}
			if (idx == 1) {
				G1_LOG_INF(
					"Action ID = 1,Received schedule multiple initialization "
					"packets, currently the first packet of data\n");
				sched_cnt = 0;
				sched_total = 0;
				sched_len = 0;
				memset(sched_buf, 0, sizeof(sched_buf));
				memcpy(sched_buf, &data[9], data_len - 9);
				sched_len += data_len - 9;
				sched_cnt++;
				sched_total = total;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
				return;
			}
			if (idx < total) {
				if (sched_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order = "
						"%d, expected packet order =%d\n",
						idx, sched_cnt + 1);
					sched_total = 0;
					sched_cnt = 0;
					memset(sched_buf, 0, sizeof(sched_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&sched_buf[sched_len], &data[9], data_len - 9);
				sched_len += data_len - 9;
				sched_cnt++;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
				return;
			}
			if (idx == total) {
				const uint8_t *p;

				if (sched_cnt != idx - 1 || sched_total != idx) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, sched_cnt + 1);
					sched_total = 0;
					sched_cnt = 0;
					memset(sched_buf, 0, sizeof(sched_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&sched_buf[sched_len], &data[9], data_len - 9);
				sched_len += data_len - 9;
				sched_cnt++;
				get_dashboard_state()->dashboard_ts->disp_mode = sched_buf[0];
				get_dashboard_state()->dashboard_ts->area = sched_buf[1];
				get_dashboard_state()->dashboard_ts->sched_mode = sched_buf[2];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("schedule display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->sched_mode);
				if (get_dashboard_state()->dashboard_ts->sched_mode == 3) {
					struct schedule_item *item;

					num = sched_buf[3];
					G1_LOG_INF("schedule record num = %d\n", num);
					if (num > 8) {
						G1_LOG_ERR("schedule data oversize!!!!\n");
						return;
					}
					memset(schedule_items, 0, sizeof(schedule_items));
					p = &sched_buf[4];
					item = schedule_items;
					for (i = 0; i != num; i++, item++) {
						if (p[0] == 1) {
							if (p[1] > 0x41) {
								G1_LOG_WRN(
									"schedule title data format error!!!\n");
							}
							memset(item, 0, sizeof(*item));
							memcpy(item->title, &p[2], MIN(p[1], 0x41));
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"schedule title data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							if (p[1] > 0x21) {
								G1_LOG_WRN(
									"schedule time data format error!!!\n");
							}
							memcpy(item->time, &p[2], MIN(p[1], 0x21));
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"schedule time data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 3) {
							if (p[1] > 0x41) {
								G1_LOG_WRN(
									"schedule time data format error!!!\n");
							}
							item->valid = 1;
							memcpy(item->location, &p[2],
							       MIN(p[1], 0x41));
							p += p[1] + 2;
							G1_LOG_INF(
								"schedule title : %s , time :%s , location : %s , "
								"schedule_validity :%d\n",
								item->title, item->time,
								item->location, 1);
						} else {
							G1_LOG_ERR(
								"schedule time data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
					}
					if (get_dashboard_state()->dashboard_ts->sched_num != i) {
						cleanCalenadrIndex();
					}
					get_dashboard_state()->dashboard_ts->sched_num = i;
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
					return;
				}
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
				return;
			}
			return;
		case DASHBOARD_SUB_PANE_STOCK: /* stocks */
			idx = *(uint16_t *)&data[7];
			total = *(uint16_t *)&data[5];
			if (total == 1) {
				get_dashboard_state()->dashboard_ts->disp_mode = data[9];
				get_dashboard_state()->dashboard_ts->area = data[10];
				get_dashboard_state()->dashboard_ts->stocks_mode = data[0xb];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("stocks display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->stocks_mode);
				if (get_dashboard_state()->dashboard_ts->stocks_mode == 2) {
					num = data[0xc];
					if (num >= 5) {
						G1_LOG_ERR("stocks data oversize!!!!\n");
						PUT_RSP(9, 1);
						POST_TO_HOST(10);
						break;
					}
					G1_LOG_INF("current stocks index num = %d\n", num);
					if (data[0xd] == 2) {
						G1_LOG_INF("delete stocks index num = %d\n", num);
						memset(&stock_items[num - 1], 0,
						       sizeof(stock_items[0]));
						cleanStocksIndex();
					} else {
						struct stock_item *st;
						const uint8_t *p;
						uint16_t img_len;

						if (data[0xe] == 1) {
							if (data[0xf] > 0x20) {
								G1_LOG_WRN(
									"stocks code oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							st = &stock_items[num - 1];
							if (memcmp(st->code, &data[0x10],
								   MIN(data[0xf], 0x20)) != 0) {
								memset(st->code, 0,
								       sizeof(st->code));
								cleanStocksIndex();
							}
							memcpy(st->code, &data[0x10],
							       MIN(data[0xf], 0x20));
							G1_LOG_INF("stock code name : %s\n",
								   st->code);
							p = &data[data[0xf] + 0x10];
						} else {
							G1_LOG_ERR(
								"stock code data struct error, error type = "
								"%d\n",
								data[0xe]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"stocks share price oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							if (memcmp(st->company, &p[2],
								   MIN(p[1], 0x40)) != 0) {
								memset(st->company, 0,
								       sizeof(st->company));
								cleanStocksIndex();
							}
							memcpy(st->company, &p[2], MIN(p[1], 0x40));
							p += p[1] + 2;
							G1_LOG_INF("stock company name : %s\n",
								   st->company);
						} else {
							G1_LOG_ERR(
								"stocks company name data struct error, "
								"error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 3) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks share price oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->price, 0, sizeof(st->price));
							memcpy(st->price, &p[2], MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock price  : %s\n",
								   st->price);
						} else {
							G1_LOG_ERR(
								"stocks share price data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 4) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks price change oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->price_change, 0,
							       sizeof(st->price_change));
							memcpy(st->price_change, &p[2],
							       MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock price change  : %s\n",
								   st->price_change);
						} else {
							G1_LOG_ERR(
								"stocks price change data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 5) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks price change oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->change_rate, 0,
							       sizeof(st->change_rate));
							memcpy(st->change_rate, &p[2],
							       MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock change rate : %s\n",
								   st->change_rate);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 6) {
							if (p[1] != 1) {
								G1_LOG_ERR(
									"stocks change status sformat error !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							st->change_status = p[2];
							p += p[1] + 2;
							G1_LOG_INF("stock change status : %d\n",
								   st->change_status);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 7) {
							img_len = *(uint16_t *)&p[1];
							if (img_len > sizeof(st->image)) {
								G1_LOG_ERR(
									"stocks image format oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							st->image_len = img_len;
							memset(st->image, 0, sizeof(st->image));
							memcpy(st->image, &p[3], img_len);
							st->valid = 1;
							G1_LOG_INF(
								"We received %d byte of data from the "
								"App\n",
								st->image_len);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
					}
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				} else {
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				}
			} else if (idx == 1) {
				G1_LOG_INF(
					"Action ID = 1,Received schedule multiple initialization "
					"packets, currently the first packet of data\n");
				stock_cnt = 0;
				stock_total = 0;
				stock_len = 0;
				memset(stock_buf, 0, sizeof(stock_buf));
				memcpy(stock_buf, &data[9], data_len - 9);
				stock_len += data_len - 9;
				stock_cnt++;
				stock_total = total;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
			} else if (idx < total) {
				if (stock_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, sched_cnt + 1);
					stock_total = 0;
					stock_cnt = 0;
					memset(stock_buf, 0, sizeof(stock_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&stock_buf[stock_len], &data[9], data_len - 9);
				stock_len += data_len - 9;
				stock_cnt++;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
			} else if (idx == total) {
				if (stock_cnt != idx - 1 || stock_total != idx) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, stock_cnt + 1);
					stock_total = 0;
					stock_cnt = 0;
					memset(stock_buf, 0, sizeof(stock_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&stock_buf[stock_len], &data[9], data_len - 9);
				stock_len += data_len - 9;
				stock_cnt++;
				get_dashboard_state()->dashboard_ts->disp_mode = stock_buf[0];
				get_dashboard_state()->dashboard_ts->area = stock_buf[1];
				get_dashboard_state()->dashboard_ts->stocks_mode = stock_buf[2];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("stocks display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->stocks_mode);
				if (get_dashboard_state()->dashboard_ts->stocks_mode == 2) {
					num = stock_buf[3];
					if (num >= 5) {
						G1_LOG_ERR("stocks data oversize!!!!\n");
						PUT_RSP(9, 1);
						POST_TO_HOST(10);
						return;
					}
					G1_LOG_INF("current stocks index num = %d\n", num);
					if (stock_buf[4] == 2) {
						G1_LOG_INF("delete stocks index num = %d\n", num);
						cleanStocksIndex();
						memset(&stock_items[num - 1], 0,
						       sizeof(stock_items[0]));
						PUT_RSP(9, 0);
						POST_TO_HOST(10);
					} else {
						const uint8_t *p = &stock_buf[5];
						struct stock_item *st;
						uint16_t img_len;

						if (p[0] == 1) {
							if (p[1] > 0x20) {
								G1_LOG_WRN(
									"stocks code oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							st = &stock_items[num - 1];
							if (memcmp(st->code, &p[2],
								   MIN(p[1], 0x20)) != 0) {
								memset(st->code, 0,
								       sizeof(st->code));
								cleanStocksIndex();
							}
							memcpy(st->code, &p[2], MIN(p[1], 0x20));
							G1_LOG_INF("stock code name : %s\n",
								   st->code);
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"stock code data struct error, error type = "
								"%d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"stocks share price oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							if (memcmp(st->company, &p[2],
								   MIN(p[1], 0x40)) != 0) {
								memset(st->company, 0,
								       sizeof(st->company));
								cleanStocksIndex();
							}
							memcpy(st->company, &p[2], MIN(p[1], 0x40));
							p += p[1] + 2;
							G1_LOG_INF("stock company name : %s\n",
								   st->company);
						} else {
							G1_LOG_ERR(
								"stocks company name data struct error, "
								"error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 3) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks share price oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->price, 0, sizeof(st->price));
							memcpy(st->price, &p[2], MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock price  : %s\n",
								   st->price);
						} else {
							G1_LOG_ERR(
								"stocks share price data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 4) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks price change oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->price_change, 0,
							       sizeof(st->price_change));
							memcpy(st->price_change, &p[2],
							       MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock price change  : %s\n",
								   st->price_change);
						} else {
							G1_LOG_ERR(
								"stocks price change data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 5) {
							if (p[1] > 0x10) {
								G1_LOG_WRN(
									"stocks price change oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							memset(st->change_rate, 0,
							       sizeof(st->change_rate));
							memcpy(st->change_rate, &p[2],
							       MIN(p[1], 0x10));
							p += p[1] + 2;
							G1_LOG_INF("stock change rate : %s\n",
								   st->change_rate);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 6) {
							if (p[1] != 1) {
								G1_LOG_ERR(
									"stocks change status sformat error !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							st->change_status = p[2];
							p += p[1] + 2;
							G1_LOG_INF("stock change status : %d\n",
								   st->change_status);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 7) {
							img_len = *(uint16_t *)&p[1];
							if (img_len > sizeof(st->image)) {
								G1_LOG_ERR(
									"stocks image format oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							st->image_len = img_len;
							memset(st->image, 0, sizeof(st->image));
							memcpy(st->image, &p[3], img_len);
							st->valid = 1;
							G1_LOG_INF(
								"We received %d byte of data from the App\n",
								st->image_len);
							stock_total = 0;
							stock_cnt = 0;
							PUT_RSP(9, 0);
						} else {
							G1_LOG_ERR(
								"stocks change rate data struct error, error "
								"type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						POST_TO_HOST(10);
					}
				} else {
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				}
			}
			break;
		case DASHBOARD_SUB_PANE_NEWS: /* news */
			total = *(uint16_t *)&data[5];
			idx = *(uint16_t *)&data[7];
			if (total == 1) {
				get_dashboard_state()->dashboard_ts->disp_mode = data[9];
				get_dashboard_state()->dashboard_ts->area = data[10];
				get_dashboard_state()->dashboard_ts->news_mode = data[0xb];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("news display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->news_mode);
				if (get_dashboard_state()->dashboard_ts->news_mode == 2) {
					num = data[0xc];
					if (num >= 5) {
						G1_LOG_ERR("news index num  oversize!!!!\n");
						PUT_RSP(9, 1);
						POST_TO_HOST(10);
						break;
					}
					G1_LOG_INF("current news index num = %d\n", num);
					if (data[0xd] == 2) {
						G1_LOG_INF("delete news index num = %d\n", num);
						memset(&news_items[num - 1], 0,
						       sizeof(news_items[0]));
						cleanNewsIndex();
					} else {
						const uint8_t *p = &data[0xe];
						struct news_item *nw;
						uint16_t txt_len;

						if (p[0] == 1) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"news source oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							nw = &news_items[num - 1];
							if (memcmp(nw->source, &p[2],
								   MIN(p[1], 0x40)) != 0) {
								memset(nw->source, 0,
								       sizeof(nw->source));
								cleanNewsIndex();
							}
							memcpy(nw->source, &p[2], MIN(p[1], 0x40));
							G1_LOG_INF("news source : %s\n",
								   nw->source);
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"news data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							txt_len = *(uint16_t *)&p[1];
							if (txt_len > sizeof(nw->text)) {
								G1_LOG_ERR(
									"news txt format oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							if (memcmp(nw->text, &p[2], p[1]) != 0) {
								memset(nw->text, 0,
								       sizeof(nw->text));
								cleanNewsIndex();
							}
							memcpy(nw->text, &p[3], txt_len);
							nw->valid = 1;
							G1_LOG_INF(
								"We received %d byte of data from the News App\n",
								txt_len);
						} else {
							G1_LOG_ERR(
								"news change rate data struct error, "
								"error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
					}
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				} else {
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				}
			} else if (idx == 1) {
				G1_LOG_INF(
					"Action ID = 1,Received schedule multiple initialization "
					"packets, currently the first packet of data\n");
				news_cnt = 0;
				news_total = 0;
				news_len = 0;
				memset(news_buf, 0, sizeof(news_buf));
				memcpy(news_buf, &data[9], data_len - 9);
				news_len += data_len - 9;
				news_cnt++;
				news_total = total;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
			} else if (idx < total) {
				if (news_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, news_cnt + 1);
					news_total = 0;
					news_cnt = 0;
					memset(news_buf, 0, sizeof(news_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&news_buf[news_len], &data[9], data_len - 9);
				news_len += data_len - 9;
				news_cnt++;
				PUT_RSP(9, 0);
				POST_TO_HOST(10);
			} else if (total == idx) {
				if (news_cnt != total - 1 || news_total != total) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, news_cnt + 1);
					news_total = 0;
					news_cnt = 0;
					memset(news_buf, 0, sizeof(news_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&news_buf[news_len], &data[9], data_len - 9);
				news_len += data_len - 9;
				news_cnt++;
				get_dashboard_state()->dashboard_ts->disp_mode = news_buf[0];
				get_dashboard_state()->dashboard_ts->area = news_buf[1];
				get_dashboard_state()->dashboard_ts->news_mode = news_buf[2];
				G1_LOG_INF(
					"Dashboard display mode :%d  custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("news display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->news_mode);
				if (get_dashboard_state()->dashboard_ts->news_mode == 2) {
					num = news_buf[3];
					if (num >= 5) {
						G1_LOG_ERR("stocks data oversize!!!!\n");
						PUT_RSP(9, 1);
						POST_TO_HOST(10);
						return;
					}
					G1_LOG_INF("current news index num = %d\n", num);
					if (news_buf[4] == 2) {
						G1_LOG_INF("delete news index num = %d\n", num);
						memset(&news_items[num - 1], 0,
						       sizeof(news_items[0]));
						PUT_RSP(9, 0);
						POST_TO_HOST(10);
					} else {
						const uint8_t *p = &news_buf[5];
						struct news_item *nw;
						uint16_t txt_len;

						if (p[0] == 1) {
							if (p[1] > 0x40) {
								G1_LOG_WRN(
									"news source oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
							}
							nw = &news_items[num - 1];
							if (memcmp(nw->source, &p[2],
								   MIN(p[1], 0x40)) != 0) {
								memset(nw->source, 0,
								       sizeof(nw->source));
								cleanNewsIndex();
							}
							memcpy(nw->source, &p[2], MIN(p[1], 0x40));
							G1_LOG_INF("stock code name : %s\n",
								   nw->source);
							p += p[1] + 2;
						} else {
							G1_LOG_ERR(
								"news data struct error, error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						if (p[0] == 2) {
							txt_len = *(uint16_t *)&p[1];
							if (txt_len > sizeof(nw->text)) {
								G1_LOG_ERR(
									"news txt format oversize !!!\n");
								PUT_RSP(9, 1);
								POST_TO_HOST(10);
								return;
							}
							if (memcmp(nw->text, &p[2], p[1]) != 0) {
								memset(nw->text, 0,
								       sizeof(nw->text));
								cleanNewsIndex();
							}
							memcpy(nw->text, &p[3], txt_len);
							nw->valid = 1;
							G1_LOG_INF(
								"We received %d byte of data from the News App\n",
								txt_len);
							news_total = 0;
							news_cnt = 0;
							PUT_RSP(9, 0);
						} else {
							G1_LOG_ERR(
								"news change rate data struct error, "
								"error type = %d\n",
								p[0]);
							PUT_RSP(9, 1);
							POST_TO_HOST(10);
							return;
						}
						POST_TO_HOST(10);
					}
				} else {
					PUT_RSP(9, 0);
					POST_TO_HOST(10);
				}
			}
			break;
		case DASHBOARD_SUB_PANE_CANVAS: /* citywalk */
			memcpy(resp, data, 9);
			resp[9] = 0;
			total = *(uint16_t *)&data[5];
			idx = *(uint16_t *)&data[7];
			if (total == 1) {
				get_dashboard_state()->dashboard_ts->disp_mode = data[9];
				get_dashboard_state()->dashboard_ts->area = data[10];
				get_dashboard_state()->dashboard_ts->citywalk_mode = data[0xb];
				G1_LOG_INF(
					"Dashboard display mode :%d custom display Area value : %d\n",
					globle->dashboard_ts->disp_mode,
					globle->dashboard_ts->area);
				G1_LOG_INF("CityWalk display mode = %d\n",
					   get_dashboard_state()->dashboard_ts->citywalk_mode);
				if (get_dashboard_state()->dashboard_ts->area != 4) {
					return;
				}
				if (get_dashboard_state()->dashboard_ts->citywalk_mode == 2) {
					return;
				}
				if (get_dashboard_state()->dashboard_ts->citywalk_mode == 3) {
					get_dashboard_state()->dashboard_ts->arrow_x =
						*(uint16_t *)&data[0xc];
					if (get_dashboard_state()->dashboard_ts->arrow_x & 1) {
						get_dashboard_state()->dashboard_ts->arrow_x--;
					}
					get_dashboard_state()->dashboard_ts->arrow_y =
						*(uint16_t *)&data[0xe];
					if (get_dashboard_state()->dashboard_ts->disp_mode == 0) {
						if (get_dashboard_state()->dashboard_ts->arrow_x >
							    0x108 ||
						    get_dashboard_state()->dashboard_ts->arrow_y >
							    0x68) {
							G1_LOG_ERR(
								"citywalk arrow offset too long!\n");
							return;
						}
						memcpy(&panoramic_map_buf[0x13ac], &data[0x10],
						       data_len - 0x10);
					} else if (get_dashboard_state()->dashboard_ts->disp_mode ==
						   1) {
						if (get_dashboard_state()->dashboard_ts->arrow_x >
							    0x158 ||
						    get_dashboard_state()->dashboard_ts->arrow_y >
							    0x68) {
							G1_LOG_ERR(
								"citywalk arrow offset too long!\n");
							return;
						}
						memcpy(&panoramic_map_buf[0x18fc], &data[0x10],
						       data_len - 0x10);
					}
					if (get_dashboard_state()->dashboard_ts->disp_mode !=
					    citywalk_last_disp_mode) {
						citywalk_last_disp_mode =
							get_dashboard_state()
								->dashboard_ts->disp_mode;
						get_dashboard_state()->dashboard_ts->citywalk_mode =
							0;
						citywalk_reload = 1;
						citywalk_bg_ready = 0;
					} else if (citywalk_bg_ready == 1) {
						citywalk_reload = 0;
						if (get_dashboard_state()
							    ->dashboard_ts->citywalk_state == 2) {
							memcpy(&panoramic_map_buf[0x197c],
							       panoramic_map_buf, 0x197c);
						} else if (get_dashboard_state()
								   ->dashboard_ts->disp_mode == 0) {
							memcpy(&panoramic_map_buf[0x197c + 0x13ac],
							       &panoramic_map_buf[0x13ac],
							       data_len - 0x10);
						} else if (get_dashboard_state()
								   ->dashboard_ts->disp_mode == 1) {
							memcpy(&panoramic_map_buf[0x197c + 0x18fc],
							       &panoramic_map_buf[0x18fc],
							       data_len - 0x10);
						}
						get_dashboard_state()
							->dashboard_ts->citywalk_state = 3;
						citywalk_canvas_valid = 1;
					}
					if (citywalk_reload == 1) {
						get_dashboard_state()->dashboard_ts->citywalk_mode =
							0;
					}
				}
			} else if (idx == 1) {
				G1_LOG_INF(
					"Received citywalk multiple packets, currently the first "
					"packet of data\n");
				if (data[10] != 4) {
					return;
				}
				multi_packet_total = total;
				multi_packet_cnt = 1;
				if (data[0xb] == 2) {
					citywalk_bg_ready = 0;
					get_dashboard_state()->dashboard_ts->citywalk_state = 1;
					memset(panoramic_map_buf, 0, 0x197c);
					memcpy(panoramic_map_buf, &data[9], data_len - 9);
					citywalk_len = data_len - 9;
				}
			} else if (idx < total) {
				if (get_dashboard_state()->dashboard_ts->citywalk_state == 1) {
					citywalk_bg_ready = 0;
					if (multi_packet_cnt != idx - 1) {
						G1_LOG_WRN(
							"There is a packet order error, current packet "
							"order = %d, expected packet order = %d\n",
							idx, multi_packet_cnt + 1);
						multi_packet_total = total;
						multi_packet_cnt = 0;
						citywalk_len = 0;
						memset(panoramic_map_buf, 0, 0x197c);
						return;
					}
					if (citywalk_len - 9 + data_len > 0x197c) {
						G1_LOG_ERR(
							"citywalk data is too long, the current data "
							"length %d,wait write data length = %d\n",
							citywalk_len, data_len - 9);
						return;
					}
					memcpy(&panoramic_map_buf[citywalk_len], &data[9],
					       data_len - 9);
					citywalk_len += data_len - 9;
					G1_LOG_INF(
						"Successfully received the subpackage content, the "
						"packet sequence is %d\n",
						idx);
					multi_packet_cnt++;
				}
			} else if (total == idx) {
				if (get_dashboard_state()->dashboard_ts->citywalk_state == 1) {
					if (multi_packet_cnt != total - 1 ||
					    multi_packet_total != total) {
						G1_LOG_WRN(
							"There is a packet order error, current packet "
							"order = %d, expected packet order = %d\n",
							idx, multi_packet_cnt + 1);
						multi_packet_total = total;
						multi_packet_cnt = 0;
						citywalk_len = 0;
						citywalk_bg_ready = 0;
						memset(panoramic_map_buf, 0, 0x197c);
						return;
					}
					if (citywalk_len - 9 + data_len > 0x197c) {
						G1_LOG_ERR(
							"citywalk data is too long, the current data "
							"length %d,wait write data length = %d\n",
							citywalk_len, data_len - 9);
						citywalk_bg_ready = 0;
						return;
					}
					G1_LOG_ERR(
						"Successfully assembled multiple packages of data, and "
						"updated the complete content to the UI thread for "
						"data update.\n");
					memcpy(&panoramic_map_buf[citywalk_len], &data[9],
					       data_len - 9);
					citywalk_len += data_len - 9;
					get_dashboard_state()->dashboard_ts->disp_mode =
						panoramic_map_buf[0];
					get_dashboard_state()->dashboard_ts->area =
						panoramic_map_buf[1];
					if (citywalk_reload == 1) {
						get_dashboard_state()->dashboard_ts->citywalk_mode =
							0;
					}
					get_dashboard_state()->dashboard_ts->citywalk_bg_param =
						panoramic_map_buf[3];
					citywalk_bg_ready = 1;
					get_dashboard_state()->dashboard_ts->citywalk_state = 2;
					if ((get_dashboard_state()->dashboard_ts->disp_mode == 0 &&
					     citywalk_len != 0x13ac) ||
					    (get_dashboard_state()->dashboard_ts->disp_mode == 1 &&
					     citywalk_len != 0x18fc)) {
						citywalk_reload = 1;
						citywalk_bg_ready = 0;
						get_dashboard_state()
							->dashboard_ts->citywalk_state = 0;
					}
				}
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				citywalk_len = 0;
			}
			ctx->write(resp, 10);
			break;
		default:
			break;
		}
		break;
	}
	case BLE_CMD_STATUS:
		if (*(uint16_t *)&data[1] != hdr->len) {
			G1_LOG_ERR("dashboard information packet length error,input data length = "
				   "%d,packet data length = %d\n",
				   hdr->len, *(uint16_t *)&data[1]);
			break;
		}
		G1_LOG_INF("received sync id = %d\n", data[3]);
		if (data[4] == 1) {
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 8);
			resp[5] = globle->unread_msg_num;
			resp[6] = getNotesIndex() + 1;
			resp[7] = globle->dashboard_ts->low_power;
			ctx->write(resp, 8);
		}
		break;
	case BLE_CMD_TELEPROMPTER_INFO: { /* teleprompter */
		struct put_pkt_hdr *ph = (struct put_pkt_hdr *)data;
		struct teleprompter_info *tp;

		data_len = ph->len;
		if (data_len != hdr->len) {
			G1_LOG_ERR("teleprompter packet length error,input data length = %d,packet "
				   "data length = %d\n",
				   hdr->len, data_len);
			return;
		}
		sub = data[4];
		switch (sub) {
		case 1:
			G1_LOG_INF("received teleprompter init  packet ....\n");
			memset(msg, 0, sizeof(struct teleprompter_info));
			total = *(uint16_t *)&data[5];
			idx = *(uint16_t *)&data[7];
			if (total == 1) {
				int64_t now;

				G1_LOG_INF("received teleprompter init packet,total packet = 1\n");
				tp = (struct teleprompter_info *)msg;
				globle->esb_down_id = data[3];
				set_esb_recv_state(data[3], 0, 0);
				tp->action = 1;
				tp->ready = 1;
				tp->mode = data[10] & 0xf;
				tp->mode_flag = data[10] & 0x80;
				tp->countdown_s = data[9];
				tp->scroll_pct = data[0xb];
				tp->mark_pos = 0;
				tp->suspend = 0;
				memcpy(tp->text, &data[0xc], data_len - 0x14);
				tp->len = data_len - 0x14;
				tp->ts = GET_LE64(&data[data_len - 8]);
				now = 0;
				get_wall_clock_ms(&now);
				G1_LOG_ERR(
					"*******************------ received app send timestamp = "
					"%lld,current system timestamp = %lld\n",
					tp->ts, now);
				((struct ble_req_hdr *)tx)->len = hdr->len;
				ctx->tx_len = hdr->len + 4;
				ble_process_req_local(ctx, rx, 8);
				PUT_RSP(9, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 10);
				} else {
					POST_TO_HOST(10);
				}
			} else if (idx == 1) {
				G1_LOG_INF(
					"Action ID = 1,Received multiple initialization packets, "
					"currently the first packet of data\n");
				multi_packet_total = total;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				tp_len = 0;
				memset(&tp_buf, 0, sizeof(tp_buf));
				tp_buf.action = 1;
				tp_buf.ready = 0;
				tp_buf.mode = data[10] & 0xf;
				tp_buf.mode_flag = data[10] & 0x80;
				tp_buf.countdown_s = data[9];
				tp_buf.scroll_pct = data[0xb];
				tp_buf.mark_pos = 0;
				tp_buf.ts = 0;
				memcpy(tp_buf.text, &data[0xc], data_len - 0xc);
				tp_len = data_len - 0xc;
				multi_packet_cnt++;
				PUT_RSP_NC(9, 0);
				POST_TO_HOST(10);
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(&tp_buf, 0, sizeof(tp_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy(&tp_buf.text[tp_len], &data[0xc], data_len - 0xc);
				tp_len += data_len - 0xc;
				G1_LOG_INF("ACTION ID = 1,Successfully received the subpackage "
					   "content, the packet sequence is %d\n",
					   idx);
				multi_packet_cnt++;
				PUT_RSP_NC(9, 0);
				POST_TO_HOST(10);
			} else if (idx == total) {
				int64_t now;

				if (multi_packet_cnt != idx - 1 || multi_packet_total != idx) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					memset(&tp_buf, 0, sizeof(tp_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				globle->esb_down_id = data[3];
				set_esb_recv_state(data[3], 0, 0);
				memcpy(&tp_buf.text[tp_len], &data[0xc], data_len - 0x14);
				tp_len += data_len - 0x14;
				tp_buf.len = tp_len;
				tp_buf.ts = GET_LE64(&data[data_len - 8]);
				now = 0;
				get_wall_clock_ms(&now);
				G1_LOG_INF("*******************------ received app send timestamp "
					   "= %lld,current system timestamp = %lld\n",
					   tp_buf.ts, now);
				multi_packet_cnt++;
				tp_buf.ready = 1;
				memcpy(get_dashboard_state()->teleprompter, &tp_buf,
				       sizeof(tp_buf));
				if (tp_buf.ready) {
					update_persist_task_status(globle, 9, 2);
				}
				G1_LOG_INF("ACTION ID = 1,Successfully assembled multiple packages "
					   "of data, and updated the complete content to the UI "
					   "thread for data update.\n");
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				PUT_RSP_NC(9, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 10);
				} else {
					POST_TO_HOST(10);
				}
			} else {
				G1_LOG_ERR("A serious packet order error occurred!\n");
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				memset(&tp_buf, 0, sizeof(tp_buf));
				PUT_RSP_NC(9, 1);
				POST_TO_HOST(10);
			}
			break;
		case 2:
			globle->esb_down_id = data[3];
			set_esb_recv_state(data[3], 0, 0);
			ctx->tx_len = ((struct ble_req_hdr *)tx)->len + 4;
			globle->teleprompter->mark_pos = *(uint16_t *)&data[6];
			globle->teleprompter->scroll_pct = data[5];
			globle->teleprompter->action = 2;
			globle->teleprompter->ts = GET_LE64(&data[data_len - 8]);
			G1_LOG_INF("MARK POSITION = %d\n", globle->teleprompter->mark_pos);
			PUT_RSP(5, 0);
			if (is_master() == 1) {
				send_response_data_to_msgqueue(resp, 6);
			} else {
				POST_TO_HOST(6);
			}
			break;
		case 3:
		case 7:
			G1_LOG_INF("received teleprompter text upgrade packet ....\n");
			total = *(uint16_t *)&data[5];
			idx = *(uint16_t *)&data[7];
			if (total == 1) {
				int64_t now;

				G1_LOG_INF("received TEXT UPGRADE Single PACKET ...\n");
				globle->esb_down_id = data[3];
				set_esb_recv_state(data[3], 0, 0);
				globle->teleprompter->action = 3;
				globle->teleprompter->ready = 1;
				globle->teleprompter->scroll_pct = data[9];
				globle->teleprompter->mark_pos = *(uint16_t *)&data[10];
				memset(globle->teleprompter->text, 0,
				       sizeof(globle->teleprompter->text));
				memcpy(globle->teleprompter->text, &data[0xc], data_len - 0x14);
				globle->teleprompter->len = data_len - 0x14;
				globle->teleprompter->ts = GET_LE64(&data[data_len - 8]);
				now = 0;
				get_wall_clock_ms(&now);
				G1_LOG_INF(
					"*******************------ received app send timestamp = "
					"%lld,current system timestamp = %lld\n",
					globle->teleprompter->ts, now);
				if (sub == 7) {
					tp_scroll_pending = 1;
				}
				((struct ble_req_hdr *)tx)->len = hdr->len;
				ctx->tx_len = hdr->len + 4;
				PUT_RSP(9, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 10);
				} else {
					POST_TO_HOST(10);
				}
			} else if (idx == 1) {
				G1_LOG_INF(
					"ACTION ID = 3,Received multiple initialization packets, "
					"currently the first packet of data\n");
				multi_packet_total = total;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				memset(&tp_buf, 0, sizeof(tp_buf));
				memcpy(&tp_buf, &data[0xc], data_len - 0xc);
				multi_packet_len = data_len - 0xc;
				multi_packet_cnt++;
				globle->teleprompter->ts = 0;
				PUT_RSP_NC(9, 0);
				POST_TO_HOST(10);
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN(
						"ACTION ID = 3,There is a packet order error, current "
						"packet order = %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = 0;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(&tp_buf, 0, sizeof(tp_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				memcpy((uint8_t *)&tp_buf + multi_packet_len, &data[0xc],
				       data_len - 0xc);
				multi_packet_len += data_len - 0xc;
				G1_LOG_INF("ACTION ID = 3,Successfully received the subpackage "
					   "content, the packet sequence is %d\n",
					   idx);
				multi_packet_cnt++;
				PUT_RSP_NC(9, 0);
				POST_TO_HOST(10);
			} else if (idx == total) {
				int64_t now;

				if (multi_packet_cnt != idx - 1 || multi_packet_total != idx) {
					G1_LOG_WRN(
						"ACTION ID = 3,There is a packet order error, current "
						"packet order = %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(&tp_buf, 0, sizeof(tp_buf));
					PUT_RSP_NC(9, 1);
					POST_TO_HOST(10);
					return;
				}
				globle->esb_down_id = data[3];
				set_esb_recv_state(data[3], 0, 0);
				memcpy((uint8_t *)&tp_buf + multi_packet_len, &data[0xc],
				       data_len - 0x14);
				multi_packet_len += data_len - 0x14;
				multi_packet_cnt++;
				memset(globle->teleprompter->text, 0,
				       sizeof(globle->teleprompter->text));
				memcpy(globle->teleprompter->text, &tp_buf, multi_packet_len);
				globle->teleprompter->len = multi_packet_len;
				globle->teleprompter->action = 3;
				globle->teleprompter->scroll_pct = data[9];
				globle->teleprompter->mark_pos = *(uint16_t *)&data[10];
				G1_LOG_INF(
					"ACTIION ID = 3,Successfully assembled multiple packages "
					"of data, and updated the complete content to the UI "
					"thread for data update.\n");
				globle->teleprompter->ts = GET_LE64(&data[data_len - 8]);
				now = 0;
				get_wall_clock_ms(&now);
				G1_LOG_INF("*******************------ received app send timestamp "
					   "= %lld,current system timestamp = %lld\n",
					   globle->teleprompter->ts, now);
				if (sub == 7) {
					tp_scroll_pending = 1;
				}
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				PUT_RSP_NC(9, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 10);
				} else {
					POST_TO_HOST(10);
				}
			} else {
				G1_LOG_ERR(
					"ACTION ID = 3,A serious packet order error occurred!\n");
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				memset(&tp_buf, 0, sizeof(tp_buf));
				PUT_RSP_NC(9, 1);
				POST_TO_HOST(10);
			}
			break;
		case 5:
			G1_LOG_INF("received teleprompter func exit packet ....\n");
			if (is_master() == 1) {
				globle->esb_down_id = data[3];
				set_esb_recv_state(data[3], 0, 0);
				memset(msg, 0, sizeof(struct teleprompter_info));
				ctx->tx_len = ((struct ble_req_hdr *)tx)->len + 4;
				ble_process_req_local(ctx, rx, 8);
			}
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 6);
			POST_TO_HOST(6);
			break;
		default:
			break;
		}
		break;
	}
	case BLE_CMD_TELEPROMPTER_SUSPEND: {
		data_len = *(uint16_t *)&data[1];
		if (data_len != hdr->len) {
			G1_LOG_ERR("teleprompter packet length error,input data length = %d,packet "
				   "data length = %d\n",
				   hdr->len, data_len);
			return;
		}
		if (data[4] != 6) {
			return;
		}
		G1_LOG_INF("received teleprompter suspend packet ....\n");
		globle->esb_down_id = data[3];
		set_esb_recv_state(data[3], 0, 0);
		if (data[5] == 1) {
			globle->teleprompter->suspend = 1;
		} else {
			globle->teleprompter->suspend = 0;
		}
		globle->teleprompter->ts = GET_LE64(&data[data_len - 0xc]);
		globle->teleprompter->counter = GET_LE32(&data[data_len - 4]);
		G1_LOG_INF("SUSPEND status ,app send counter time = %d\n",
			   globle->teleprompter->counter);
		ctx->tx_len = ((struct ble_req_hdr *)tx)->len + 4;
		memset(resp, 0, sizeof(resp));
		memcpy(resp, data, 6);
		if (is_master() == 1) {
			send_response_data_to_msgqueue(resp, 6);
		} else {
			POST_TO_HOST(6);
		}
		break;
	}
	case BLE_CMD_TELEPROMPTER_POSITION_SET:
		if (*(uint16_t *)&data[1] != hdr->len) {
			G1_LOG_ERR("teleprompter packet length error,input data length = %d,packet "
				   "data length = %d\n",
				   hdr->len, *(uint16_t *)&data[1]);
			break;
		}
		if (data[4] == 4) {
			G1_LOG_INF("received app send sync packet ....\n");
			tp_app_sync_count++;
			ctx->tx_len = ((struct ble_req_hdr *)tx)->len + 4;
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 6);
			POST_TO_HOST(6);
		}
		break;
	case BLE_CMD_QUICK_NOTE_CONTROL: { /* quick note / audio */
		struct put_pkt_hdr *ph = (struct put_pkt_hdr *)data;

		data_len = ph->len;
		if (data_len != hdr->len) {
			G1_LOG_WRN(
				"packet length error,input data length = %d,packet data length = "
				"%d\n",
				hdr->len, data_len);
			break;
		}
		switch (data[4]) {
		case 1:
			if (is_master() == 1 && data_len == 6 && data[5] == 1) {
				requestAudioInfoToApp(0);
			}
			break;
		case 2:
			if (is_master() == 1) {
				if (data_len == 6) {
					G1_LOG_INF("received request voice data commmand\n");
					sendAudioStreamFileToApp(0, data[5]);
				} else if (data_len == 0xb) {
					if (data[10] == 0) {
						G1_LOG_INF(
							"received request voice data next data\n");
					} else if (data[10] == 0xaa) {
						G1_LOG_INF(
							"APP success received audio data ,delete voice "
							"block num %d\n",
							data[9]);
						if (is_master() == 1) {
							if ((uint8_t)(data[9] - 1) < 4) {
								delAudioStreamRecord(data[9]);
							} else {
								G1_LOG_WRN(
									"APP delete voice block num "
									"error %d\n",
									data[9]);
							}
						}
					} else {
						G1_LOG_ERR(
							"app return error response information\n");
					}
				}
			}
			break;
		case 3:
			total = *(uint16_t *)&data[5];
			idx = *(uint16_t *)&data[7];
			if (total == 1) {
				int num = data[9] & 0xf;
				uint8_t flag = data[10];
				int n = num - 1;

				memset(QN_ITEM(n).title, 0, sizeof(QN_ITEM(n).title));
				memset(QN_ITEM(n).text, 0, sizeof(QN_ITEM(n).text));
				if (flag == 0) {
					QN_ITEM(n).valid = 0;
					G1_LOG_INF(
						"received empty quick note data ,delete quick note "
						"num.%d\n",
						num);
					PUT_RSP_NC(9, 0);
					ctx->write(resp, 10);
					deleteQuickNoteData(n);
				} else {
					uint8_t tl = data[0xb];
					uint16_t xl;

					memcpy(QN_ITEM(n).title, &data[0xc],
					       MIN(tl, sizeof(QN_ITEM(n).title)));
					xl = data[tl + 0xc] | (data[tl + 0xd] << 8);
					memcpy(QN_ITEM(n).text, &data[tl + 0xe],
					       MIN(xl, sizeof(QN_ITEM(n).text)));
					G1_LOG_INF("title length = %d,text length = %d\n", tl, xl);
					QN_ITEM(n).valid = 1;
					G1_LOG_INF("Quick Note Title = %s,TXET = %s\n",
						   QN_ITEM(n).title, QN_ITEM(n).text);
					upgradeQuickNoteDataToFlash(n);
					PUT_RSP_NC(9, 0);
					ctx->write(resp, 10);
				}
			} else if (idx == 1) {
				memset(qn_text_buf, 0, sizeof(qn_text_buf));
				memset(&qn_title_buf, 0, sizeof(qn_title_buf));
				qn_len = 0;
				qn_cnt = 0;
				qn_total = total;
				qn_num = data[9];
				QN_ITEM(qn_num - 1).valid = 0;
				if (qn_num > 4 || qn_num == 0) {
					G1_LOG_ERR("app send parameter error,qucik note num = %d\n",
						   qn_num);
					PUT_RSP_NC(9, 1);
					ctx->write(resp, 10);
				} else {
					uint8_t tl = data[0xb];
					uint16_t xl;

					memcpy(qn_title_buf.str, &data[0xc],
					       MIN(tl, sizeof(qn_title_buf)));
					xl = data_len - 0xe - tl;
					memcpy(&qn_text_buf[qn_len], &data[tl + 0xe],
					       MIN(xl, sizeof(qn_text_buf)));
					qn_len = xl;
					qn_cnt++;
					PUT_RSP_NC(9, 0);
					ctx->write(resp, 10);
				}
			} else if (idx < total) {
				if (qn_cnt != idx - 1) {
					G1_LOG_WRN(
						"ACTION ID = 3,There is a packet order error, current "
						"packet order = %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					qn_total = 0;
					qn_cnt = 0;
					qn_len = 0;
					memset(qn_text_buf, 0, sizeof(qn_text_buf));
					memset(&qn_title_buf, 0, sizeof(qn_title_buf));
					PUT_RSP_NC(9, 1);
					ctx->write(resp, 10);
					return;
				}
				memcpy(&qn_text_buf[qn_len], &data[9], data_len - 9);
				qn_len += data_len - 9;
				qn_cnt++;
				PUT_RSP_NC(9, 0);
				ctx->write(resp, 10);
			} else if (idx == total) {
				if (qn_cnt != total - 1 || qn_total != total) {
					G1_LOG_WRN(
						"ACTION ID = 3,There is a packet order error, current "
						"packet order = %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					memset(qn_text_buf, 0, sizeof(qn_text_buf));
					memset(&qn_title_buf, 0, sizeof(qn_title_buf));
					qn_len = 0;
					qn_cnt = 0;
					qn_total = 0;
					PUT_RSP_NC(9, 1);
					ctx->write(resp, 10);
					return;
				}
				memcpy(&qn_text_buf[qn_len], &data[9], data_len - 9);
				qn_len += data_len - 9;
				memset(&QN_ITEM(qn_num - 1).title, 0, sizeof(QN_ITEM(0).title));
				memcpy(QN_ITEM(qn_num - 1).title, qn_title_buf.str,
				       sizeof(qn_title_buf));
				memset(QN_ITEM(qn_num - 1).text, 0, sizeof(QN_ITEM(0).text));
				memcpy(QN_ITEM(qn_num - 1).text, qn_text_buf, sizeof(qn_text_buf));
				QN_ITEM(qn_num - 1).valid = 1;
				G1_LOG_INF("Quick Note Title =%s,QuickNote TXET =%s\n",
					   QN_ITEM(qn_num - 1).title, QN_ITEM(qn_num - 1).text);
				upgradeQuickNoteDataToFlash(qn_num - 1);
				PUT_RSP_NC(9, 0);
				ctx->write(resp, 10);
				qn_len = 0;
				qn_cnt = 0;
				qn_total = 0;
			}
			break;
		case 4:
			if (data[5] < 5) {
				G1_LOG_INF("delete audio record num = %d\n", data[5] - 1);
				if (is_master() == 1) {
					delAudioStreamRecord(data[5]);
				}
				memcpy(resp, data, 5);
				resp[5] = 0;
				ctx->write(resp, 6);
			} else {
				G1_LOG_INF("received delete all quick note audio file command .\n");
				if (is_master() == 1) {
					delAudioStreamRecord(0xff);
				}
				memcpy(resp, data, 5);
				resp[5] = 0;
				ctx->write(resp, 6);
			}
			break;
		case 5:
			G1_LOG_INF("received clean audio voice command\n");
			if (is_master() == 1) {
				delAudioStreamRecord(0xff);
				memcpy(resp, data, 5);
				resp[5] = 0;
				ctx->write(resp, 6);
			}
			break;
		case 7:
			memcpy(resp, data, 5);
			resp[5] = 0;
			quicknote_timeout_reset();
			if (data[5] == 1) {
				globle->quicknote_show_info->enable = 1;
				globle->quicknote_show_info->started_by_app = 1;
				globle->quicknote_show_info->action = 7;
				memset(globle->quicknote_show_info->data, 0,
				       sizeof(globle->quicknote_show_info->data));
				update_persist_task_status(globle, 0xc, 2);
				set_esb_recv_state(data[3], 0, 0);
			} else {
				resp[5] = 1;
			}
			ctx->write(resp, 6);
			break;
		case 8: {
			struct put_pkt_hdr *rh = (struct put_pkt_hdr *)resp;

			quicknote_timeout_reset();
			memcpy(resp, data, 9);
			resp[1] = 10;
			resp[2] = 0;
			resp[9] = 0;
			idx = *(uint16_t *)&data[7];
			total = *(uint16_t *)&data[5];
			if (total == 1) {
				G1_LOG_INF("received quick note packet,total packet = 1\n");
				data_len -= 0xb;
				if (data_len > 0x14a) {
					G1_LOG_ERR(
						"quick note data is too long, the current data length "
						"0,wait write data length = %d\n",
						data_len);
					resp[9] = 1;
					ctx->write(resp, rh->len);
					return;
				}
				globle->quicknote_show_info->action = 8;
				get_dashboard_state()->quicknote_show_info->result_code = data[9];
				get_dashboard_state()->quicknote_show_info->scroll_line = data[10];
				if (get_dashboard_state()->quicknote_show_info->result_code == 0) {
					memset(text_packet_buf, 0, sizeof(text_packet_buf));
					memset(get_dashboard_state()->quicknote_show_info->data, 0,
					       sizeof(get_dashboard_state()
							      ->quicknote_show_info->data));
					get_dashboard_state()->quicknote_show_info->len = data_len;
					memcpy(get_dashboard_state()->quicknote_show_info->data,
					       &data[0xb],
					       get_dashboard_state()->quicknote_show_info->len);
				} else {
					display_DelayClose(10000);
				}
				set_esb_recv_state(data[3], 0, 0);
			} else if (idx == 1) {
				G1_LOG_INF(
					"Received quick note multiple packets, currently the first "
					"packet of data\n");
				multi_packet_total = total;
				multi_packet_cnt = 1;
				memset(text_packet_buf, 0, sizeof(text_packet_buf));
				memcpy(text_packet_buf, &data[0xb], data_len - 0xb);
				multi_packet_len = data_len - 0xb;
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order = %d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(text_packet_buf, 0, sizeof(text_packet_buf));
					resp[9] = 1;
					ctx->write(resp, rh->len);
					return;
				}
				if (multi_packet_len - 0xb + data_len > 0x14a) {
					G1_LOG_ERR(
						"quick note data is too long, the current data length "
						"%d,wait write data length = %d\n",
						multi_packet_len, data_len - 0xb);
					resp[9] = 1;
					ctx->write(resp, rh->len);
					return;
				}
				memcpy(&text_packet_buf[multi_packet_len], &data[0xb],
				       data_len - 0xb);
				multi_packet_len += data_len - 0xb;
				G1_LOG_INF(
					"Successfully received the subpackage content, the packet "
					"sequence is %d\n",
					idx);
				multi_packet_cnt++;
			} else if (idx == total) {
				if (multi_packet_cnt != idx - 1 || idx != multi_packet_total) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order = %d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					memset(text_packet_buf, 0, sizeof(text_packet_buf));
					resp[9] = 1;
					ctx->write(resp, rh->len);
					return;
				}
				if (multi_packet_len - 0xb + data_len > 0x14a) {
					G1_LOG_ERR(
						"quick note data is too long, the current data length "
						"%d,wait write data length = %d\n",
						multi_packet_len, data_len - 0xb);
					resp[9] = 1;
					ctx->write(resp, rh->len);
					return;
				}
				G1_LOG_INF("Successfully assembled multiple packages of data, and "
					   "updated the complete content to the UI thread for data "
					   "update.\n");
				globle->quicknote_show_info->action = 8;
				get_dashboard_state()->quicknote_show_info->result_code = data[9];
				get_dashboard_state()->quicknote_show_info->scroll_line = data[10];
				if (get_dashboard_state()->quicknote_show_info->result_code == 0) {
					memcpy(&text_packet_buf[multi_packet_len], &data[0xb],
					       data_len - 0xb);
					multi_packet_len += data_len - 0xb;
					memset(get_dashboard_state()->quicknote_show_info->data, 0,
					       sizeof(get_dashboard_state()
							      ->quicknote_show_info->data));
					memcpy(get_dashboard_state()->quicknote_show_info->data,
					       text_packet_buf, multi_packet_len);
					get_dashboard_state()->quicknote_show_info->len =
						multi_packet_len;
				} else {
					display_DelayClose(10000);
				}
				set_esb_recv_state(data[3], 0, 0);
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
			}
			ctx->write(resp, rh->len);
			break;
		}
		case 9:
			quicknote_timeout_reset();
			memcpy(resp, data, 5);
			resp[5] = globle->quicknote_show_info->enable != 1;
			resp[6] = data[5];
			ctx->write(resp, 7);
			break;
		case 10:
			memcpy(resp, data, 5);
			resp[5] = 0;
			if (data[5] <= 2) {
				quicknote_timeout_reset();
				if (globle->dashboard_mode != 12) {
					globle->quicknote_show_info->enable = 1;
					globle->quicknote_show_info->started_by_app = 1;
					memset(get_dashboard_state()->quicknote_show_info->data, 0,
					       sizeof(get_dashboard_state()
							      ->quicknote_show_info->data));
					update_persist_task_status(globle, 0xc, 2);
				}
				globle->quicknote_show_info->action = 10;
				globle->quicknote_show_info->result_code = data[5];
				set_esb_recv_state(data[3], 0, 0);
			} else {
				resp[5] = 1;
			}
			ctx->write(resp, 6);
			break;
		default:
			break;
		}
		break;
	}
	case BLE_CMD_NAVIGATION_INFO: { /* navigation */
		struct put_pkt_hdr *ph = (struct put_pkt_hdr *)data;
		struct nav_info *nav;

		data_len = ph->len;
		if (data_len != hdr->len) {
			G1_LOG_WRN(
				"packet length error,input data length = %d,packet data length = "
				"%d\n",
				hdr->len, data_len);
			return;
		}
		switch (data[4]) {
		case 0:
			G1_LOG_INF("received navigation func startup  packet ....\n");
			nav = get_dashboard_state()->nav;
			globle->esb_down_id = data[3];
			memset(nav, 0, sizeof(*nav));
			memset(get_dashboard_state()->nav, 0, sizeof(*nav));
			nav->type = 0;
			nav->running = 1;
			nav->sync_id = data[3];
			nav_overview_map_ready = 0;
			nav_panoramic_map_ready = 0;
			update_persist_task_status(get_dashboard_state(), 10, 2);
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 6);
			resp[5] = 0;
			if (is_master() == 1) {
				send_response_data_to_msgqueue(resp, 6);
			} else {
				POST_TO_HOST(6);
			}
			break;
		case 1: {
			uint32_t len;
			uint32_t pos, end;

			nav = get_dashboard_state()->nav;
			get_dashboard_state()->esb_down_id = data[3];
			nav->direction = data[5];
			memcpy(&nav->x, &data[6], sizeof(nav->x));
			memcpy(&nav->y, &data[8], sizeof(nav->y));
			if (nav->x != nav_last_x || nav->y != nav_last_y) {
				nav_last_x = nav->x;
				nav_last_y = nav->y;
				nav_pos_updated = 1;
			}
			if (nav->x > 0x1e8 || nav->y > 0x88) {
				G1_LOG_ERR("app send x/y parameter overstep!!! x= %d,y= %d\n",
					   nav->x, nav->y);
			}
			/*
			 * Five NUL-terminated strings follow each other from data[10];
			 * pos is the index of the terminator of the previous one.
			 */
			end = data_len - 1;
			/* time remaining */
			len = nav_field_len(&data[10], data_len - 10);
			if (len == 0 || len > sizeof(nav->time_remaining)) {
				G1_LOG_ERR("TIME_REMAINNING_STRING_SIZE oversize\n");
				PUT_RSP(5, 1);
				ctx->write(resp, 6);
				return;
			}
			memset(nav->time_remaining, 0, sizeof(nav->time_remaining));
			memcpy(nav->time_remaining, &data[10], len);
			pos = 10 + len - 1;
			/* remaining kilometers */
			len = nav_field_len(&data[pos + 1], end - pos);
			if (len == 0 || len > sizeof(nav->km_remaining)) {
				G1_LOG_ERR("KILOMETER_REMAINNING_STRING_SIZE oversize\n");
				PUT_RSP(5, 1);
				ctx->write(resp, 6);
				return;
			}
			memset(nav->km_remaining, 0, sizeof(nav->km_remaining));
			memcpy(nav->km_remaining, &data[pos + 1], len);
			pos += len;
			/* road name */
			len = nav_field_len(&data[pos + 1], end - pos);
			if (len == 0 || len > sizeof(nav->road_name)) {
				G1_LOG_ERR("ROAD_NAME_STRING_SIZE oversize\n");
				PUT_RSP(5, 1);
				ctx->write(resp, 6);
				return;
			}
			memset(nav->road_name, 0, sizeof(nav->road_name));
			memcpy(nav->road_name, &data[pos + 1], len);
			pos += len;
			/* remaining distance */
			len = nav_field_len(&data[pos + 1], end - pos);
			if (len == 0 || len > sizeof(nav->remaining_distance)) {
				G1_LOG_ERR("REMAINING_DISTANCE_STRING_SIZE oversize\n");
				PUT_RSP(5, 1);
				ctx->write(resp, 6);
				return;
			}
			memset(nav->remaining_distance, 0, sizeof(nav->remaining_distance));
			memcpy(nav->remaining_distance, &data[pos + 1], len);
			pos += len;
			/* current speed */
			len = nav_field_len(&data[pos + 1], end - pos);
			if (len == 0 || len > sizeof(nav->speed)) {
				G1_LOG_ERR("CURRENT_SPEED_STRING_SIZE oversize\n");
				PUT_RSP(5, 1);
				ctx->write(resp, 6);
				return;
			}
			memset(nav->speed, 0, sizeof(nav->speed));
			memcpy(nav->speed, &data[pos + 1], len);
			G1_LOG_INF(
				"direction = %d,x = %d,y = %d,time_remaining = "
				"%s,remainning_kilometers = %s,road_name_info = %s,                "
				"            remaining_distance_info= %s,current speed = %s\n",
				nav->direction, nav->x, nav->y, nav->time_remaining,
				nav->km_remaining, nav->road_name, nav->remaining_distance,
				nav->speed);
			nav->updated = 1;
			nav->type = 1;
			nav->sync_id = data[3];
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 5);
			nav_text_updated = 1;
			if (is_master() == 1) {
				send_response_data_to_msgqueue(resp, 6);
			} else {
				POST_TO_HOST(6);
			}
			break;
		}
		case 2: /* overview map */
			idx = *(uint16_t *)&data[7];
			total = *(uint16_t *)&data[5];
			if (idx == 1) {
				G1_LOG_INF(
					"Received multiple initialization packets, currently the "
					"first packet of data,total packet = %d\n",
					total);
				multi_packet_total = total;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				nav = get_dashboard_state()->nav;
				nav_overview_map_ready = 0;
				lock_nav_map_mutex(K_FOREVER);
				memset(nav_overview_map_buf, 0, sizeof(nav_overview_map_buf));
				memcpy(nav_overview_map_buf, &data[9], data_len - 9);
				unlock_nav_map_mutex();
				multi_packet_len = data_len - 9;
				multi_packet_cnt++;
				if (total == 1) {
					nav_overview_map_updated = 1;
					nav_overview_map_ready = 1;
					nav->updated = 1;
					nav->type = 2;
					nav->sync_id = data[3];
					nav->map_size = multi_packet_len;
					G1_LOG_INF(
						"Successfully received navigation data .Maps are "
						"compressed to only one pack!!!\n");
					multi_packet_total = 0;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
				}
				PUT_RSP_NC(9, 0);
				ctx->write(resp, 10);
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					lock_nav_map_mutex(K_FOREVER);
					memset(nav_overview_map_buf, 0,
					       sizeof(nav_overview_map_buf));
					unlock_nav_map_mutex();
					get_dashboard_state();
					nav_overview_map_ready = 0;
					PUT_RSP_NC(9, 1);
					ctx->write(resp, 10);
					return;
				}
				if (multi_packet_len - 9 + data_len >=
				    sizeof(nav_overview_map_buf)) {
					G1_LOG_ERR("overview map : The data length of the current "
						   "packet is too long, the current data length "
						   "%d,wait write data length = %d\n",
						   multi_packet_len,
						   multi_packet_len - 9 + data_len);
					return;
				}
				get_dashboard_state();
				lock_nav_map_mutex(K_FOREVER);
				memcpy(&nav_overview_map_buf[multi_packet_len], &data[9],
				       data_len - 9);
				unlock_nav_map_mutex();
				nav_overview_map_ready = 0;
				multi_packet_len += data_len - 9;
				G1_LOG_INF("Successfully received the subpackage content, "
					   "the packet sequence is %d,total size = %d\n",
					   idx, multi_packet_len);
				multi_packet_cnt++;
			} else if (idx == total) {
				if (multi_packet_cnt != idx - 1 || idx != multi_packet_total) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					lock_nav_map_mutex(K_FOREVER);
					memset(nav_overview_map_buf, 0,
					       sizeof(nav_overview_map_buf));
					unlock_nav_map_mutex();
					get_dashboard_state();
					nav_overview_map_ready = 0;
					PUT_RSP_NC(9, 1);
					ctx->write(resp, 10);
					return;
				}
				if (multi_packet_len - 9 + data_len >
				    sizeof(nav_overview_map_buf)) {
					G1_LOG_ERR("overview map : The data length of the current "
						   "packet is too long, the current data length "
						   "%d,wait write data length = %d\n",
						   multi_packet_len,
						   multi_packet_len - 9 + data_len);
					return;
				}
				globle->esb_down_id = data[3];
				nav = get_dashboard_state()->nav;
				lock_nav_map_mutex(K_FOREVER);
				memcpy(&nav_overview_map_buf[multi_packet_len], &data[9],
				       data_len - 9);
				unlock_nav_map_mutex();
				multi_packet_len += data_len - 9;
				G1_LOG_INF("Successfully received the last packet,packet "
					   "length = %d,total packet size = %d\n",
					   idx, multi_packet_len);
				nav->updated = 1;
				nav_overview_map_ready = 1;
				nav_overview_map_updated = 1;
				nav->type = 2;
				nav->sync_id = data[3];
				nav->map_size = multi_packet_len;
				multi_packet_cnt++;
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				PUT_RSP_NC(9, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 10);
				} else {
					POST_TO_HOST(10);
				}
			} else {
				G1_LOG_WRN(
					"There is a packet order error, current packet order = %d, "
					"expected packet order =%d\n",
					idx, multi_packet_cnt + 1);
				multi_packet_total = total;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
				lock_nav_map_mutex(K_FOREVER);
				memset(nav_overview_map_buf, 0, sizeof(nav_overview_map_buf));
				unlock_nav_map_mutex();
				PUT_RSP_NC(9, 1);
				ctx->write(resp, 10);
			}
			break;
		case 3: /* panoramic map */
			G1_LOG_INF("received navigation panoramic map data packet ....\n");
			idx = *(uint16_t *)&data[7];
			total = *(uint16_t *)&data[5];
			if (idx == 1) {
				pano_total = total;
				pano_cnt = 0;
				pano_len = 0;
				nav = get_dashboard_state()->nav;
				nav_panoramic_map_ready = 0;
				citywalk_canvas_valid = 0;
				nav->panoramic = data[9];
				memset(panoramic_map_buf, 0, PANORAMIC_MAP_SIZE);
				memcpy(panoramic_map_buf, &data[10], data_len - 10);
				pano_len = data_len - 10;
				G1_LOG_INF(
					"Successfully received multiple initialization packets, "
					"currently the first packet of data,received data size = "
					"%d,total packet len = %d\n",
					pano_len, pano_total);
				pano_cnt++;
				PUT_RSP_NC(10, 0);
				ctx->write(resp, 11);
			} else if (idx < total) {
				get_dashboard_state();
				if (pano_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, pano_cnt + 1);
					pano_total = total;
					pano_cnt = 0;
					pano_len = 0;
					memset(panoramic_map_buf, 0, PANORAMIC_MAP_SIZE);
					nav_panoramic_map_ready = 0;
					PUT_RSP_NC(10, 1);
					ctx->write(resp, 11);
					return;
				}
				if (pano_len - 10 + data_len >= PANORAMIC_MAP_SIZE) {
					G1_LOG_ERR("Panoramic map : The data length of the current "
						   "packet is too long, the current data length "
						   "%d,wait write data length = %d\n",
						   pano_len, pano_len - 10 + data_len);
					return;
				}
				nav_panoramic_map_ready = 0;
				memcpy(&panoramic_map_buf[pano_len], &data[10], data_len - 10);
				pano_len += data_len - 10;
				G1_LOG_INF("Successfully received the subpackage content, "
					   "the packet sequence is %d,total received size "
					   "= %d\n",
					   idx, pano_len);
				pano_cnt++;
			} else if (idx == total) {
				nav = get_dashboard_state()->nav;
				if (pano_cnt != idx - 1 || idx != pano_total) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order "
						"= %d, expected packet order =%d\n",
						idx, pano_cnt + 1);
					pano_total = total;
					pano_cnt = 0;
					pano_len = 0;
					memset(panoramic_map_buf, 0, PANORAMIC_MAP_SIZE);
					nav_panoramic_map_ready = 0;
					PUT_RSP_NC(10, 1);
					ctx->write(resp, 11);
					return;
				}
				if (pano_len - 10 + data_len > PANORAMIC_MAP_SIZE) {
					G1_LOG_ERR("Panoramic map : The data length of the current "
						   "packet is too long, the current data length "
						   "%d,wait write data length = %d\n",
						   pano_len, pano_len - 10 + data_len);
					return;
				}
				globle->esb_down_id = data[3];
				memcpy(&panoramic_map_buf[pano_len], &data[10], data_len - 10);
				pano_len += data_len - 10;
				G1_LOG_INF("Successfully received panoramic map  last "
					   "packet,Packet id = %d,total received size = "
					   "%d\n",
					   idx, pano_len);
				pano_cnt++;
				nav_panoramic_map_ready = 1;
				nav_panoramic_map_updated = 1;
				nav->updated = 1;
				nav->type = 3;
				nav->sync_id = data[3];
				pano_total = 0;
				pano_cnt = 0;
				pano_len = 0;
				PUT_RSP_NC(10, 0);
				if (is_master() == 1) {
					send_response_data_to_msgqueue(resp, 11);
				} else {
					POST_TO_HOST(11);
				}
			} else {
				get_dashboard_state();
				G1_LOG_WRN(
					"There is a packet order error, current packet order = %d, "
					"expected packet order =%d\n",
					idx, pano_cnt + 1);
				pano_total = total;
				pano_cnt = 0;
				pano_len = 0;
				memset(panoramic_map_buf, 0, PANORAMIC_MAP_SIZE);
				nav_panoramic_map_ready = 0;
				PUT_RSP_NC(10, 1);
				ctx->write(resp, 11);
			}
			break;
		case 4:
			G1_LOG_INF("received app send sync packet ....\n");
			nav = get_dashboard_state()->nav;
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 6);
			if (nav->running == 0) {
				resp[5] = 0;
				G1_LOG_WRN("navigation don't startup, sync packet return\n");
			} else {
				nav_app_sync_count++;
			}
			ctx->write(resp, 6);
			break;
		case 5:
			G1_LOG_INF("received navigation func exit packet ....\n");
			nav = get_dashboard_state()->nav;
			globle->esb_down_id = data[3];
			memset((uint8_t *)nav + 1, 0, sizeof(*nav) - 1);
			nav->running = 0;
			PUT_RSP(5, 1);
			ctx->write(resp, 6);
			break;
		case 6: {
			uint8_t status;

			nav = get_dashboard_state()->nav;
			globle->esb_down_id = data[3];
			status = data[5];
			nav->updated = 1;
			nav->arrived = status;
			nav->type = 6;
			nav->sync_id = data[3];
			memset(resp, 0, sizeof(resp));
			memcpy(resp, data, 6);
			memset(nav->prompt, 0, sizeof(nav->prompt));
			if (data_len - 6 >= sizeof(nav->prompt)) {
				G1_LOG_ERR("app send prompt word oversize,drop it...\n");
				resp[5] = 1;
			} else {
				memcpy(nav->prompt, &data[6], data_len - 6);
				G1_LOG_INF("received arrived status packet. arrived status = %d "
					   ",arrived prompt word = %s\n",
					   status, nav->prompt);
				resp[5] = 0;
			}
			if (is_master() == 1) {
				send_response_data_to_msgqueue(resp, 6);
			} else {
				POST_TO_HOST(6);
			}
			break;
		}
		default:
			break;
		}
		break;
	}
	case BLE_CMD_TRANSLATE_SRC_TEXT: /* translate: source text */
		if (hdr->len != 0) {
			memset(text_packet_buf, 0, sizeof(text_packet_buf));
			if (data[3] == data[2] - 1) {
				resp[1] = PKCS7_PACK_DONE;
			} else {
				save_esb_snapshot(globle->translate_src,
						  sizeof(struct translate_buf), globle->esb_down_id,
						  0);
				set_esb_recv_state(data[1], 1, 0);
				resp[1] = PKCS7_PACK_MORE;
			}
			memcpy(text_packet_buf, &data[7], hdr->len - 7);
		}
		if (data[3] == 0) {
			memset(globle->translate_src->data, 0, sizeof(globle->translate_src->data));
			globle->translate_src->len = 0;
			globle->translate_src->screen_status = data[4];
			globle->translate_src->char_pos = data[6] + data[5] * 256;
		}
		memcpy((uint8_t *)globle->translate_src + globle->translate_src->len + 6,
		       text_packet_buf, hdr->len - 7);
		globle->translate_src->len += hdr->len - 7;
		((struct ble_req_hdr *)tx)->len = hdr->len;
		ctx->tx_len = hdr->len + 4;
		if (data[3] == data[2] - 1) {
			ble_process_req_local(ctx, rx, 8);
		}
		resp[2] = data[1];
		resp[3] = data[2];
		resp[4] = data[3];
		if (is_master() == 1) {
			if (data[3] == data[2] - 1) {
				deferred_text_ack_cmd = BLE_CMD_TRANSLATE_SRC_TEXT;
				deferred_text_ack_total = data[2];
				deferred_text_ack_idx = data[3];
				set_esb_recv_state(data[1], 0, 0);
			} else {
				ctx->write(resp, 20);
			}
		} else {
			if (data[3] == data[2] - 1) {
				set_esb_recv_state(data[1], 0, 0);
			}
			ctx->write(resp, 20);
		}
		break;
	case BLE_CMD_TRANSLATE_DST_TEXT: /* translate: translated text */
		if (hdr->len != 0) {
			memset(text_packet_buf, 0, sizeof(text_packet_buf));
			if (data[3] == data[2] - 1) {
				resp[1] = PKCS7_PACK_DONE;
			} else {
				save_esb_snapshot(globle->translate_dst,
						  sizeof(struct translate_buf), globle->esb_up_id,
						  1);
				set_esb_up_recv_state(data[1], 1);
				resp[1] = PKCS7_PACK_MORE;
			}
			memcpy(text_packet_buf, &data[7], hdr->len - 7);
		}
		if (data[3] == 0) {
			memset(globle->translate_dst->data, 0, sizeof(globle->translate_dst->data));
			globle->translate_dst->len = 0;
			globle->translate_dst->screen_status = data[4];
			globle->translate_dst->char_pos = data[6] + data[5] * 256;
		}
		memcpy((uint8_t *)globle->translate_dst + globle->translate_dst->len + 6,
		       text_packet_buf, hdr->len - 7);
		globle->translate_dst->len += hdr->len - 7;
		((struct ble_req_hdr *)tx)->len = hdr->len;
		ctx->tx_len = hdr->len + 4;
		if (data[3] == data[2] - 1) {
			ble_process_req_local(ctx, rx, 8);
		}
		resp[2] = data[1];
		resp[3] = data[2];
		resp[4] = data[3];
		if (is_master() == 1) {
			if (data[3] == data[2] - 1) {
				deferred_text_ack_cmd = BLE_CMD_TRANSLATE_DST_TEXT;
				deferred_text_ack_total = data[2];
				deferred_text_ack_idx = data[3];
				set_esb_up_recv_state(data[1], 0);
			} else {
				ctx->write(resp, 20);
			}
		} else {
			if (data[3] == data[2] - 1) {
				set_esb_up_recv_state(data[1], 0);
			}
			ctx->write(resp, 20);
		}
		break;
	case BLE_CMD_MICROPHONE_SET: /* microphone */
		if (data[1] == 1) {
			if (is_idle() || get_dashboard_state()->task_status->task == 9 ||
			    get_dashboard_state()->task_status->task == 0x10 ||
			    get_dashboard_state()->task_status->task == 0xb ||
			    get_dashboard_state()->task_status->task == 0x13 ||
			    get_dashboard_state()->task_status->task == 0x14 ||
			    get_dashboard_state()->task_status->task == 0xc ||
			    (get_dashboard_state()->dashboard_mode == 0 &&
			     get_dashboard_state()->suspend_state == 0)) {
				if (get_dashboard_state()->task_status->task == 0x10 &&
				    get_dashboard_state()->dash_buf->type == 2) {
					get_dashboard_state()->dash_buf->type = 3;
				}
				G1_LOG_INF("received open dmic command\n");
				request_dmic_stream_start();
				resp[1] = PKCS7_PACK_DONE;
			} else {
				resp[1] = PKCS7_PACK_ERR;
			}
		} else {
			G1_LOG_INF("received close dmic command\n");
			dmic_stream_stop();
			resp[1] = PKCS7_PACK_DONE;
		}
		resp[2] = data[1];
		ctx->write(resp, 20);
		break;
	case BLE_CMD_TRANSLATE_CONTROL:
		if (data[2] != 0 && data[3] != 0) {
			globle->translate_src->translation_type = data[3];
			globle->translate_src->original_type = data[2];
			globle->translate_dst->translation_type = data[3];
			globle->translate_dst->original_type = data[2];
		}
		globle->translate_src->suspend = data[1];
		globle->translate_dst->suspend = data[1];
		dmic_stream_pause(data[1]);
		G1_LOG_INF(
			"received start/Pause command,origin language type = %d,translate origin "
			"language type = %d,Suspend status = %d\n",
			globle->translate_dst->original_type,
			globle->translate_dst->translation_type, globle->translate_src->suspend);
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		break;
	case BLE_CMD_FILE_UPLOAD:
		BIT_MAP_CHUNK_ID++;
		if (data[1] != BIT_MAP_CHUNK_ID) {
			resp[1] = BIT_MAP_CHUNK_ID;
			resp[2] = PKCS7_PACK_ERR;
			ctx->write(resp, 20);
			BIT_MAP_CHUNK_ID--;
			G1_LOG_ERR("wrong package id %d\n", BIT_MAP_CHUNK_ID);
		} else {
			enqueue_file(&data[1], (uint8_t)(hdr->len - 1));
			globle->dashboard_cmd = 0x15;
			k_sem_give(&globle->dashboard_position_sem);
		}
		globle->last_sync_ts = k_uptime_get();
		break;
	case BLE_CMD_FILE_UPLOAD_COMPLETE:
		if (hdr->len < 0x15 && data[1] == 0xd && data[2] == 0xe) {
			enqueue_file(&data[1], (uint8_t)(hdr->len - 1));
			globle->dashboard_cmd = 0x20;
			k_sem_give(&globle->dashboard_position_sem);
			G1_LOG_ERR("recv upgrade font success\n");
			resp[1] = PKCS7_PACK_DONE;
			ctx->write(resp, 20);
		} else {
			resp[1] = PKCS7_PACK_ERR;
			ctx->write(resp, 20);
			G1_LOG_ERR("recv upgrade font failed\n");
		}
		BIT_MAP_CHUNK_ID = 0xff;
		break;
	case BLE_CMD_WEAR_DETECTION_SET:
		globle->check_mode = data[1];
		globle->dashboard_cmd = 0x27;
		k_sem_give(&globle->dashboard_position_sem);
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		break;
	case BLE_CMD_BITMAP_SHOW:
		rx[3] = data[1];
		rx[2] = data[2];
		rx[1] = data[3];
		rx[0] = data[4];
		resp[1] = data[1];
		resp[2] = data[2];
		resp[3] = data[3];
		resp[4] = data[4];
		if (*(uint32_t *)rx != LAST_FILE_CRC) {
			resp[5] = PKCS7_PACK_ERR;
		} else {
			resp[5] = PKCS7_PACK_DONE;
		}
		ctx->write(resp, 20);
		globle->dashboard_cmd = hdr->cmd;
		k_sem_give(&globle->dashboard_position_sem);
		break;
	case BLE_CMD_UPGRADE_CONTROL:
		setEraseDFU(true);
		globle->last_sync_ts = k_uptime_get();
		globle->dashboard_cmd = hdr->cmd;
		k_sem_give(&globle->dashboard_position_sem);
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		break;
	case BLE_CMD_BITMAP_HIDE:
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, 20);
		if (get_dashboard_state()->dashboard_mode == 0xb ||
		    get_dashboard_state()->dashboard_mode == 0x10) {
			exit_running_task_keep_status(globle, 0);
		} else {
			if (get_dashboard_state()->is_master == 2 &&
			    get_dashboard_state()->dashboard_mode == 6) {
				k_msleep(50);
			}
			exit_running_task(globle, 0);
		}
		break;
	case BLE_CMD_TUTORIAL_CONTROL: {
		int n;

		G1_LOG_INF("received onboarding init packet ....\n");
		n = onboarding_ble_process(globle, hdr, data, resp);
		if (n > 0) {
			memcpy(msg, data, hdr->len);
			ret = ctx->write(resp, (uint16_t)n);
			if (ret < 0) {
				G1_LOG_ERR("onboarding post_to_host failed ret = %d\n", ret);
			}
		}
		break;
	}
	case BLE_CMD_HARDWARE_SET:
		dlen = *(uint16_t *)&data[1];
		resp[1] = 6;
		resp[3] = data[3];
		resp[2] = 0;
		resp[4] = data[4];
		if (hdr->len == dlen && data[4] < 9) {
			uint8_t v;

			resp[5] = PKCS7_PACK_DONE;
			switch (data[4]) {
			case HW_SUB_USER_DATA_RESET:
				globle->user_data_reset_pending = 1;
				break;
			case HW_SUB_HEIGHT_DEPTH:
				if (data[6] > 8 || data[7] > 9) {
					resp[5] = PKCS7_PACK_ERR;
					break;
				}
				globle->esb_down_id = data[3];
				memcpy(msg, data, hdr->len);
				ble_process_req_local(ctx, rx, 8);
				break;
			case HW_SUB_GIVE_MIC_TRANSM_SEM:
				if (get_pt_test_mode() == 1) {
					G1_LOG_INF("give mic_transm_sem\n");
					k_sem_give(&mic_transm_sem);
				}
				break;
			case HW_SUB_LUM_GEAR:
				v = data[5];
				G1_LOG_WRN("ble set lum gear:%d\n", v);
				if (v > 0x3f) {
					resp[5] = PKCS7_PACK_ERR;
					break;
				}
				if (globle->panel.lum_gear != v) {
					globle->panel.lum_gear = v;
					globle->panel.brightness_max = v;
					globle->dashboard_cmd = hdr->cmd;
					sys_settings_dirty = 1;
					k_sem_give(&globle->dashboard_position_sem);
				}
				break;
			case HW_SUB_DOUBLE_TAP_ACTION:
				v = data[5];
				if (v > 5) {
					resp[5] = PKCS7_PACK_ERR;
					break;
				}
				if (globle->double_tap_customize != v) {
					SendDoubleTapCustomizeToSlave(v);
				}
				break;
			case HW_SUB_LUM_COEFFICIENT:
				v = data[5];
				switch (v) {
				case 0:
				case 1:
					globle->dashboard_ts->dashboard_hold = v;
					break;
				case 2:
					v = data[6];
					G1_LOG_WRN("ble set lum coeffic:%d\n", v);
					if (globle->dashboard_ts->dashboard_hold != 1 || v < 70 ||
					    v > 100) {
						resp[5] = PKCS7_PACK_ERR;
						break;
					}
					if (globle->panel.brightness_percent != v) {
						globle->panel.brightness_percent = v;
						set_brightness_to_panel_reg_in_running(
							globle->panel.panel_brightness_level);
						storage_commit_request(0, 0);
					}
					break;
				default:
					resp[5] = PKCS7_PACK_ERR;
					break;
				}
				break;
			case HW_SUB_LONG_PRESS_ENABLE:
				v = data[5];
				if (v > 1) {
					resp[5] = PKCS7_PACK_ERR;
					break;
				}
				if (globle->longpress_tap_customize != v) {
					SendLongpressTapCustomizeToSlave(v);
				}
				break;
			case HW_SUB_MIC_ON_HEAD_UP:
				if (data[5] == 1) {
					globle->head_up_mic_disabled = 1;
				} else {
					globle->head_up_mic_disabled = 0;
				}
				break;
			default:
				resp[5] = PKCS7_PACK_ERR;
				break;
			}
		} else {
			resp[5] = PKCS7_PACK_ERR;
		}
		ctx->write(resp, 20);
		break;
	default:
		G1_LOG_WRN("@tx error put req_type: %d\n", hdr->cmd);
		resp[1] = PKCS7_PACK_ERR;
		resp[2] = 0x15;
		strcpy((char *)&resp[2], "error put req.");
		ctx->write(resp, 20);
		break;
	}
}
