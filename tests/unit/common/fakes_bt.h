/* Bluetooth host and BLE glue fakes. */
#ifndef G1_UNIT_FAKES_BT_H_
#define G1_UNIT_FAKES_BT_H_

#include <stdint.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/addr.h>
#include <glasses_state.h>

struct bt_conn;
struct bt_data_msg;

/* never dereferenced */
#define G1_FAKE_CONN ((struct bt_conn *)&g1_fake_conn_storage)
extern uint32_t g1_fake_conn_storage;

DECLARE_FAKE_VALUE_FUNC(int, bt_unpair, uint8_t, const bt_addr_le_t *);
DECLARE_FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);

DECLARE_FAKE_VALUE_FUNC(struct bt_conn *, get_current_conn);
DECLARE_FAKE_VALUE_FUNC(int, bt_start);
DECLARE_FAKE_VALUE_FUNC(int, stop_ble_advertising);
DECLARE_FAKE_VALUE_FUNC(int, ancs_main, struct ble_context *);
DECLARE_FAKE_VALUE_FUNC(int, set_ancs_enable, int);
DECLARE_FAKE_VOID_FUNC(ancs_delete_notification, uint32_t);
extern uint8_t multi_packet_buf[];

DECLARE_FAKE_VALUE_FUNC(int, dequeue_bt_data, struct bt_data_msg *);

#endif /* G1_UNIT_FAKES_BT_H_ */
