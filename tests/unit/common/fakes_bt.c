#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <string.h>

#include "fakes_bt.h"
#include "harness.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include "ble/ble_ancs.h"

uint32_t g1_fake_conn_storage;

DEFINE_FAKE_VALUE_FUNC(int, bt_unpair, uint8_t, const bt_addr_le_t *);
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);

DEFINE_FAKE_VALUE_FUNC(struct bt_conn *, get_current_conn);
DEFINE_FAKE_VALUE_FUNC(int, bt_start);
DEFINE_FAKE_VALUE_FUNC(int, stop_ble_advertising);
DEFINE_FAKE_VALUE_FUNC(int, ancs_main, struct ble_context *);
DEFINE_FAKE_VALUE_FUNC(int, set_ancs_enable, int);
DEFINE_FAKE_VOID_FUNC(ancs_delete_notification, uint32_t);
__weak uint8_t multi_packet_buf[MULTI_PACKET_BUF_SIZE];

DEFINE_FAKE_VALUE_FUNC(int, dequeue_bt_data, struct bt_data_msg *);

static void fakes_bt_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(bt_unpair);
	RESET_FAKE(bt_conn_disconnect);
	RESET_FAKE(get_current_conn);
	RESET_FAKE(bt_start);
	RESET_FAKE(stop_ble_advertising);
	RESET_FAKE(ancs_main);
	RESET_FAKE(set_ancs_enable);
	RESET_FAKE(ancs_delete_notification);
	memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
	RESET_FAKE(dequeue_bt_data);
	dequeue_bt_data_fake.return_val = -ENOMSG;
}

G1_UNIT_RESET_HOOK(fakes_bt_reset);
