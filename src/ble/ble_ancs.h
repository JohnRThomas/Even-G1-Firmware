/*
 * BLE (NUS + ANCS client) module - private types and prototypes.
 */
#ifndef G1_BLE_ANCS_H_
#define G1_BLE_ANCS_H_

#include <glasses_state.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

#define BT_DATA_MSGQ_MAX 96

/* One queued NUS packet (enqueue_bt_data / dequeue_bt_data) */
struct bt_data_msg {
  uint8_t len;
  uint8_t data[256];
};

/* ble_nus.c */
extern struct k_msgq nus_data_msgq;
int enqueue_bt_data(void *data, uint8_t len);
int dequeue_bt_data(struct bt_data_msg *out);
int init_nus(void);
struct ble_context *get_ble_context(void);
void ble_context_init(struct ble_context *ctx);

/* ble_ancs.c */
struct bt_conn;
struct bt_conn *get_current_conn(void);
int bt_start(void);
int ancs_main(struct ble_context *ctx);
void ancs_delete_notification(uint32_t uid);
int set_ancs_enable(int enable);
int stop_ble_advertising(void);
bool is_ble_device_bound(void);

/* bt_conn_security.c: the linker's --wrap replacements of the Zephyr host's functions */
struct bt_conn *__real_bt_conn_add_le(uint8_t id, const bt_addr_le_t *peer);
struct bt_conn *__wrap_bt_conn_add_le(uint8_t id, const bt_addr_le_t *peer);
int __wrap_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec);

#endif /* G1_BLE_ANCS_H_ */
