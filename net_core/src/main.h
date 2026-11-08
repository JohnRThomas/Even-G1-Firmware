/*
 * Declarations of net_core/src/main.c: network core role/mode, the ESB TX
 * buffer and RX queue shared with the ESB thread, and status reports to the
 * application core.
 */
#ifndef NET_MAIN_H_
#define NET_MAIN_H_

#include <stddef.h>
#include <stdint.h>

#define ESB_TX_BUF_SIZE 0xfc

extern uint8_t esb_tx_buf[ESB_TX_BUF_SIZE];
extern uint8_t esb_channel;
extern uint8_t esb_tx_kind;
extern uint8_t esb_tx_state;
extern uint8_t esb_tx_done;
extern uint8_t esb_evt_active;

int net_mode_get(void);
int net_role_get(void);

int esb_tx_buf_set(const uint8_t *data, size_t len);
size_t esb_tx_buf_len_get(void);
void esb_tx_buf_clear(void);

int esb_rx_msgq_init(void);
int esb_rx_msgq_get(uint8_t *data);

/* Reports the ESB link state to the application core. */
void ipc_send_esb_status(uint8_t val);

#endif /* NET_MAIN_H_ */
