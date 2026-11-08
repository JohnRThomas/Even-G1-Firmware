/*
 * Production-test microphone transfer: the thread that sends the recorded
 * microphone data from external flash to the factory tool in chunks.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/printk.h>
#include "services/production_test/production_test.h"
#include "services/production_test/pt_mic.h"
#include "services/production_test/pt_internal.h"
#include "system/logging.h"
#include "main.h"

struct k_sem mic_transm_sem; /* mic_transm_thread trigger */
static struct k_thread mic_transm_thread_data;
static K_THREAD_STACK_DEFINE(mic_transm_thread_stack, 1024);
void mic_transm_thread(void *p1, void *p2, void *p3)
{
	const struct device *flash_dev = DEVICE_DT_GET(DT_NODELABEL(mx25r64));
	uint8_t buf[MIC_TRANSM_FRAME] = {0};
	uint8_t fail = 0;
	uint16_t seq;
	uint32_t addr;
	int err;
	int cnt;
	int i;

	k_sem_init(&mic_transm_sem, 0, 1);

	while (1) {
		k_sem_take(&mic_transm_sem, K_FOREVER);
		buf[0] = 0x5a;
		buf[1] = 0xa5;
		buf[2] = 0xf1;
		buf[3] = 0x01;

		if (!device_is_ready(flash_dev)) {
			G1_LOG_ERR(" [%s] device not ready.\n", flash_dev->name);
			buf[4] = 0;
			buf[5] = 0xca;
			get_dashboard_state()->ble_context.write(buf, 10);
		} else if (get_dashboard_state()->flash_read != 0) {
			seq = 0;
			while (1) {
				addr = mic_flash_addr + seq * MIC_TRANSM_CHUNK;
				printk("start send mic data,flash address:0x%x,seq:%d\n", addr,
				       seq);
				err = flash_read(flash_dev, addr, buf + 6, MIC_TRANSM_CHUNK);
				if (err) {
					printk("Flash read failed! %d\n", err);
					buf[4] = seq;
					buf[5] = 0xca;
					get_dashboard_state()->ble_context.write(buf, 10);
					break;
				}
				buf[4] = seq;
				buf[5] = 0;
				cnt = 0;
				for (i = 6; i < 26; i++) {
					if (buf[i] == 0xff) {
						cnt++;
					}
				}
				if (cnt == 20 || addr > 0x410000) {
					buf[4] = 0;
					buf[5] = 1;
					get_dashboard_state()->ble_context.write(buf, 10);
					break;
				}
				if (get_dashboard_state()->ble_context.write(
					    buf, MIC_TRANSM_FRAME) < 0) {
					fail++;
					if (fail >= 10) {
						G1_LOG_ERR("BLE send failed for %d times\n", fail);
						fail = 0;
						break;
					}
				} else {
					seq++;
				}
				k_msleep(20);
			}
		}
		memset(buf, 0, sizeof(buf));
	}
}

void start_mic_transm_thread(void)
{
	k_thread_create(&mic_transm_thread_data, mic_transm_thread_stack,
			K_THREAD_STACK_SIZEOF(mic_transm_thread_stack), mic_transm_thread, NULL,
			NULL, NULL, -12, 0, K_NO_WAIT);
}
