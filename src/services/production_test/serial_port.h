/*
 * Declarations of serial_port.c.
 */
#ifndef G1_SERVICES_PT_SERIAL_PORT_H_
#define G1_SERVICES_PT_SERIAL_PORT_H_

#include "services/production_test/production_test.h"

void serial_rx_clear(void);
int lpuart_async_a(const struct device *dev);
int lpuart_async_b(const struct device *dev, uint8_t *data, size_t len);
int read_serial_data(const struct device *dev, uint8_t **data, uint32_t *len);

#endif /* G1_SERVICES_PT_SERIAL_PORT_H_ */
