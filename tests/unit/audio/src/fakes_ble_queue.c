#include <zephyr/kernel.h>

#include "fakes_ble_queue.h"

#include "ble/ble_ancs.h"

struct g1_pkt_log audio_bt_queue_log;

__weak int enqueue_bt_data(void *data, uint8_t len)
{
	g1_pkt_log_add(&audio_bt_queue_log, data, len);
	return 0;
}

static void fakes_ble_queue_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	g1_pkt_log_clear(&audio_bt_queue_log);
}

G1_UNIT_RESET_HOOK(fakes_ble_queue_reset);
