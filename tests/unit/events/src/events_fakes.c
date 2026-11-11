#include <zephyr/kernel.h>

#include "harness.h"
#include "fakes_esb.h"
#include "events_fakes.h"

DEFINE_FAKE_VOID_FUNC(set_ncs_mem_notes_index, uint8_t);

#define PROXY_MSG_DATA_MAX 20

int g1_events_response_send_count;
static int responses_sent;

int send_response_data_to_ble(void)
{
	int last = g1_esb_response_log.count - 1;

	g1_events_response_send_count++;
	if (g1_state()->is_master == 2) {
		return 0;
	}
	if (last >= responses_sent) {
		g1_ble()->write(g1_esb_response_log.data[last],
				MIN(g1_esb_response_log.len[last], PROXY_MSG_DATA_MAX));
		responses_sent = g1_esb_response_log.count;
	}
	return 0;
}

void events_fakes_reset(void)
{
	RESET_FAKE(set_ncs_mem_notes_index);
	g1_events_response_send_count = 0;
	responses_sent = 0;
}
