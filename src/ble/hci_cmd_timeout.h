/*
 * HCI command with a bounded wait for its completion.
 */
#ifndef BLE_HCI_CMD_TIMEOUT_H_
#define BLE_HCI_CMD_TIMEOUT_H_

#include <stdint.h>

/*
 * Sends the parameterless HCI command `opcode` and waits up to timeout_ms for it
 * to complete successfully.  The response parameters are dropped.
 *
 * Returns 0 on success, -ETIMEDOUT when no successful completion arrives in
 * time (a completion with an error status counts as none), or the error of
 * bt_hci_cmd_send().  One caller at a time: the completion flag is shared.
 */
int hci_cmd_send_timeout(uint16_t opcode, int32_t timeout_ms);

#endif /* BLE_HCI_CMD_TIMEOUT_H_ */
