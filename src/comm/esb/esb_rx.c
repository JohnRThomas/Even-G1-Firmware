/*
 * ESB packets received from the other lens, delivered by the network core over
 * IPC: the master's view of the slave's status packets ("cpunet-esbm-cpu-sync")
 * and the slave's view of the master's sync packets ("cpunet-esbs").
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "comm/esb/esb.h"
#include "comm/proxy_thread.h"
#include "system/logging.h"
#include "system/work_mode.h"
#include "main.h"
#include "services/notifications/notify.h"
#include "services/storage/storage.h"
#include "ui/screens/ui.h"
#include "util/date.h"
#include "ui/screen_task.h"
#include "services/tasks/tasks.h"
#include "system/clock.h"
#include "ui/screens/ui_bitmap.h"
#include "ui/screens/ui_raster_height.h"
#include "services/storage/setting_store.h"

#define ESB_CMD_MASK 0x3f
#define ESB_DIR_MASK 0xc0
#define ESB_DIR_UP   0x80
#define ESB_DIR_DOWN 0x40

uint8_t slave_msg_key_pending;
static uint8_t esbm_last_seq;
int esb_slave_recv(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	uint8_t buf[22] = {0};
	struct esb_sync_pkt *new_data_pkg = (struct esb_sync_pkt *)buf;
	struct esb_sync_pkt *data_pkg = &gs->sync;
	struct GlassesState *g;
	struct notif_msg *slot;
	uint32_t id;
	uint8_t cmd, dist;

	memcpy(new_data_pkg, &data[11], sizeof(*new_data_pkg));
	if (new_data_pkg->package_id == data_pkg->package_id) {
		G1_LOG(0, "slave recv same package\n");
		return 0;
	}
	if (new_data_pkg->package_id != (uint8_t)(data_pkg->package_id + 1)) {
		G1_LOG(0,
		       "###############loss package new_data_pkg->package_id %d data_pkg->package_id %d\n",
		       new_data_pkg->package_id, data_pkg->package_id);
	}

	cmd = buf[0] & ESB_CMD_MASK;
	if (cmd == 1) {
		if (gs->suspend_state == 1) {
			data_pkg->package_id++;
			G1_LOG(0, "slave recv suspend package\n");
			return 0;
		}
	} else if (cmd == 2 && gs->suspend_state == 0) {
		data_pkg->package_id++;
		G1_LOG(0, "slave recv resume package\n");
	}

	if (data_pkg->screen_id != new_data_pkg->screen_id && cmd == 8) {
		data_pkg->package_id++;
		G1_LOG(0, "screen id is not same\n");
		return 0;
	}

	if (data_pkg->screen_id == 9 || data_pkg->screen_id == 11 || data_pkg->screen_id == 10) {
		if ((buf[0] & ESB_DIR_MASK) == ESB_DIR_UP) {
			if (gs->esb_up_recving) {
				data_pkg->package_id++;
				G1_LOG(0, "slave recv up recving\n");
				return 0;
			}
		} else if ((buf[0] & ESB_DIR_MASK) == ESB_DIR_DOWN && gs->esb_down_recving) {
			data_pkg->package_id++;
			G1_LOG(0, "slave recv down recving\n");
			return 0;
		}
	} else if (data_pkg->screen_id == 0x10 && gs->esb_up_recving) {
		G1_LOG(0, "slave even ai recv down recving\n");
		data_pkg->package_id++;
		return 0;
	}

	if (new_data_pkg->screen_id != 6 && new_data_pkg->screen_id != 0) {
		gs->esb_up_seq = data[3];
		gs->esb_down_seq = data[4];
		if (cmd == 0) {
			if ((buf[0] & ESB_DIR_MASK) == ESB_DIR_UP ||
			    (new_data_pkg->screen_id == 0x10 && GLOBAL_STATE->dash_buf->type >= 6 &&
			     GLOBAL_STATE->dash_buf->type <= 9)) {
				if (gs->esb_up_id != data[3] &&
				    gs->esb_up_snapshot[ESB_SNAPSHOT_SEQ] != data[3]) {
					data_pkg->package_id++;
					G1_LOG(0, "slave recv wrong up package\n");
					return 0;
				}
			} else if (gs->esb_down_id != data[4] &&
				   gs->esb_down_snapshot[ESB_SNAPSHOT_SEQ] != data[4]) {
				data_pkg->package_id++;
				G1_LOG(0, "slave recv wrong down package\n");
				return 0;
			}
		}
	}

	gs->esb_hdr.cmd = data[0] & ESB_CMD_MASK;
	gs->esb_arg1 = data[1];
	gs->esb_arg2 = data[2];
	set_date(UNALIGNED_GET((const uint32_t *)&data[7]));
	new_data_pkg->screen_id = data_pkg->screen_id;
	*data_pkg = *new_data_pkg;
	gs->imu.mode = new_data_pkg->hdr.dir;
	gs->dashboard_ts->ts = UNALIGNED_GET((const uint32_t *)&data[7]);
	if (gs->user_sched_info->ts) {
		gs->user_sched_info->ts = gs->dashboard_ts->ts;
	}
	gs->esb_dir = new_data_pkg->hdr.dir;
	gs->esb_screen_id = new_data_pkg->screen_id;
	if (new_data_pkg->flags & BIT(2)) {
		clear_all_messages();
		data_pkg->flags &= ~BIT(2);
	}

	switch (new_data_pkg->hdr.cmd) {
	case 8:
		if (data_pkg->screen_id != new_data_pkg->screen_id && cmd == 8) {
			G1_LOG(0, "screen id is not same, exit\n");
			return 0;
		}
		exit_running_task_keep_status(GLOBAL_STATE, 0);
		if (new_data_pkg->arg[0] && GLOBAL_STATE->sync.screen_id == 4) {
			expire_cur_message();
			if (GLOBAL_STATE->sync.unread_msg_num) {
				set_msg_show_state(6);
			} else {
				set_msg_show_state(0);
			}
		}
		if (GLOBAL_STATE->imu.mode == 2) {
			get_dashboard_startup_mode();
		}
		update_persist_task_status_to_idle(GLOBAL_STATE);
		k_sem_give(&GLOBAL_STATE->sem_2);
		break;
	case 10:
		if (GLOBAL_STATE->esb_pkt.flags & BIT(2)) {
			return 0;
		}
		slot = find_push_message_by_uid(UNALIGNED_GET((uint32_t *)new_data_pkg->arg));
		G1_LOG(0, "##########%s %d SYNC_DISPLAY_CMD_COMFIRM_NOTIFY_FIRST slave before\n",
		       __func__, __LINE__);
		if (slot && slot->sync_state == 0) {
			slot->sync_state = 2;
		}
		break;
	case 11:
		/* the master confirmed the end of the notification display */
		if (GLOBAL_STATE->esb_pkt.flags & BIT(2)) {
			return 0;
		}
		id = UNALIGNED_GET((uint32_t *)new_data_pkg->arg);
		slot = find_push_message_by_uid(id);
		G1_LOG(0, "##########%s %d SYNC_DISPLAY_CMD_COMFIRM_NOTIFY_END slave before\n",
		       __func__, __LINE__);
		if (slot && slot->sync_state == 2) {
			slot->sync_state = 4;
			confirm_message(id);
			push_msg_syncing = 0;
		}
		break;
	case 13:
		reset_dash_buf(0);
		g = GLOBAL_STATE;
		g->dash_buf->type = 2;
		if (g->task_status->task != 0x10) {
			update_persist_task_status(g, 0x10, 2);
		}
		break;
	case 14:
		GLOBAL_STATE->dash_buf->type = 4;
		break;
	case 15:
		GLOBAL_STATE->dash_buf->type = 6;
		break;
	case 16:
		g = GLOBAL_STATE;
		g->dash_buf->type = 11;
		g->sync.stay_time_left_s = 8;
		break;
	case 17:
		GLOBAL_STATE->dash_buf->type = 14;
		break;
	case 18:
		GLOBAL_STATE->dash_buf->type = 16;
		break;
	case 12:
		if (new_data_pkg->arg[0] == 11) {
			dist = new_data_pkg->arg[1];
			G1_LOG(1, "slave rcv canvas_distance_gear:%d,raster_height_gear:%d\n", dist,
			       new_data_pkg->arg[2]);
			if (dist < 10 && new_data_pkg->arg[2] < 9) {
				g = GLOBAL_STATE;
				g->panel.canvas_distance_gear = dist;
				g->panel.raster_height_offset = new_data_pkg->arg[2];
				cal_panel_canvas_coord(&g->panel.y_offset,
						       &g->panel.current_column);
			}
			if (GLOBAL_STATE->dashboard_ts->exit_pending) {
				GLOBAL_STATE->dashboard_ts->exit_pending = 0;
			}
		}
		break;
	default:
		k_sem_give(&gs->sem_1);
		break;
	}

	if (gs->esb_hdr.cmd == 3 && gs->esb_hdr.dir == 2 && gs->esb_arg1 == 1) {
		switch_to_dfu_mode(&gs->main_sem);
	}
	return 0;
}

