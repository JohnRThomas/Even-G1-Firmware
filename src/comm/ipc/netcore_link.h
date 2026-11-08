/*
 * Control channel to the network core (netcore_link.c): RTC synchronisation,
 * runtime info, MAC addresses and the ESB counters the network core reports.
 */
#ifndef G1_COMM_IPC_NETCORE_LINK_H_
#define G1_COMM_IPC_NETCORE_LINK_H_

#include <stdint.h>
#include <stddef.h>
#include "system/glasses_state.h"

extern char net_build_time[32]; /* build time string reported by the network core */
extern int esb_tx_count;        /* ESB package counters reported by the network core */
extern int esb_tx_fail_count;
extern int esb_rx_count;
extern int esb_signal;
extern int rtc_sync_timer_running;

void rtc_sync_start(void);
int runtime_info_sync(struct GlassesState *gs);
/* receive callback of the "cpuapp-hw-id" IPC endpoint */
int netcore_ctrl_recv(struct GlassesState *gs, const uint8_t *data, size_t len);

#endif /* G1_COMM_IPC_NETCORE_LINK_H_ */
