/*
 * HCI command with a bounded wait for its completion.
 *
 * Zephyr's bt_hci_cmd_send_sync() waits 10 s for the controller and then
 * asserts.  hci_cmd_send_timeout() sends the command with bt_hci_cmd_send()
 * instead and asks the host to set a flag when the command completes
 * successfully (bt_hci_cmd_state_set_init()); it polls that flag for at most
 * timeout_ms.  A controller that stops answering therefore shows up as
 * -ETIMEDOUT, and the caller decides what to do about it.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/hci.h>
#include <../subsys/bluetooth/host/hci_core.h>

#include "g1_log.h"
#include "hci_cmd_timeout.h"

#define POLL_INTERVAL_MS 10

/* The host keeps a pointer to the state until the command completes, which may
 * be after the wait has timed out, so neither lives on the caller's stack. */
static struct bt_hci_cmd_state_set cmd_state;
static atomic_t cmd_done;

int hci_cmd_send_timeout(uint16_t opcode, int32_t timeout_ms)
{
	struct net_buf *buf;
	int32_t waited;
	int err;

	buf = bt_hci_cmd_create(opcode, 0);
	if (!buf) {
		return -ENOBUFS;
	}

	atomic_clear_bit(&cmd_done, 0);
	bt_hci_cmd_state_set_init(buf, &cmd_state, &cmd_done, 0, true);

	err = bt_hci_cmd_send(opcode, buf);
	if (err) {
		return err;
	}

	for (waited = 0; !atomic_test_bit(&cmd_done, 0); waited += POLL_INTERVAL_MS) {
		if (waited >= timeout_ms) {
			G1_LOG_ERR("command opcode 0x%04x timeout after %d ms\n", opcode,
				   timeout_ms);
			return -ETIMEDOUT;
		}
		k_msleep(POLL_INTERVAL_MS);
	}

	return 0;
}
