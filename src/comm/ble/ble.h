/*
 * Bluetooth link of the glasses: stack bring-up and advertising, the
 * connection with the phone (pairing, bonding, MTU) and the NUS service that
 * carries the application protocol, with the packet queue between the NUS
 * receive callback and the BLE work thread.  Implemented in ble_conn.c and
 * nus.c; the ANCS client has its own header, ancs.h.
 */
#ifndef G1_BLE_H_
#define G1_BLE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/addr.h>
#include "system/glasses_state.h"

#define BT_DATA_MSGQ_MAX 96

/* One queued NUS packet (enqueue_bt_data / dequeue_bt_data) */
struct bt_data_msg {
	uint8_t len;
	uint8_t data[256];
};

/* nus.c */
int enqueue_bt_data(void *data, uint8_t len);
int dequeue_bt_data(struct bt_data_msg *out);
int init_nus(void);
struct ble_context *get_ble_context(void);
void ble_context_init(struct ble_context *ctx);

/* ble_conn.c */
struct bt_conn;
struct bt_conn *get_current_conn(void);
/* enables the stack, registers the pairing callbacks, initialises the ANCS
 * client and the NUS service and starts advertising */
int ble_link_start(struct ble_context *ctx);
int bt_start(void);
int stop_ble_advertising(void);
bool is_ble_device_bound(void);

/* link buffers */
#define BLE_TX_BUF_SIZE    0x2b8
#define BLE_SMALL_PKT_MAX  20
#define BLE_SMALL_BUF_SIZE (BLE_SMALL_PKT_MAX + 1)

#endif /* G1_BLE_H_ */
