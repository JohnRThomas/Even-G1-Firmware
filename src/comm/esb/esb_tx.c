/*
 * ESB packets sent to the other lens through the network core: the master's
 * sync packet (sync_to_slave), the slave's status packet (sync_to_master), the
 * thread that keeps the two lenses' screen state and notifications in step, and
 * the ESB band selection.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include "comm/esb/esb.h"
#include "comm/ble/report.h"
#include "comm/ipc/ipc_host.h"
#include "system/logging.h"
#include "system/system_state.h"
#include "system/work_mode.h"
#include "main.h"
#include "services/notifications/notify.h"
#include "services/production_test/production_test.h"
#include "services/tasks/tasks.h"
#include "ui/screen_task.h"
#include "util/date.h"
#include "system/clock.h"
#include "drivers/mic/dmic.h"

uint8_t push_msg_syncing;
static uint8_t esb_sending;
static uint8_t esb_tx_busy;
static uint8_t note_retry;
static struct esb_pkt last_status_pkt;
static uint8_t esb_tx_buf[252];
static uint16_t last_sync_wait;
static uint8_t esb_last_cmd = 0x13;

static K_THREAD_STACK_DEFINE(msg_sync_stack, 0x400);
static struct k_thread msg_sync_thread_data;
void set_band_for_ESB(uint8_t band)
{
	uint8_t msg[64] = {0};

	msg[0] = 1;
	msg[1] = band;
	global_ipc_service_send(msg, 4);
}

int sync_to_slave(struct GlassesState *state, int cmd, void *data, size_t len)
{
	uint8_t buf[0xfc] = {0};
	uint32_t date = state->dashboard_ts->ts;
	uint32_t old_date;
	uint16_t wait = 0;
	uint16_t timeout;
	uint16_t n;
	uint16_t busy;
	uint8_t seq;
	uint8_t st;
	int64_t now;
	int skip = 0;
	int same = 0;

	if (get_work_mode() != 1 && get_dashboard_state()->work_mode != 8 &&
	    get_pt_test_mode() != 1 && state->is_master != 2) {
		now = k_uptime_get();
		n = 501;
		if (state->last_sync_ts == 0 || now - state->last_sync_ts > 5000) {
			timeout = 500;
		} else {
			timeout = 5000;
			if (cmd) {
				timeout = 3000;
			}
		}
		while (task_status_lock && --n) {
			k_msleep(1);
		}
		busy = 0;
		while (esb_sending) {
			k_msleep(1);
			busy++;
		}
		if (busy && esb_last_cmd == cmd) {
			if (cmd) {
				G1_LOG_ERR("ESB send same type cmd %d, exit\n", cmd);
				esb_last_cmd = 0;
				skip = 1;
			}
		} else if (esb_last_cmd == 1) {
			if (cmd != 0) {
				if (cmd == 1 && last_sync_wait < 500) {
					same = 1;
				}
			} else {
				G1_LOG_ERR("ESB send data after suspend cmd, ignore it.\n");
				esb_last_cmd = 0;
				skip = 1;
			}
		} else if (cmd && esb_last_cmd == cmd && cmd != 7 && (cmd < 12 || cmd > 16) &&
			   last_sync_wait < 500) {
			same = 1;
		}
		if (same) {
			G1_LOG_ERR("ESB send same cmd, ignore it %d.\n", cmd);
			esb_last_cmd = 0;
			skip = 1;
		}
		if (!skip) {
			esb_sending = 1;
			if (cmd < 3) {
				esb_last_cmd = cmd;
			}
			seq = state->sync_seq;
			if (state->dashboard_mode == 10) {
				st = state->nav->imu_action_status;
			} else {
				st = state->imu.mode;
			}
			state->sync_seq = seq + 1;
			state->sync_hdr = (struct esb_hdr){.cmd = cmd, .dir = st};
			state->esb_hdr.dir = 1;
			state->sync_canvas_x = state->canvas_x;
			state->sync_canvas_y = state->panel.current_column + state->canvas_y;
			buf[0] = 2;
			old_date = state->date_ts;
			state->date_ts = date;
			if (data && len) {
				if (len > 4) {
					memcpy(state->sync_data, data, 4);
				} else {
					memcpy(state->sync_data, data, len);
				}
			} else {
				memset(state->sync_data, 0, 4);
			}
			memcpy(&buf[1], &state->esb_hdr, 11);
			memcpy(&buf[12], &state->sync, sizeof(struct esb_sync_pkt));
			state->sync_ack = 1;
			if (state->esb_screen_id != 6 && state->esb_screen_id != 0 && cmd == 0) {
				if (state->esb_dir != state->imu.mode) {
					state->sync_seq = seq;
					G1_LOG_ERR("ESB send imu status is changed, ignore it.\n");
					skip = 1;
				}
				if (!skip) {
					if (state->dashboard_mode == 16) {
						if (state->esb_up_seq != state->esb_up_id ||
						    state->esb_up_recving) {
							state->sync_seq = seq;
							G1_LOG_ERR(
								"ESB send even ai is changed or receiving, ignore it.\n");
							skip = 1;
						}
					} else if (state->esb_dir == 2 &&
						   state->esb_up_seq != state->esb_up_id) {
						state->sync_seq = seq;
						G1_LOG_ERR(
							"ESB send up package is changed, ignore it.\n");
						skip = 1;
					}
				}
				if (!skip) {
					if (state->imu.mode != 2) {
						if (state->imu.mode == 1 &&
						    state->esb_down_seq != state->esb_down_id) {
							state->sync_seq = seq;
							G1_LOG_ERR(
								"ESB send down package is changed, ignore it.\n");
							skip = 1;
						}
					} else if (state->esb_up_recving) {
						state->sync_seq = seq;
						G1_LOG_ERR(
							"ESB send up package is receiving, ignore it.\n");
						skip = 1;
					}
				}
				if (!skip && state->imu.mode == 1 && state->esb_down_recving) {
					state->sync_seq = seq;
					G1_LOG_ERR(
						"ESB send down package is receiving, ignore it.\n");
					skip = 1;
				}
				if (!skip && state->esb_screen_id != state->dashboard_mode &&
				    state->dashboard_mode > 6 && state->esb_screen_id > 6) {
					state->sync_seq = seq;
					G1_LOG_ERR("ESB send package id is changed, ignore it.\n");
					skip = 1;
				}
			}
		}
		if (skip) {
			wait = 5000;
		} else {
			state->ipc_ctx.send(buf, 0x21);
			for (wait = 0; state->sync_ack != 2;) {
				if (++wait >= timeout) {
					G1_LOG_ERR("sync to slave failed!!!!!! wait_count %d\n",
						   wait);
					break;
				}
				k_msleep(1);
			}
			if (state->sync_ack == 2) {
				set_date(date);
			} else {
				set_date(old_date);
			}
			if (wait < timeout) {
				state->last_sync_ts = now;
			}
		}
	}
	last_sync_wait = wait;
	esb_sending = 0;
	return wait;
}

unsigned int sync_to_master(struct GlassesState *state, int cmd, void *data, size_t len)
{
	uint16_t wait = 0;
	int skip = 0;
	struct fw_ver *ver = &state->fw_ver;

	if (get_pt_test_mode() == 1) {
		esb_tx_busy = 0;
		return wait;
	}
	while (esb_tx_busy) {
		if (++wait == 2000) {
			esb_tx_busy = 0;
			return wait;
		}
		k_msleep(1);
	}
	esb_tx_busy = 1;
	state->esb_pkt.seq++;
	state->esb_pkt.cmd = (cmd & 0x3f) | 0x80;
	state->esb_pkt.ver.major = ver->major;
	state->esb_pkt.ver.minor = ver->minor;
	state->esb_pkt.ver.patch = ver->patch;
	memcpy(state->esb_pkt.mac, state->slave_mac, sizeof(state->esb_pkt.mac));
	state->esb_pkt.battery = state->battery_info[0];
	if (cmd == 12 || cmd == 13) {
		esb_tx_buf[0] = 4;
		memcpy(&esb_tx_buf[1], &state->esb_pkt, sizeof(state->esb_pkt));
		memcpy(&esb_tx_buf[2], data, len);
		state->sync_ack = 1;
		state->ipc_ctx.send(esb_tx_buf, 0xdc);
	} else {
		if (data && len) {
			memcpy(state->esb_pkt.data, data, MIN(len, 2));
		}
		if (cmd != 1) {
			esb_tx_buf[0] = 4;
			memcpy(&esb_tx_buf[1], &state->esb_pkt, sizeof(state->esb_pkt));
		} else {
			last_status_pkt.seq = state->esb_pkt.seq;
			if (memcmp(&last_status_pkt, &state->esb_pkt, sizeof(last_status_pkt)) ==
			    0) {
				skip = 1;
			} else {
				esb_tx_buf[0] = 4;
				memcpy(&esb_tx_buf[1], &state->esb_pkt, sizeof(state->esb_pkt));
			}
		}
		if (!skip) {
			state->sync_ack = 1;
			state->ipc_ctx.send(esb_tx_buf, 0x21);
		}
	}
	if (!skip) {
		while (state->sync_ack != 2) {
			if (++wait == 2000) {
				if (cmd == 1) {
					memset(&last_status_pkt, 0, sizeof(last_status_pkt));
				}
				break;
			}
			k_msleep(1);
		}
		if (cmd == 1 && state->sync_ack == 2) {
			memcpy(&last_status_pkt, &state->esb_pkt, sizeof(last_status_pkt));
		}
	}
	esb_tx_busy = 0;
	return wait;
}

static void sync_push_message(void)
{
	struct notif_msg *item;

	if (get_dashboard_state()->sync_flags & BIT(2)) {
		return;
	}
	if (get_dashboard_state()->esb_pkt.flags & BIT(2)) {
		return;
	}
	item = get_push_message_copy();
	if (!item) {
		return;
	}
	if (get_dashboard_state()->is_master == 1) {
		switch (item->sync_state) {
		case 1:
			push_msg_syncing = 1;
			if (sync_to_slave(get_dashboard_state(), 10, &item->uid, 4) < 3000) {
				set_msg_sync_state(item->pos, 2);
			}
			break;
		case 3:
			if (sync_to_slave(get_dashboard_state(), 11, &item->uid, 4) < 3000) {
				set_msg_sync_state(item->pos, 4);
				confirm_message(item->uid);
				push_msg_syncing = 0;
			}
			break;
		default:
			break;
		}
	} else if (!msg_busy) {
		switch (item->sync_state) {
		case 0:
			push_msg_syncing = 1;
			sync_to_master(get_dashboard_state(), 12, item, NOTIF_MSG_HALF);
			note_retry = 0;
			break;
		case 2:
			if (++note_retry >= 10) {
				note_retry = 0;
				set_msg_sync_state(item->pos, 0);
				push_msg_syncing = 0;
			} else {
				sync_to_master(get_dashboard_state(), 13,
					       (uint8_t *)item + NOTIF_MSG_HALF, NOTIF_MSG_HALF);
			}
			break;
		default:
			break;
		}
	}
}

static void msg_sync_thread(void *p1, void *p2, void *p3)
{
	struct GlassesState *state = p1;
	uint8_t data[4];
	int64_t now;

	for (;;) {
		if (get_dashboard_state()->work_mode == 1 ||
		    get_dashboard_state()->work_mode == 8) {
			k_sleep(K_MSEC(5000));
			continue;
		}
		if (get_dashboard_state()->is_master == 1) {
			switch (get_dashboard_state()->dash_buf->type) {
			case 1:
				if (sync_to_slave(get_dashboard_state(), 0xd, NULL, 0) < 500) {
					exit_temp_task(state, 0);
					reset_dash_buf(0);
					if (dmic_get_stop_state()) {
						get_dashboard_state()->dash_buf->type = 2;
					} else {
						get_dashboard_state()->dash_buf->type = 3;
					}
					if (get_dashboard_state()->task_status->task != 0x10) {
						update_persist_task_status(get_dashboard_state(),
									   0x10, 2);
					}
				}
				break;
			case 3:
				if (sync_to_slave(get_dashboard_state(), 0xe, NULL, 0) < 500) {
					get_dashboard_state()->dash_buf->type = 4;
				}
				break;
			case 5:
				if (sync_to_slave(get_dashboard_state(), 0xf, NULL, 0) < 500) {
					dmic_stream_stop();
					report_event_to_phone(0x18);
					get_dashboard_state()->dash_buf->type = 6;
				}
				break;
			case 10:
				if (sync_to_slave(get_dashboard_state(), 0x10, NULL, 0) < 500) {
					get_dashboard_state()->dash_buf->type = 0xb;
					get_dashboard_state()->stay_time_left_s = 8;
				}
				break;
			case 13:
				if (sync_to_slave(get_dashboard_state(), 0x11, NULL, 0) < 500) {
					get_dashboard_state()->dash_buf->type = 0xe;
				}
				break;
			case 15:
				if (sync_to_slave(get_dashboard_state(), 0x12, NULL, 0) < 500) {
					get_dashboard_state()->dash_buf->type = 0x10;
				}
				break;
			default:
				break;
			}
			if (get_dashboard_state()->evenai_buf->sub == 6 &&
			    get_dashboard_state()->evenai_buf->action_status < 3) {
				data[0] = 0xe;
				data[1] = get_dashboard_state()->evenai_buf->sub;
				data[2] = get_dashboard_state()->evenai_buf->action_status;
				data[3] = 0;
				if (sync_to_slave(get_dashboard_state(), 6, data, 4) < 500) {
					exit_temp_task(get_dashboard_state(), 0);
					if (get_dashboard_state()->task_status->task == 0x14 &&
					    get_dashboard_state()->evenai_buf->state == 1 &&
					    get_dashboard_state()->evenai_buf->sub == 4 &&
					    get_dashboard_state()->evenai_buf->action_status ==
						    0x13) {
						G1_LOG_WRN(
							"evenai_v2 need to show network_err tip\n");
						get_dashboard_state()->task_status->status = 2;
					} else {
						if (get_dashboard_state()->task_status->task !=
						    0x14) {
							get_dashboard_state()->evenai_buf->state =
								1;
							memset(get_dashboard_state()
								       ->evenai_buf->data,
							       0, 0x200);
							update_persist_task_status(
								get_dashboard_state(), 0x14, 2);
						}
						get_dashboard_state()->evenai_buf->sub = 7;
						now = k_uptime_get();
						get_dashboard_state()->evenai_buf->ts = now;
					}
				}
			}
		} else {
			switch (get_dashboard_state()->dash_buf->type) {
			case 1:
				sync_to_master(get_dashboard_state(), 0xf, NULL, 0);
				exit_temp_task(state, 0);
				break;
			case 5:
				sync_to_master(get_dashboard_state(), 0x10, NULL, 0);
				break;
			default:
				break;
			}
			if (get_dashboard_state()->evenai_buf->sub == 6) {
				data[0] = get_dashboard_state()->evenai_buf->sub;
				data[1] = get_dashboard_state()->evenai_buf->action_status;
				sync_to_master(get_dashboard_state(), 0x11, data, 2);
			}
		}
		sync_push_message();
		if (get_dashboard_state()->is_master == 1 && slave_msg_key_pending) {
			sync_message_signal_to_slave(3);
			if (get_dashboard_state()->message_mode && count_unread_messages() &&
			    get_dashboard_state()->dashboard_mode != 4) {
				update_temp_task_status(get_dashboard_state(), 4, 2);
				trigger_screen_state_change("IMU:wakeup:new-notification",
							    get_dashboard_state(), 1);
				set_msg_timeout_flag();
			}
			slave_msg_key_pending = 0;
			handle_msg_touch_key();
			set_cur_message_state();
		}
		k_msleep(200);
	}
}

void esb_sync_start(struct GlassesState *state)
{
	k_thread_create(&msg_sync_thread_data, msg_sync_stack,
			K_THREAD_STACK_SIZEOF(msg_sync_stack), msg_sync_thread, state, NULL, NULL,
			-10, 0, K_NO_WAIT);
}
