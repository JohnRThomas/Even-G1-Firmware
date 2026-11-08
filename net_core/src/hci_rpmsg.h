/*
 * HCI transport between the Bluetooth controller and the application core.
 */
#ifndef NET_HCI_RPMSG_H_
#define NET_HCI_RPMSG_H_

#include <stdint.h>

int hci_rpmsg_init(void);
int hci_rpmsg_get_static_addr(uint8_t *addr);

#endif /* NET_HCI_RPMSG_H_ */
