/*
 * NFC event thread: waits for the tag's interrupt line, reads the interrupt status
 * and maintains the "in the case" state from the presence of the case's field.
 */
#include <zephyr/kernel.h>
#include "services/nfc_wlc/nfc_thread.h"
#include "drivers/nfc/st25dv.h"
#include "system/low_power.h"
#include "services/production_test/production_test.h"
#include "services/power/charger_box.h"
#include "services/production_test/pt_data.h"
#include "services/production_test/serial_thread.h"

static uint8_t nfc_sem_ready;
static struct k_sem nfc_msg_sem;
static int64_t nfc_deadline;

static K_THREAD_STACK_DEFINE(nfc_stack, 0xc00);
static struct k_thread nfc_thread_data;
static bool nfc_timeout(uint32_t ms)
{
	uint64_t elapsed = k_uptime_get() - nfc_deadline;

	return (uint64_t)nfc_deadline < (uint64_t)k_uptime_get() && elapsed > ms;
}

static void nfc_thread(void *p1, void *p2, void *p3)
{
	uint8_t it_status;
	int retry = 0;

	if (k_sem_init(&nfc_msg_sem, 0, 10) == 0) {
		nfc_sem_ready = 1;
	}
	printk("[csh_debug_nfc] g_nfcMsgSemInitOk is %d \n", nfc_sem_ready);
	for (;;) {
		if (get_dashboard_state()->work_mode == 8) {
			nfc_deadline_set(10000);
			set_pt_deadline();
			k_sleep(K_MSEC(50));
		} else if (get_dashboard_state()->work_mode == 1) {
			k_sleep(K_MSEC(5000));
		} else {
			if (get_pt_test_mode() == 1) {
				if (init_nfc_comm() == 0) {
					set_box_status(BOX_IN_BOX, 1);
					nfc_deadline_set(0);
				} else if (nfc_timeout(5000)) {
					set_box_status(BOX_IN_BOX, 0);
				}
				if (get_dashboard_state()->comflag == 1) {
					init_serial_comm();
				}
				k_sleep(K_MSEC(150));
			} else {
				bool again;

				do {
					again = false;
					if (k_sem_take(&nfc_msg_sem, K_MSEC(3000)) == 0) {
						it_status = 0;
						if ((uint64_t)nfc_deadline <
						    (uint64_t)k_uptime_get()) {
							nfc_deadline_set(0);
						}
						st25dv_read_it_status(&it_status);
						if (it_status & BIT(5)) {
							init_nfc_comm();
						} else if (++retry <= 9) {
							again = true;
							continue;
						}
						set_box_status(BOX_IN_BOX, 1);
						retry = 0;
					} else if ((!can_enter_low_power &&
						    get_dashboard_state()->work_mode != 8) &&
						   nfc_timeout(3000)) {
						set_box_status(BOX_IN_BOX, 0);
						set_box_status(BOX_LID_CLOSED, 0);
						can_enter_low_power = 0;
					}

				} while (again);
			}
		}
	}
}

void trigger_nfc_event(void)
{
	if (nfc_sem_ready) {
		k_sem_give(&nfc_msg_sem);
	}
}

/* returns the new deadline */
int64_t nfc_deadline_set(uint32_t ms)
{
	nfc_deadline = k_uptime_get() + ms;
	return nfc_deadline;
}

void nfc_thread_start(void)
{
	k_thread_create(&nfc_thread_data, nfc_stack, K_THREAD_STACK_SIZEOF(nfc_stack), nfc_thread,
			NULL, NULL, NULL, -9, 0, K_NO_WAIT);
}
