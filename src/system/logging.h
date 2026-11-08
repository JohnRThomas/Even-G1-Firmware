/*
 * Logging helpers.
 *
 * Every log line is "%s(): <msg>" with __func__, gated on the runtime
 * LOG_LEVEL at the call site and passed to g1_printk(), which routes it to
 * ble_printk() when BLE_DEBUG is set, otherwise to printk().  ble_printk()
 * queues the line for the BLE work thread, which sends the queue to the
 * phone (logging.c); LOG_LEVEL itself is defined in main.c.
 */
#ifndef G1_LOG_H_
#define G1_LOG_H_

#include <zephyr/sys/printk.h>

extern int LOG_LEVEL;
extern int BLE_DEBUG;

/* debug log message queue (ble_printk -> phone) */
#define DEBUG_MSG_SIZE 200
#define DEBUG_MSGQ_MAX 20

/* set when a line was queued while the BLE work thread was idle; the thread
 * clears it once it has requested the transfer to the phone */
extern int debug_pending;

int debug_init(void);
int dequeue_debug(void *msg);

void ble_printk(const char *fmt, ...);

/* ble_printk() when BLE_DEBUG is set, otherwise printk() */
void g1_printk(const char *fmt, ...) __printf_like(1, 2);
/* hex dump of a buffer, 16 bytes per line */
void dump_hex(const char *tag, const uint8_t *data, int len);

#define G1_LOG(lvl, fmt, ...)                                                                      \
	do {                                                                                       \
		if (LOG_LEVEL > (lvl)) {                                                           \
			g1_printk("%s(): " fmt, __func__, ##__VA_ARGS__);                          \
		}                                                                                  \
	} while (0)

#define G1_LOG_ERR(fmt, ...) G1_LOG(0, fmt, ##__VA_ARGS__)
#define G1_LOG_WRN(fmt, ...) G1_LOG(1, fmt, ##__VA_ARGS__)
#define G1_LOG_INF(fmt, ...) G1_LOG(2, fmt, ##__VA_ARGS__)
#define G1_LOG_DBG(fmt, ...) G1_LOG(3, fmt, ##__VA_ARGS__)

/* log line without the LOG_LEVEL check and without the "%s(): " prefix */
#define BLE_PRINTK(fmt, ...) g1_printk(fmt, ##__VA_ARGS__)

#endif /* G1_LOG_H_ */
