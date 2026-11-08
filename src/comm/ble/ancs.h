/*
 * ANCS client (ancs.c): discovery of the GATT service and the Apple
 * Notification Center Service on the connected phone, the notification
 * attribute requests on their own thread, and the switch and deletion
 * requests other modules make.  The link code calls the init and discovery
 * entry points; everything else about the client is private to ancs.c.
 */
#ifndef G1_BLE_ANCS_H_
#define G1_BLE_ANCS_H_

#include <stdint.h>
#include "system/glasses_state.h"

struct bt_conn;

/* called by ble_link_start() */
int ancs_c_init(struct notif_msg *data);
int gattp_init(void);
void start_ancs_work_thread(struct ble_context *ctx);

/* called by the connection callbacks once the link is encrypted / dropped */
void reset_ancs_discovery_flags(void);
void discover_gattp(struct bt_conn *conn);
void clear_ancs_discovery_flags(void);

/* enables (discovers) or disables the client for the bonded phone */
int set_ancs_enable(int enable);
/* queues a "notification removed" event for the given notification uid */
void ancs_delete_notification(uint32_t uid);

#endif /* G1_BLE_ANCS_H_ */
