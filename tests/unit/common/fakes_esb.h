/* Proxy thread, ESB counter and IPC send fakes; packets are logged. */
#ifndef G1_UNIT_FAKES_ESB_H_
#define G1_UNIT_FAKES_ESB_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include "harness.h"

DECLARE_FAKE_VALUE_FUNC(int, SendDashBoardStartupModeInfoToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendDoubleTapCustomizeToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendLongpressTapCustomizeToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, SendSystemLanguageInfoToSlave, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, send_response_data_to_msgqueue, const void *, uint32_t);
extern struct g1_pkt_log g1_esb_response_log;

/* GlassesState->ipc_ctx.send */
DECLARE_FAKE_VALUE_FUNC(int, g1_fake_ipc_send, const void *, size_t);
extern struct g1_pkt_log g1_ipc_send_log;

extern int esb_signal;
extern int esb_rx_count;
extern int esb_tx_count;
extern int esb_tx_fail_count;

#endif /* G1_UNIT_FAKES_ESB_H_ */
