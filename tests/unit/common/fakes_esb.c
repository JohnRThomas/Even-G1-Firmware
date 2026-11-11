#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes_esb.h"

#include "display/proxy_thread.h"
#include "main.h"

DEFINE_FAKE_VALUE_FUNC(int, SendDashBoardStartupModeInfoToSlave, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, SendDoubleTapCustomizeToSlave, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, SendLongpressTapCustomizeToSlave, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, SendSystemLanguageInfoToSlave, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, send_response_data_to_msgqueue, const void *, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_fake_ipc_send, const void *, size_t);

struct g1_pkt_log g1_esb_response_log;
struct g1_pkt_log g1_ipc_send_log;

__weak int esb_signal;
__weak int esb_rx_count;
__weak int esb_tx_count;
__weak int esb_tx_fail_count;

static int send_response_default(const void *data, uint32_t len)
{
	g1_pkt_log_add(&g1_esb_response_log, data, len);
	return 0;
}

static int ipc_send_default(const void *data, size_t len)
{
	g1_pkt_log_add(&g1_ipc_send_log, data, len);
	return 0;
}

static void fakes_esb_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(SendDashBoardStartupModeInfoToSlave);
	RESET_FAKE(SendDoubleTapCustomizeToSlave);
	RESET_FAKE(SendLongpressTapCustomizeToSlave);
	RESET_FAKE(SendSystemLanguageInfoToSlave);
	RESET_FAKE(send_response_data_to_msgqueue);
	RESET_FAKE(g1_fake_ipc_send);
	send_response_data_to_msgqueue_fake.custom_fake = send_response_default;
	g1_fake_ipc_send_fake.custom_fake = ipc_send_default;
	g1_pkt_log_clear(&g1_esb_response_log);
	g1_pkt_log_clear(&g1_ipc_send_log);

	esb_signal = 0;
	esb_rx_count = 0;
	esb_tx_count = 0;
	esb_tx_fail_count = 0;

	get_dashboard_state()->ipc_ctx.send = g1_fake_ipc_send;
}

G1_UNIT_RESET_HOOK(fakes_esb_reset);
