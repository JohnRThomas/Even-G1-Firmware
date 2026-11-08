/*
 * Production-test lpuart bring-up (init_serial_port / init_serial_comm).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

#include "system/logging.h"
#include "services/production_test/production_test.h"
#include "services/production_test/serial_thread.h"
#include "services/production_test/pt_data.h"
#include "services/production_test/serial_port.h"

static uint8_t lpuart_ready;

static int init_serial_port(void)
{
	const struct device *lpuart = DEVICE_DT_GET(DT_NODELABEL(uart0));

	if (lpuart_ready) {
		return 0;
	}

	__ASSERT(device_is_ready(lpuart), "LPUART device not ready");

	if (lpuart_async_a(lpuart)) {
		lpuart_ready = 0;
		printk("init_serial_port is failed\n");
		return -1;
	}
	lpuart_ready = 1;

	return 0;
}

int init_serial_comm(void)
{
	if (init_serial_port()) {
		return -1;
	}

	if (setUARTDevice(DEVICE_DT_GET(DT_NODELABEL(uart0)))) {
		printk("init_serial_comm is failed\n");
		k_sleep(K_MSEC(500));
		return -2;
	}

	return process_pt_data(NULL) ? -3 : 0;
}
