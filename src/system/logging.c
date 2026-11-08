/*
 * Log output.  g1_printk() is the sink of the G1_LOG_* macros: it prints to
 * the console, or, while BLE_DEBUG is set, formats the line into a small
 * message queue that the BLE work thread drains to the phone (ble_printk()
 * always takes the queue).  A line queued while the work thread is idle sets
 * debug_pending and wakes the thread so it can request the transfer.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "system/logging.h"
#include "comm/ble/ble.h"

int LOG_LEVEL = 2;
int BLE_DEBUG;
int debug_pending;
static struct k_msgq debug_msgq;
static uint8_t debug_buf[DEBUG_MSG_SIZE];

int debug_init(void)
{
	int ret;

	ret = k_msgq_alloc_init(&debug_msgq, DEBUG_MSG_SIZE, DEBUG_MSGQ_MAX);
	if (ret) {
		printk("debug init failed\r\n");
	} else {
		printk("debug init success\r\n");
	}
	return ret;
}

static int enqueue_debug(void *msg)
{
	int ret;

	if (debug_msgq.used_msgs == DEBUG_MSGQ_MAX) {
		/* drop the oldest message */
		k_msgq_get(&debug_msgq, debug_buf, K_NO_WAIT);
	}
	memcpy(debug_buf, msg, DEBUG_MSG_SIZE);
	ret = k_msgq_put(&debug_msgq, debug_buf, K_NO_WAIT);
	if (ret) {
		printk("enqueue_debug failed\r\n");
	}
	return ret;
}

int dequeue_debug(void *msg)
{
	uint8_t buf[DEBUG_MSG_SIZE];
	int ret;

	ret = k_msgq_get(&debug_msgq, buf, K_NO_WAIT);
	if (ret == 0) {
		memcpy(msg, buf, DEBUG_MSG_SIZE);
	}
	return ret;
}

/* not inlined, so buf stays off the stack of g1_printk()'s printk() path */
static __noinline void ble_vprintk(const char *fmt, va_list ap)
{
	struct ble_context *ctx = get_ble_context();
	char buf[DEBUG_MSG_SIZE];

	memset(buf, 0, sizeof(buf));
	vsnprintf(buf, sizeof(buf), fmt, ap);
	enqueue_debug(buf);

	if (!ctx->receiver_thd_is_running && k_sem_count_get(&ctx->rx_sem) == 0) {
		debug_pending = 1;
		k_sem_give(&ctx->rx_sem);
	}
}

void ble_printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ble_vprintk(fmt, ap);
	va_end(ap);
}

void g1_printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (BLE_DEBUG) {
		ble_vprintk(fmt, ap);
	} else {
		vprintk(fmt, ap);
	}
	va_end(ap);
}

void dump_hex(const char *tag, const uint8_t *data, int len)
{
	printk("dump_hex(%s):", tag);
	for (int i = 0; i < len; i++) {
		if ((i & 7) == 0) {
			printk("\n");
		}
		printk("0x%02x,", data[i]);
	}
	printk("\n");
}
