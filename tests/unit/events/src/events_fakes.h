#ifndef EVENTS_FAKES_H_
#define EVENTS_FAKES_H_

#include <stdint.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VOID_FUNC(set_ncs_mem_notes_index, uint8_t);

int send_response_data_to_ble(void);
extern int g1_events_response_send_count;

void events_fakes_reset(void);

#endif /* EVENTS_FAKES_H_ */
