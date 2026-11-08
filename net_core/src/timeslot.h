/*
 * MPSL timeslot session that gives the ESB link radio time.
 */
#ifndef NET_TIMESLOT_H_
#define NET_TIMESLOT_H_

#include <stdint.h>

/* cb(0) is called when a radio timeslot starts and cb(1) when it ends. */
void timeslot_handler_init(int (*cb)(int op));
void timeslot_handler_stop(void);
uint8_t timeslot_radio_is_on(void);

#endif /* NET_TIMESLOT_H_ */
