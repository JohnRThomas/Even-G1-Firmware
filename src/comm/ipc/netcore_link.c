/*
 * Control channel to the network core over IPC ("cpuapp-hw-id" endpoint): RTC tick
 * synchronisation at boot, the runtime info (role, mode, ESB channel and
 * addresses) the network core asks for, MAC addresses and ESB counters it reports.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <hal/nrf_ipc.h>
#include "comm/ipc/netcore_link.h"
#include "comm/ipc/ipc_host.h"
#include "system/logging.h"
#include "main.h"
#include "system/system_state.h"

#define RTC_SYNC_WORD (*(volatile uint32_t *)0x20070000)

char net_build_time[32];
int esb_tx_count;
int esb_tx_fail_count;
int esb_rx_count;
int esb_signal;
int rtc_sync_timer_running = 1;
static bool first_sync = true;

static void timeout_handler(struct k_timer *timer);

static K_TIMER_DEFINE(rtc_sync_timer, timeout_handler, NULL);

/* IPC message to the network core carrying the runtime info */
struct ipc_runtime_msg {
	uint8_t type;
	uint8_t reserved;
	struct {
		uint8_t role;
		uint8_t mode;
		uint8_t esb_channel;
		uint8_t esb_master_addr;
		uint8_t esb_slave_addr;
	} info;
} __aligned(8);

enum {
	IPC_MSG_TICK_SYNC = 1,
	IPC_MSG_RUNTIME_INFO = 2,
	IPC_MSG_UPDATE_MACADDR = 3,
	IPC_MSG_SET_MACADDR = 4,
	IPC_MSG_BT_READY = 6,
	IPC_RESP_CPUNET_ESB_PACKAGES = 7,
	IPC_MSG_ESB_STATUS = 0x0d,
};

void rtc_sync_start(void)
{
	k_timer_start(&rtc_sync_timer, K_MSEC(50), K_MSEC(50));
}
int runtime_info_sync(struct GlassesState *gs)
{
	struct ipc_runtime_msg msg = {0};
	int ret;

	msg.type = 1;
	msg.info.role = gs->is_master;
	msg.info.mode = gs->work_mode;
	msg.info.esb_channel = gs->esb_channel;
	msg.info.esb_master_addr = gs->esb_master_addr;
	msg.info.esb_slave_addr = gs->esb_slave_addr;
	ret = global_ipc_service_send(&msg, sizeof(msg));
	if (first_sync) {
		first_sync = false;
		G1_LOG(1,
		       "--role:%d --mode:%d, esb_channel %d --ret:%d esb_master_addr %02X esb_slave_addr %02X\n",
		       msg.info.role, msg.info.mode, msg.info.esb_channel, ret, gs->esb_master_addr,
		       gs->esb_slave_addr);
	}
	return ret;
}

static void timeout_handler(struct k_timer *timer)
{
	uint32_t ticks = sys_clock_tick_get_32();

	if (RTC_SYNC_WORD == 0xa5a5a5a5) {
		G1_LOG(1, "RTC has sync at %lld ms\n", k_uptime_get());
	} else {
		RTC_SYNC_WORD = ticks;
		nrf_ipc_task_trigger(NRF_IPC, NRF_IPC_TASK_SEND_2);
		G1_LOG(1, "IPC send at %d ticks %lld ms\n", ticks, k_uptime_get());
		if (ticks <= 0x28000) {
			return;
		}
	}
	k_timer_stop(timer);
	rtc_sync_timer_running = 0;
}

int netcore_ctrl_recv(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	uint64_t uptime;

	switch (data[0]) {
	case IPC_MSG_TICK_SYNC:
		if (k_sem_count_get(&gs->ipc_sem) == 0) {
			k_sem_give(&gs->ipc_sem);
		}
		break;
	case IPC_MSG_RUNTIME_INFO:
		uptime = k_ticks_to_ms_ceil64(k_uptime_ticks());
		G1_LOG(1, "cpunet request send info uptime %lld\n", uptime);
		if (uptime > 10000) {
			G1_LOG(0, "sys reboot because recv cpunet sync package\n");
			system_reboot_delayed();
		}
		runtime_info_sync(gs);
		break;
	case IPC_MSG_UPDATE_MACADDR:
		uptime = k_ticks_to_ms_ceil64(k_uptime_ticks());
		if (GLOBAL_STATE->is_master == true) {
			memcpy(gs->master_mac, &data[1], sizeof(gs->master_mac));
		} else {
			memcpy(gs->slave_mac, &data[1], sizeof(gs->slave_mac));
		}
		G1_LOG(1,
		       "cpunet request update macaddr info uptime %lld bt macaddr esb_master_addr %02X esb_slave_addr %02X \n",
		       uptime, gs->esb_master_addr, gs->esb_slave_addr);
		if (k_sem_count_get(&gs->ipc_sem) == 0) {
			k_sem_give(&gs->ipc_sem);
		}
		break;
	case IPC_MSG_BT_READY:
		if (!gs->bt_state) {
			gs->bt_state = 1;
		}
		G1_LOG(1, "bt_ready\n");
		if (k_sem_count_get(&gs->ipc_sem) == 0) {
			k_sem_give(&gs->ipc_sem);
		}
		strncpy(net_build_time, &data[1], len - 1);
		net_build_time[sizeof(net_build_time) - 1] = 0;
		break;
	case IPC_RESP_CPUNET_ESB_PACKAGES:
		memcpy(&esb_tx_count, &data[1], 4);
		memcpy(&esb_tx_fail_count, &data[5], 4);
		memcpy(&esb_rx_count, &data[9], 4);
		memcpy(&esb_signal, &data[13], 4);
		if (!gs->check_mode) {
			esb_signal = 70;
		}
		G1_LOG(1, "IPC_RESP_CPUNET_ESB_PACKAGES %d %d %d %d\n", esb_tx_count,
		       esb_tx_fail_count, esb_rx_count, esb_signal);
		break;
	case IPC_MSG_SET_MACADDR:
		memcpy(gs->master_mac, &data[1], sizeof(gs->master_mac));
		memcpy(gs->slave_mac, &data[7], sizeof(gs->slave_mac));
		break;
	case IPC_MSG_ESB_STATUS:
		gs->sync_ack = data[1];
		break;
	default:
		G1_LOG(1, "sync fail! len: %d, data=%s\n\n", len, data);
		break;
	}
	return 0;
}
