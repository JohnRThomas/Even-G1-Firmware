#ifndef G1_UNIT_PT_TEST_H_
#define G1_UNIT_PT_TEST_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

extern const struct device g1_pt_uart_dev;

/* bytes at offset of the RX buffer */
void pt_test_uart_receive(const uint8_t *bytes, size_t n, size_t offset);

uint8_t pt_test_sum8(const uint8_t *buf, size_t n);

#endif /* G1_UNIT_PT_TEST_H_ */
