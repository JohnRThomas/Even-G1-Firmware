/*
 * BLE "POST" requests from the phone (commands 0x47..0x56): unpair, display
 * sync, text / transcription / Even AI content delivered in several packets,
 * notification delivery, dashboard lock and the other phone-initiated
 * actions.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/proto.h"
#include "comm/ble/proto_internal.h"
#include "services/notifications/notify.h"
#include "ui/screen_dispatch.h"
#include "services/tasks/tasks.h"
#include "drivers/mic/dmic.h"
#include "ui/screens/ui_transcribe_info.h"
#include "ui/screens/ui_even_ai_v2_info.h"

/* multi-packet reassembly buffer (text, transcribe, even AI) */
#define MULTI_PACKET_BUF_SIZE 0x204
static uint8_t multi_packet_buf[MULTI_PACKET_BUF_SIZE];

/* response of a post request: cmd, u16 length, seq, sub command, status, ... */
#define POST_RESP_LEN(r) (*(uint16_t *)&(r)[1])

/* unaligned little-endian 16-bit field of a request packet */
static inline uint16_t ble_get_u16(const uint8_t *p)
{
	uint16_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

int ble_process_post_req(struct ble_context *ctx, struct ble_req_hdr *hdr, uint8_t *data)
{
	uint8_t *tx = ctx->tx_buf;
	uint8_t *payload = tx + 4;
	uint8_t resp[20] = {0};
	uint8_t rx[128];

	memcpy(tx, hdr, sizeof(*hdr));
	ctx->tx_len = hdr->len + 4;
	resp[0] = hdr->cmd;

	switch (hdr->cmd) {
	case BLE_CMD_NOTIFICATION_SEND:
		post_notification_cmd_process(ctx, (uint8_t *)hdr, data);
		return 0;
	case BLE_CMD_SYNC_DISPLAY:
		memcpy(ctx->tx_buf, hdr, sizeof(*hdr));
		memcpy(payload, data + 1, hdr->len - 1);
		ctx->tx_len = hdr->len + 4;
		ble_process_req_local(ctx, rx, 8);
		resp[1] = PKCS7_PACK_DONE;
		break;
	case BLE_CMD_BT_UNPAIR: {
		struct bt_conn *conn;
		int err;

		G1_LOG_WRN("*****will unbond current bt connection*****\n\n");
		resp[1] = PKCS7_PACK_DONE;
		ctx->write(resp, sizeof(resp));
		err = bt_unpair(BT_ID_DEFAULT, NULL);
		if (err) {
			G1_LOG_WRN("bt_unpair fail: %d\n\n", err);
		}
		conn = get_current_conn();
		if (conn == NULL) {
			return 0;
		}
		G1_LOG_ERR("bt_conn_disconnect because recv unpair\n");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return 0;
	}
	case BLE_CMD_TEXT_SET: {
		struct dash_data_buf *dash;

		if (hdr->len != 0) {
			memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
			if (data[3] == data[2] - 1) {
				resp[1] = PKCS7_PACK_DONE;
			} else {
				save_esb_snapshot(get_dashboard_state()->dash_buf,
						  sizeof(struct dash_data_buf),
						  get_dashboard_state()->esb_up_id, 1);
				set_esb_up_recv_state(data[1], 1);
				resp[1] = PKCS7_PACK_MORE;
			}
			memcpy(multi_packet_buf, data + 9, hdr->len - 9);
		}
		if (data[3] == 0) {
			memset(get_dashboard_state()->dash_buf->data, 0,
			       sizeof(get_dashboard_state()->dash_buf->data));
			get_dashboard_state()->dash_buf->len = 0;
			get_dashboard_state()->dash_buf->screen_status = data[4];
			get_dashboard_state()->dash_buf->char_pos = data[5] * 256 + data[6];
			get_dashboard_state()->dash_buf->scroll_pct = data[7];
			get_dashboard_state()->dash_buf->page_count = data[8];
		}
		dash = get_dashboard_state()->dash_buf;
		memcpy(&dash->data[get_dashboard_state()->dash_buf->len], multi_packet_buf,
		       hdr->len - 9);
		get_dashboard_state()->dash_buf->len += hdr->len - 9;
		((struct ble_req_hdr *)tx)->len = hdr->len;
		ctx->tx_len = hdr->len + 4;
		if (data[3] == data[2] - 1) {
			uint8_t kind;

			ble_process_req_local(ctx, rx, 8);
			kind = data[4] & 0xf0;
			if (kind == 0x60) {
				dmic_stream_stop();
				get_dashboard_state()->dash_buf->type = 10;
			} else if (kind == 0x70) {
				get_dashboard_state()->dash_buf->type = 15;
				get_dashboard_state()->stay_time_left_s = 0;
			} else {
				bool type_6 = get_dashboard_state()->dash_buf->type == 6;

				if (kind == 0x40) {
					get_dashboard_state()->dash_buf->type = 8;
					get_dashboard_state()->stay_time_left_s = 8;
				} else if (type_6 || kind == 0x30) {
					get_dashboard_state()->dash_buf->type = 7;
					get_dashboard_state()->stay_time_left_s = 0x10;
				} else if (get_dashboard_state()->dash_buf->type > 6 &&
					   kind == 0x50) {
					get_dashboard_state()->dash_buf->type = 9;
					get_dashboard_state()->stay_time_left_s = 0;
				}
			}
		}
		resp[2] = data[1];
		resp[3] = data[2];
		resp[4] = data[3];
		if (is_master() == 1) {
			if (data[3] == data[2] - 1) {
				deferred_text_ack_cmd = BLE_CMD_TEXT_SET;
				deferred_text_ack_total = data[2];
				deferred_text_ack_idx = data[3];
				set_esb_up_recv_state(data[1], 0);
				return 0;
			}
		} else if (data[3] == data[2] - 1) {
			set_esb_up_recv_state(data[1], 0);
		}
		break;
	}
	case BLE_CMD_POST_49:
	case BLE_CMD_MTU_SET:
		ble_process_req_local(ctx, rx, 8);
		resp[1] = PKCS7_PACK_DONE;
		break;
	case BLE_CMD_NOTIFICATION_CLEAR:
		G1_LOG_INF("ble_process_post_req BLE_REQ_POST_DELETE_NOTIFICATION_MSG\n");
		memcpy(payload, data + 1, hdr->len - 1);
		ble_process_req_local(ctx, rx, 8);
		resp[1] = PKCS7_PACK_DONE;
		break;
	case BLE_CMD_NOTIF_AUTO_DISPLAY_SET:
		memcpy(ctx->tx_buf, hdr, sizeof(*hdr));
		memcpy(payload, data + 1, hdr->len - 1);
		ble_process_req_local(ctx, rx, 8);
		resp[1] = rx[0];
		resp[2] = rx[1];
		resp[3] = rx[2];
		break;
	case BLE_CMD_DASHBOARD_LOCK_SET:
		if (ble_get_u16(&data[1]) != hdr->len) {
			G1_LOG_WRN(
				"packet length error,input data length = %d,packet data length = %d\n",
				hdr->len, ble_get_u16(&data[1]));
			return 0;
		}
		G1_LOG_INF("received Dashboard lock command.\n");
		if (get_dashboard_state()->is_master != 1) {
			G1_LOG_ERR("slave received error dashboard lock command! can't exec\n");
			return 0;
		}
		if (data[4] != 1) {
			G1_LOG_ERR("received error dashboard lock action command.\n");
			return 0;
		}
		if (atomic_test_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT)) {
			return 0;
		}
		G1_LOG_INF("master exec dashboard lock process.\n");
		atomic_set_bit(&dashboard_lock_requested, DASHBOARD_LOCK_BIT);
		atomic_clear_bit(&dashboard_lock_reported, DASHBOARD_LOCK_BIT);
		k_timer_start(&DashboardLockTimer, K_SECONDS(1), K_NO_WAIT);
		return 0;
	case BLE_CMD_QUICK_NOTE_UPDATE_TASK: {
		uint16_t data_len = ble_get_u16(&data[1]);

		resp[1] = 6;
		resp[3] = data[3];
		resp[2] = 0;
		resp[4] = data[4];
		if (hdr->len == data_len && data[4] <= 2) {
			if (data[5] == 1) {
				get_dashboard_state()->ts_info->enable = 1;
				get_dashboard_state()->ts_info->action_cmd = data[4];
				get_dashboard_state()->ts_info->ts = k_uptime_get() + 5000;
				get_dashboard_state()->esb_down_id = data[3];
				update_persist_task_status(get_dashboard_state(), 0x12, 2);
			} else {
				get_dashboard_state()->ts_info->enable = 0;
			}
			resp[5] = PKCS7_PACK_DONE;
		} else {
			resp[5] = PKCS7_PACK_ERR;
		}
		break;
	}
	case BLE_CMD_TRANSCRIBE_CONTROL: {
		uint16_t data_len = ble_get_u16(&data[1]);
		uint16_t total, idx;

		if (hdr->len != data_len || data[4] > 3) {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
			ctx->write(resp, POST_RESP_LEN(resp));
			return 0;
		}
		transcribe_info_timeout_reset();
		switch (data[4]) {
		case 1: {
			uint8_t mode = data[5];

			resp[1] = 7;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = 1;
			resp[5] = PKCS7_PACK_DONE;
			resp[6] = mode;
			if (mode == 0) {
				get_dashboard_state()->transcribe_buf->running = mode;
				dmic_stream_stop();
			} else if (mode == 1 || mode == 2) {
				memset(get_dashboard_state()->transcribe_buf, 0,
				       sizeof(struct transcribe_buf));
				get_dashboard_state()->transcribe_buf->running = true;
				get_dashboard_state()->transcribe_buf->mode = mode;
				if (get_dashboard_state()->transcribe_buf->mode == 2) {
					get_dashboard_state()->transcribe_buf->flags = 0;
				} else {
					get_dashboard_state()->transcribe_buf->flags = 0xff;
				}
				get_dashboard_state()->esb_down_id = data[3];
				update_persist_task_status(get_dashboard_state(), 0x13, 2);
			} else if (mode == 3) {
				dmic_stream_pause(1);
				get_dashboard_state()->transcribe_buf->flags &= ~1;
			} else if (mode == 4) {
				dmic_stream_pause(0);
				if (get_dashboard_state()->transcribe_buf->mode != 2) {
					get_dashboard_state()->transcribe_buf->flags |= 1;
				}
			}
			break;
		}
		case 2:
			memcpy(resp, data, 9);
			resp[1] = 10;
			resp[2] = 0;
			resp[9] = PKCS7_PACK_DONE;
			total = ble_get_u16(&data[5]);
			idx = ble_get_u16(&data[7]);
			if (total == 1) {
				G1_LOG_INF("received transcribe packet,total packet = 1\n");
				get_dashboard_state()->transcribe_buf->status = data[9];
				if (data[10] == 1 ||
				    get_dashboard_state()->transcribe_buf->mode == 2) {
					get_dashboard_state()->transcribe_buf->flags &= ~1;
				} else {
					get_dashboard_state()->transcribe_buf->flags |= 1;
				}
				get_dashboard_state()->transcribe_buf->dark_lines = data[11];
				memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
				memset(get_dashboard_state()->transcribe_buf->data, 0,
				       sizeof(get_dashboard_state()->transcribe_buf->data));
				get_dashboard_state()->transcribe_buf->len = data_len - 12;
				memcpy(get_dashboard_state()->transcribe_buf->data, data + 12,
				       get_dashboard_state()->transcribe_buf->len);
				set_esb_recv_state(data[3], 0, 0);
			} else if (idx == 1) {
				G1_LOG_INF("Received transcribe multiple packets, currently the "
					   "first packet of data\n");
				multi_packet_total = total;
				multi_packet_cnt = 1;
				memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
				memcpy(multi_packet_buf, data + 12, data_len - 12);
				multi_packet_len = data_len - 12;
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN("There is a packet order error, current packet "
						   "order = %d, expected packet order = %d\n",
						   idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
					resp[9] = PKCS7_PACK_ERR;
				} else if (multi_packet_len + data_len > 0x20c) {
					G1_LOG_ERR("transcribe data is too long, the current data "
						   "length %d,wait write data length = %d\n",
						   multi_packet_len, data_len - 12);
					resp[9] = PKCS7_PACK_ERR;
				} else {
					memcpy(multi_packet_buf + multi_packet_len, data + 12,
					       data_len - 12);
					multi_packet_len = multi_packet_len - 12 + data_len;
					G1_LOG_INF("Successfully received the subpackage content, "
						   "the packet sequence is %d\n",
						   idx);
					multi_packet_cnt++;
				}
			} else if (idx == total) {
				if (multi_packet_cnt != total - 1 || multi_packet_total != total) {
					G1_LOG_WRN("There is a packet order error, current packet "
						   "order = %d, expected packet order = %d\n",
						   idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
					resp[9] = PKCS7_PACK_ERR;
				} else if (multi_packet_len + data_len > 0x20c) {
					G1_LOG_ERR("transcribe data is too long, the current data "
						   "length %d,wait write data length = %d\n",
						   multi_packet_len, data_len - 12);
					resp[9] = PKCS7_PACK_ERR;
				} else {
					G1_LOG_INF(
						"Successfully assembled multiple packages of data, "
						"and updated the complete content to the UI thread "
						"for data update.\n");
					get_dashboard_state()->transcribe_buf->status = data[9];
					if (data[10] == 1 ||
					    get_dashboard_state()->transcribe_buf->mode == 2) {
						get_dashboard_state()->transcribe_buf->flags &= ~1;
					} else {
						get_dashboard_state()->transcribe_buf->flags |= 1;
					}
					get_dashboard_state()->transcribe_buf->dark_lines =
						data[11];
					memcpy(multi_packet_buf + multi_packet_len, data + 12,
					       data_len - 12);
					multi_packet_len = multi_packet_len - 12 + data_len;
					memset(get_dashboard_state()->transcribe_buf->data, 0,
					       sizeof(get_dashboard_state()->transcribe_buf->data));
					memcpy(get_dashboard_state()->transcribe_buf->data,
					       multi_packet_buf, multi_packet_len);
					get_dashboard_state()->transcribe_buf->len =
						multi_packet_len;
					set_esb_recv_state(data[3], 0, 0);
					multi_packet_total = 0;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
				}
			}
			break;
		case 3:
			resp[1] = 7;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = 3;
			resp[5] = get_dashboard_state()->transcribe_buf->running ? PKCS7_PACK_DONE
										 : PKCS7_PACK_ERR;
			resp[6] = data[5];
			break;
		default:
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
			break;
		}
		ctx->write(resp, POST_RESP_LEN(resp));
		return 0;
	}
	case BLE_CMD_TRANSCRIBE_STATUS:
		if (hdr->len == ble_get_u16(&data[1]) && data[4] <= 3) {
			transcribe_info_timeout_reset();
			if (data[4] == 3) {
				resp[1] = 7;
				resp[2] = 0;
				resp[3] = data[3];
				resp[4] = 3;
				resp[5] = get_dashboard_state()->transcribe_buf->running
						  ? PKCS7_PACK_DONE
						  : PKCS7_PACK_ERR;
				resp[6] = data[5];
			} else {
				resp[1] = 6;
				resp[2] = 0;
				resp[3] = data[3];
				resp[4] = data[4];
				resp[5] = PKCS7_PACK_ERR;
			}
		} else {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
		}
		ctx->write(resp, POST_RESP_LEN(resp));
		return 0;
	case BLE_CMD_EVENAI: {
		uint16_t data_len = ble_get_u16(&data[1]);
		uint8_t sub;

		if (hdr->len != data_len || data[4] > 7) {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
			ctx->write(resp, POST_RESP_LEN(resp));
			return 0;
		}
		even_ai_v2_info_timeout_reset();
		sub = data[4];
		switch (sub) {
		case 1: {
			uint8_t mode = data[5];

			resp[1] = 7;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = 1;
			resp[5] = PKCS7_PACK_DONE;
			resp[6] = mode;
			if (mode == 1) {
				get_dashboard_state()->evenai_buf->state = mode;
				get_dashboard_state()->evenai_buf->sub = mode;
				get_dashboard_state()->evenai_buf->wakeup_arg = data[6];
				get_dashboard_state()->evenai_buf->action_status = data[7];
				memset(get_dashboard_state()->evenai_buf->data, 0,
				       sizeof(get_dashboard_state()->evenai_buf->data));
			} else if (mode == 2) {
				get_dashboard_state()->evenai_buf->state = mode;
				dmic_stream_stop();
				ctx->write(resp, POST_RESP_LEN(resp));
				return 0;
			} else if (mode == 3) {
				get_dashboard_state()->evenai_buf->state = 1;
				get_dashboard_state()->evenai_buf->sub = 0;
			} else {
				if (is_master() == 1) {
					get_dashboard_state()->evenai_buf->state = 0;
				}
				dmic_stream_stop();
				ctx->write(resp, POST_RESP_LEN(resp));
				return 0;
			}
			if (get_dashboard_state()->task_status->task != 0x14) {
				update_persist_task_status(get_dashboard_state(), 0x14, 2);
			}
			set_esb_recv_state(data[3], 0, 0);
			break;
		}
		case 2:
		case 3:
		case 4: {
			int hlen;
			uint16_t total, idx;

			memcpy(resp, data, 9);
			resp[1] = 10;
			resp[2] = 0;
			resp[9] = PKCS7_PACK_DONE;
			if (sub == 2) {
				hlen = 11;
			} else if (sub == 3) {
				hlen = 12;
			} else {
				hlen = 10;
			}
			idx = ble_get_u16(&data[7]);
			total = ble_get_u16(&data[5]);
			if (total == 1) {
				int n;
				uint8_t *payload;

				G1_LOG_INF("received evenai_v2 packet,total packet = 1\n");
				n = data_len - hlen;
				if (n > 0x200) {
					G1_LOG_ERR(
						"evenai_v2 data is too long, the current data length "
						"0,wait write data length = %d\n",
						n);
					resp[9] = PKCS7_PACK_ERR;
					ctx->write(resp, POST_RESP_LEN(resp));
					return 0;
				}
				payload = data + hlen;
				get_dashboard_state()->evenai_buf->sub = sub;
				get_dashboard_state()->evenai_buf->action_status = data[9];
				if (sub != 4) {
					get_dashboard_state()->evenai_buf->scroll_line = data[10];
					if (sub == 2) {
						get_dashboard_state()->evenai_buf->ts =
							k_uptime_get();
					} else {
						get_dashboard_state()->evenai_buf->scroll_pct =
							data[11];
					}
				} else {
					if (get_dashboard_state()->evenai_buf->action_status == 1) {
						get_dashboard_state()
							->dashboard_ts->dashboard_hold = 2;
					} else if (get_dashboard_state()
							   ->evenai_buf->action_status == 0x13) {
						if (get_dashboard_state()->task_status->task !=
						    0x14) {
							if (get_dashboard_state()->dashboard_mode ==
								    6 &&
							    get_dashboard_state()
									    ->dashboard_ts
									    ->exit_pending == 0) {
								get_dashboard_state()
									->dashboard_ts
									->exit_pending = 1;
							}
							get_dashboard_state()->evenai_buf->state =
								1;
							update_persist_task_status(
								get_dashboard_state(), 0x14, 2);
						}
						get_dashboard_state()->evenai_buf->ts =
							k_uptime_get();
					}
				}
				if (sub != 2 ||
				    get_dashboard_state()->evenai_buf->action_status != 1 ||
				    payload[0] != 0) {
					memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
					memset(get_dashboard_state()->evenai_buf->data, 0,
					       sizeof(get_dashboard_state()->evenai_buf->data));
					get_dashboard_state()->evenai_buf->len = n;
					memcpy(get_dashboard_state()->evenai_buf->data, payload,
					       get_dashboard_state()->evenai_buf->len);
				}
				set_esb_recv_state(data[3], 0, 0);
			} else if (idx == 1) {
				G1_LOG_INF(
					"Received evenai_v2 multiple packets, currently the first "
					"packet of data\n");
				multi_packet_total = total;
				multi_packet_cnt = 1;
				memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
				memcpy(multi_packet_buf, data + hlen, data_len - hlen);
				multi_packet_len = data_len - hlen;
			} else if (idx < total) {
				if (multi_packet_cnt != idx - 1) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order = "
						"%d, expected packet order = %d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					multi_packet_len = 0;
					memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
					resp[9] = PKCS7_PACK_ERR;
					ctx->write(resp, POST_RESP_LEN(resp));
					return 0;
				}
				if (multi_packet_len + data_len - hlen > 0x200) {
					G1_LOG_ERR(
						"evenai_v2 data is too long, the current data length "
						"%d,wait write data length = %d\n",
						multi_packet_len, data_len - hlen);
					resp[9] = PKCS7_PACK_ERR;
					ctx->write(resp, POST_RESP_LEN(resp));
					return 0;
				}
				memcpy(multi_packet_buf + multi_packet_len, data + hlen,
				       data_len - hlen);
				multi_packet_len = multi_packet_len + data_len - hlen;
				G1_LOG_INF(
					"Successfully received the subpackage content, the packet "
					"sequence is %d\n",
					idx);
				multi_packet_cnt++;
			} else if (idx == total) {
				if (multi_packet_cnt != idx - 1 || idx != multi_packet_total) {
					G1_LOG_WRN(
						"There is a packet order error, current packet order = "
						"%d, expected packet order = %d\n",
						idx, multi_packet_cnt + 1);
					multi_packet_total = total;
					multi_packet_cnt = 0;
					memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
					resp[9] = PKCS7_PACK_ERR;
					ctx->write(resp, POST_RESP_LEN(resp));
					return 0;
				}
				if (multi_packet_len + data_len - hlen > 0x200) {
					G1_LOG_ERR(
						"evenai_v2 data is too long, the current data length "
						"%d,wait write data length = %d\n",
						multi_packet_len, data_len - hlen);
					resp[9] = PKCS7_PACK_ERR;
					ctx->write(resp, POST_RESP_LEN(resp));
					return 0;
				}
				G1_LOG_INF("Successfully assembled multiple packages of data, and "
					   "updated the complete content to the UI thread for data "
					   "update.\n");
				get_dashboard_state()->evenai_buf->sub = sub;
				get_dashboard_state()->evenai_buf->action_status = data[9];
				if (sub != 4) {
					get_dashboard_state()->evenai_buf->scroll_line = data[10];
					if (sub == 2) {
						get_dashboard_state()->evenai_buf->ts =
							k_uptime_get();
					} else {
						get_dashboard_state()->evenai_buf->scroll_pct =
							data[11];
					}
				} else {
					if (get_dashboard_state()->evenai_buf->action_status == 1) {
						get_dashboard_state()
							->dashboard_ts->dashboard_hold = 2;
					} else if (get_dashboard_state()
							   ->evenai_buf->action_status == 0x13) {
						if (get_dashboard_state()->task_status->task !=
						    0x14) {
							if (get_dashboard_state()->dashboard_mode ==
								    6 &&
							    get_dashboard_state()
									    ->dashboard_ts
									    ->exit_pending == 0) {
								get_dashboard_state()
									->dashboard_ts
									->exit_pending = 1;
							}
							get_dashboard_state()->evenai_buf->state =
								1;
							update_persist_task_status(
								get_dashboard_state(), 0x14, 2);
						}
						get_dashboard_state()->evenai_buf->ts =
							k_uptime_get();
					}
				}
				memcpy(multi_packet_buf + multi_packet_len, data + hlen,
				       data_len - hlen);
				multi_packet_len = multi_packet_len + data_len - hlen;
				if (sub != 2 ||
				    get_dashboard_state()->evenai_buf->action_status != 1 ||
				    data[hlen] != 0) {
					memset(get_dashboard_state()->evenai_buf->data, 0,
					       sizeof(get_dashboard_state()->evenai_buf->data));
					memcpy(get_dashboard_state()->evenai_buf->data,
					       multi_packet_buf, multi_packet_len);
					get_dashboard_state()->evenai_buf->len = multi_packet_len;
				}
				set_esb_recv_state(data[3], 0, 0);
				multi_packet_total = 0;
				multi_packet_cnt = 0;
				multi_packet_len = 0;
			}
			break;
		}
		case 5:
			resp[1] = 7;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = 5;
			if (get_dashboard_state()->evenai_buf->state == 1) {
				resp[5] = PKCS7_PACK_DONE;
			} else {
				resp[5] = PKCS7_PACK_ERR;
			}
			resp[6] = data[5];
			break;
		default:
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = sub;
			resp[5] = PKCS7_PACK_ERR;
			break;
		}
		ctx->write(resp, POST_RESP_LEN(resp));
		return 0;
	}
	case BLE_CMD_EVENAI_STATUS:
		if (ble_get_u16(&data[1]) != hdr->len || data[4] > 7) {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
			ctx->write(resp, POST_RESP_LEN(resp));
			return 0;
		}
		even_ai_v2_info_timeout_reset();
		if (data[4] == 5) {
			resp[1] = 7;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = 5;
			if (get_dashboard_state()->evenai_buf->state == 1) {
				resp[5] = PKCS7_PACK_DONE;
			} else {
				resp[5] = PKCS7_PACK_ERR;
			}
			resp[6] = data[5];
		} else {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = PKCS7_PACK_ERR;
		}
		ctx->write(resp, POST_RESP_LEN(resp));
		return 0;
	case BLE_CMD_EVENAI_EXIT:
		if (ble_get_u16(&data[1]) == hdr->len) {
			uint8_t sub, seq, flag;

			even_ai_v2_info_timeout_reset();
			sub = data[4];
			seq = data[3];
			flag = data[5];
			if (sub == 8) {
				resp[1] = 7;
				resp[2] = 0;
				resp[3] = seq;
				resp[4] = 8;
				resp[5] = 0;
				resp[6] = flag;
				if (flag == 0) {
					if (get_dashboard_state()->dashboard_ts->exit_pending ==
					    0) {
						get_dashboard_state()->dashboard_ts->exit_pending =
							1;
					}
					dmic_stream_stop();
				}
			} else {
				resp[1] = 7;
				resp[2] = 0;
				resp[3] = seq;
				resp[4] = sub;
				resp[5] = 1;
				resp[6] = flag;
			}
		} else {
			resp[1] = 6;
			resp[2] = 0;
			resp[3] = data[3];
			resp[4] = data[4];
			resp[5] = 1;
		}
		ctx->write(resp, POST_RESP_LEN(resp));
		return 0;
	default:
		G1_LOG_WRN("@tx error post req_type: %d\n", hdr->cmd);
		resp[1] = PKCS7_PACK_ERR;
		resp[2] = 0x15;
		strcpy((char *)&resp[2], "error post req.");
		break;
	}
	ctx->write(resp, sizeof(resp));
	return 0;
}
