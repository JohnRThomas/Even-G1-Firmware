/*
 * HCI transport between the Bluetooth controller and the application core.
 */
#ifndef NET_HCI_RPMSG_H_
#define NET_HCI_RPMSG_H_

#include <stdint.h>

int hci_rpmsg_init(void);
int hci_rpmsg_get_static_addr(uint8_t *addr);

/* Controller assertion hook (CONFIG_BT_CTLR_ASSERT_HANDLER), called by the Bluetooth controller. */
void bt_ctlr_assert_handle(char *file, uint32_t line);

#endif /* NET_HCI_RPMSG_H_ */
