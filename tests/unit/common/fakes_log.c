#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fakes_log.h"
#include "g1_log.h"

__weak int BLE_DEBUG;
struct g1_pkt_log g1_fake_ble_printk_log;

/* 200 = DEBUG_MSG_SIZE */
__weak void ble_printk(const char *fmt, ...)
{
	char buf[200];
	va_list ap;

	va_start(ap, fmt);
	memset(buf, 0, sizeof(buf));
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g1_pkt_log_add(&g1_fake_ble_printk_log, buf, strlen(buf) + 1);
}

static void fakes_log_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	g1_pkt_log_clear(&g1_fake_ble_printk_log);
}

G1_UNIT_RESET_HOOK(fakes_log_reset);
