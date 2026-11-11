/* The lpuart of serial_port.c, faked in test_serial.c; test_pt_data.c talks over it too. */
#ifndef G1_UNIT_PT_TEST_H_
#define G1_UNIT_PT_TEST_H_

#include <zephyr/fff.h>

#include "harness.h"

extern struct device g1_pt_uart_dev;

/* uart_tx() of serial_port.c: it and uart_poll_out() log the bytes sent in g1_pt_uart_tx_log */
DECLARE_FAKE_VALUE_FUNC(int, g1_pt_uart_tx, const struct device *, const uint8_t *, size_t,
      int32_t);
extern struct g1_pkt_log g1_pt_uart_tx_log;

/* the bytes of hex arrive at offset of the RX buffer */
void pt_test_uart_receive(const char *hex, size_t offset);

#endif /* G1_UNIT_PT_TEST_H_ */