int esb_master_recv(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	const struct notif_msg *src;
	struct notif_msg *slot;
	uint8_t cmd = data[0] & 0x3f;

	if (cmd != 0 && cmd != 12 && cmd != 13) {
		if (esbm_last_seq == data[0x15]) {
			return 0;
		}
		esbm_last_seq = data[0x15];
		gs->fw_ver = gs->esb_pkt.ver;
		gs->battery_right = gs->esb_pkt.battery;
		memcpy(gs->slave_mac, gs->esb_pkt.mac, sizeof(gs->slave_mac));
		memcpy(&gs->esb_pkt, data, sizeof(gs->esb_pkt));
		if (data[0x12] & BIT(2)) {
			clear_all_messages();
			gs->esb_pkt.flags &= ~BIT(2);
		}
	}

	switch (((const struct esb_hdr *)data)->cmd) {
	case 4:
		if (GLOBAL_STATE->sync.screen_id == 4) {
			expire_cur_message();
			if (GLOBAL_STATE->sync.unread_msg_num) {
				set_msg_show_state(6);
			} else {
				set_msg_show_state(0);
			}
		}
		atomic_or(&close_app_flags, BIT(1));
		exit_running_task(gs, 1);
		if (gs->imu.mode == 2) {
			get_dashboard_startup_mode();
		}
		update_persist_task_status_to_idle(gs);
		break;
	case 5:
		exit_running_task(gs, 1);
		change_work_mode(7);
		break;
	case 8:
		exit_running_task(gs, 1);
		gs->display_mode = 12;
		gs->dashboard_cmd = 3;
		k_sem_give(&gs->dashboard_position_sem);
		break;
	case 9:
		gs->display_mode = 10;
		gs->dashboard_cmd = 3;
		k_sem_give(&gs->dashboard_position_sem);
		show_not_disturb_bitmap(1);
		break;
	case 6:
		if (gs->esb_pkt.subcmd == 1) {
			gs->quicknote_show_info->longpress_req = 1;
		} else if (gs->esb_pkt.subcmd == 0) {
			gs->quicknote_show_info->longpress_req = 2;
		}
		break;
	case 7:
		SlaveDoubleClickEventInject();
		break;
	case 10:
		slave_msg_key_pending = 1;
		break;
	case 11:
		break;
	case 12:
		if ((GLOBAL_STATE->sync.flags & BIT(2)) || msg_array_busy) {
			break;
		}
		src = (const struct notif_msg *)&data[1];
		slot = get_message_by_index(data[0xd]);
		if (slot && !slot->busy &&
		    (src->uid != slot->uid || src->recv_time != slot->recv_time ||
		     slot->sync_state != 1)) {
			memcpy(slot, &data[1], NOTIF_MSG_HALF);
			slot->sync_state = 1;
		}
		break;
	case 13:
		if ((GLOBAL_STATE->sync.flags & BIT(2)) || msg_array_busy) {
			break;
		}
		slot = get_push_message();
		if (slot && !slot->busy && slot->sync_state != 3) {
			memcpy((uint8_t *)slot + NOTIF_MSG_HALF, &data[1], NOTIF_MSG_HALF);
			slot->sync_state = 3;
		}
		break;
	case 14:
		if (gs->esb_pkt.subcmd == 4) {
			GLOBAL_STATE->onboarding->action = gs->esb_pkt.value;
		} else if (gs->esb_pkt.subcmd == 5) {
			request_not_disturb_bitmap(gs->esb_pkt.value);
		}
		break;
	case 15:
		if (GLOBAL_STATE->dash_buf->type == 1) {
			break;
		}
		reset_dash_buf(0);
		GLOBAL_STATE->dash_buf->type = 1;
		break;
	case 16:
		if (GLOBAL_STATE->dash_buf->type == 4) {
			GLOBAL_STATE->dash_buf->type = 5;
		}
		break;
	case 17:
		if (gs->esb_pkt.subcmd == 6) {
			struct GlassesState *g = GLOBAL_STATE;

			if (g->task_status->task == 0x14 && g->evenai_buf->state == 1 &&
			    g->evenai_buf->sub == 4 && g->evenai_buf->action_status == 0x13) {
				G1_LOG(1, "evenai_v2 need to show network_err tip\n");
			} else if (gs->esb_pkt.value < 3) {
				g->evenai_buf->sub = 6;
				g->evenai_buf->action_status = gs->esb_pkt.value;
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

/* IPC endpoint callback for "cpunet-esbs-ctrl" */
int esb_ctrl_recv(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	return 0;
}
