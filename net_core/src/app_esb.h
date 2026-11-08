/*
 * ESB link between the two glasses: event callback, pipe addresses, link
 * state and the timeslot start/stop hook.
 */
#ifndef NET_APP_ESB_H_
#define NET_APP_ESB_H_

#include <stdint.h>

#include "net_common.h"

/* Event passed to the callback given to app_esb_init(). */
struct app_esb_evt {
	uint8_t type;           /* 0 = TX success, 1 = TX failed, 2 = RX received */
	uint8_t *data;
	uint32_t len;
	int8_t rssi;
};

typedef void (*app_esb_cb_t)(struct app_esb_evt *evt);

extern uint8_t base_addr_0[4];
extern uint8_t base_addr_1[4];
extern uint8_t addr_prefix[2];

void esb_ops_init(struct esb_ops *ops);
int app_esb_init(uint8_t prx, app_esb_cb_t cb);
int on_timeslot_start_stop(int op);

void esb_link_set_high(void);
void esb_link_set_low(void);
void esb_link_set_paused(void);
int esb_link_state_get(void);
int esb_tx_busy_get(void);

/* Radio ISR of the ESB library with CONFIG_ESB_DYNAMIC_INTERRUPTS; esb.h does not declare it. */
void RADIO_IRQHandler(const void *args);

#endif /* NET_APP_ESB_H_ */
