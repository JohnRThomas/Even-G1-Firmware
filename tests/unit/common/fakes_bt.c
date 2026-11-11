/* Bluetooth host and BLE glue fakes. */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include "ble/ble_ancs.h"

uint32_t g1_fake_conn_storage;

DEFINE_FAKE_VALUE_FUNC(int, bt_unpair, uint8_t, const bt_addr_le_t *);
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);

DEFINE_FAKE_VALUE_FUNC(struct bt_conn *, get_current_conn);
FAKE_VALUE_FUNC(int, set_ancs_enable, int);
DEFINE_FAKE_VOID_FUNC(ancs_delete_notification, uint32_t);
__weak uint8_t multi_packet_buf[MULTI_PACKET_BUF_SIZE];

static void fakes_bt_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(bt_unpair);
  RESET_FAKE(bt_conn_disconnect);
  RESET_FAKE(get_current_conn);
  RESET_FAKE(ancs_delete_notification);
  memset(multi_packet_buf, 0, MULTI_PACKET_BUF_SIZE);
}

G1_UNIT_RESET_HOOK(fakes_bt_reset);
