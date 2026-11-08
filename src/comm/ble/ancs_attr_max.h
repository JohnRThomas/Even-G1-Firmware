/*
 * Raises the largest iOS notification attribute the NCS ANCS client accepts.
 *
 * The SDK header fixes BT_ANCS_ATTR_DATA_MAX at 32 bytes, and
 * bt_ancs_register_attr() rejects a larger buffer with -EINVAL.  This firmware
 * registers a 256-byte buffer for the notification message, so with the stock
 * value ANCS initialisation fails and the firmware reboots.
 *
 * Include this header instead of <bluetooth/services/ancs_client.h>.  The top
 * level CMakeLists.txt also force-includes it in front of the SDK's
 * ancs_client.c, which holds that length check.  The SDK itself is not modified.
 *
 * The SDK header is read with its own value first, so the one structure member
 * it sizes with this constant (the app identifier kept while an app attribute
 * response is parsed) stays 32 bytes in the library and here alike.  That is
 * enough: the firmware asks for app attributes with the identifier from its own
 * 32-byte buffer.
 */
#ifndef BLE_ANCS_ATTR_MAX_H_
#define BLE_ANCS_ATTR_MAX_H_

#include <bluetooth/services/ancs_client.h>

#undef BT_ANCS_ATTR_DATA_MAX
#define BT_ANCS_ATTR_DATA_MAX 256

#endif /* BLE_ANCS_ATTR_MAX_H_ */
