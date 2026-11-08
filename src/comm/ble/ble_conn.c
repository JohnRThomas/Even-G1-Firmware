/*
 * Bluetooth link with the phone: stack bring-up, advertising with the
 * per-side device name, the connection callbacks, pairing and bonding
 * against the stored peer address, MTU tracking and the link state the rest
 * of the firmware asks about.  Once the link is encrypted the ANCS client
 * (ancs.c) is told to start its service discovery.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/settings/settings.h>
#include <../subsys/bluetooth/host/conn_internal.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/ancs.h"
#include "main.h"
#include "services/notifications/notify.h"
#include "services/production_test/production_test.h"
#include "system/system_state.h"
#include "system/work_mode.h"
#include "services/storage/settings_records.h"

#define DEVICE_NAME     "Even G1"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* current connection */
static struct bt_conn *current_conn;
static char device_name[32];
static uint8_t manuf_data[20];
static uint8_t bt_start_busy;

static struct ble_context *link_ctx;

static int security_fail_count;

static struct bt_le_conn_param conn_param = BT_LE_CONN_PARAM_INIT(12, 12, 0, 800);

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	{0},
};

static struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
	if (tx >= 100) {
		k_sem_give(&link_ctx->tx_sem);
		if (tx == 498) {
			change_work_mode(1);
		}
	}
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	printk("bond_deleted conn: %s\n", addr);
	link_ctx->security_pending = 0;
}

static void check_bonded_addr(const struct bt_bond_info *info, void *user_data)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(&info->addr, addr, sizeof(addr));
	link_ctx->security_pending = 0;
	G1_LOG_ERR("Got %s bind addr\n\n", addr);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
	G1_LOG_ERR("bt_conn_disconnect because Pairing failed\n");
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing cancelled: %s\n", addr);
	G1_LOG_ERR("bt_conn_disconnect because Pairing cancelled\n");
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void ble_security_changed(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		G1_LOG_INF("%s level %u\n\n", addr, level);
		if (bt_conn_get_security(conn) >= BT_SECURITY_L2) {
			const bt_addr_le_t *dst;

			reset_ancs_discovery_flags();
			dst = bt_conn_get_dst(conn);
			if (!get_pt_test_mode() && get_dashboard_state()->bonded == 1 &&
			    !memcmp(&get_dashboard_state()->bonded_addr, dst,
				    sizeof(bt_addr_le_t))) {
				discover_gattp(conn);
			}
		}
		link_ctx->security_pending = 0;
		security_fail_count = 0;
	} else {
		G1_LOG_INF("Security failed: %s level %u err %d\n\n", addr, level, err);
		security_fail_count++;
		if (security_fail_count >= 10) {
			G1_LOG_ERR(
				"reboot because Security failed exceed 10 times, so reboot\r\n\n");
			system_reboot_delayed();
		}
		bt_conn_set_security(conn, BT_SECURITY_L2);
	}
}

static void ble_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	bt_addr_le_to_str(dst, addr, sizeof(addr));
	printk("%s -- type: 0x%x, role:0x%x force_bind %d", addr, conn->type, conn->role,
	       get_dashboard_state()->force_bind);
	if (err) {
		G1_LOG_ERR("failed (err 0x%02x)\n\n", err);
		return;
	}

	if (!get_pt_test_mode() && !get_dashboard_state()->force_bind &&
	    memcmp(&get_dashboard_state()->bonded_addr, dst, sizeof(bt_addr_le_t)) != 0) {
		G1_LOG_ERR("disconnect because invalid mac %s -- type: 0x%x, role:0x%x \n\n", addr,
			   conn->type, conn->role);
		bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
		return;
	}

	bt_conn_le_param_update(conn, &conn_param);
	get_dashboard_state()->ble_context.last_rx_time = k_uptime_get_32();
	get_dashboard_state()->ble_context.connected_time = k_uptime_get_32();
	link_ctx->connected = 1;
	link_ctx->security_pending = 1;
	current_conn = conn;
}

static void on_pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	bt_addr_le_to_str(dst, addr, sizeof(addr));
	if (get_dashboard_state()->force_bind) {
		get_dashboard_state()->force_bind = 0;
	}
	if (!get_pt_test_mode() &&
	    memcmp(&get_dashboard_state()->bonded_addr, dst, sizeof(bt_addr_le_t)) != 0) {
		bt_unpair(BT_ID_DEFAULT, &get_dashboard_state()->bonded_addr);
		get_dashboard_state()->bonded = 0;
		get_dashboard_state()->dashboard_cmd = 0;
		k_sem_give(&get_dashboard_state()->dashboard_position_sem);
	}
	bt_addr_le_copy(&get_dashboard_state()->bonded_addr, dst);
	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
	link_ctx->security_pending = 0;
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = on_pairing_complete,
	.pairing_failed = pairing_failed,
	.bond_deleted = bond_deleted,
};

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	link_ctx->connected = 0;
	current_conn = NULL;
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	G1_LOG_ERR("from %s (reason 0x%02x)\n", addr, reason);
	link_ctx->security_pending = 0;
	while (k_sem_count_get(&get_dashboard_state()->ble_context.tx_sem)) {
		k_sem_take(&get_dashboard_state()->ble_context.tx_sem, K_FOREVER);
	}
	clear_ancs_discovery_flags();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = ble_connected,
	.disconnected = ble_disconnected,
	.security_changed = ble_security_changed,
};

int bt_start(void)
{
	int err;

	while (bt_start_busy) {
		k_msleep(1);
	}
	bt_start_busy = 1;

	if (get_dashboard_state()->bt_state == 2 || get_dashboard_state()->bt_state == 0) {
		bt_start_busy = 0;
		return 0;
	}

	ad[1].type = BT_DATA_MANUFACTURER_DATA;
	ad[1].data_len = sizeof(manuf_data);
	if (is_master() == 1) {
		manuf_data[0] = 1;
	} else {
		manuf_data[0] = 2;
	}
	ad[1].data = manuf_data;
	strcpy(&manuf_data[1], get_serial_number());

	printk("c->esb_channel %d\r\n", get_dashboard_state()->esb_channel);

	if (is_master() == 1) {
		if (get_pt_test_mode() == 1) {
			if (get_dashboard_state()->esb_channel != 0xff) {
				sprintf(device_name, "%sV%d%d%d_%d_R_%02X%02X%02X", DEVICE_NAME,
					get_pt_version_major(), FW_VER_MINOR, FW_VER_PATCH,
					get_dashboard_state()->esb_channel,
					get_dashboard_state()->master_mac[2],
					get_dashboard_state()->master_mac[1],
					get_dashboard_state()->master_mac[0]);
			} else {
				sprintf(device_name, "%sV%d%d%d_R_%02X%02X%02X", DEVICE_NAME,
					get_pt_version_major(), FW_VER_MINOR, FW_VER_PATCH,
					get_dashboard_state()->master_mac[2],
					get_dashboard_state()->master_mac[1],
					get_dashboard_state()->master_mac[0]);
			}
		} else {
			if (get_dashboard_state()->esb_channel != 0xff) {
				sprintf(device_name, "%s_%d_R_%02X%02X%02X", DEVICE_NAME,
					get_dashboard_state()->esb_channel,
					get_dashboard_state()->master_mac[2],
					get_dashboard_state()->master_mac[1],
					get_dashboard_state()->master_mac[0]);
			} else {
				sprintf(device_name, "%s_R_%02X%02X%02X", DEVICE_NAME,
					get_dashboard_state()->master_mac[2],
					get_dashboard_state()->master_mac[1],
					get_dashboard_state()->master_mac[0]);
			}
		}
	} else {
		if (get_pt_test_mode() == 1) {
			if (get_dashboard_state()->esb_channel != 0xff) {
				sprintf(device_name, "%sV%d%d%d_%d_L_%02X%02X%02X", DEVICE_NAME,
					get_pt_version_major(), FW_VER_MINOR, FW_VER_PATCH,
					get_dashboard_state()->esb_channel,
					get_dashboard_state()->slave_mac[2],
					get_dashboard_state()->slave_mac[1],
					get_dashboard_state()->slave_mac[0]);
			} else {
				sprintf(device_name, "%sV%d%d%d_L_%02X%02X%02X", DEVICE_NAME,
					get_pt_version_major(), FW_VER_MINOR, FW_VER_PATCH,
					get_dashboard_state()->slave_mac[2],
					get_dashboard_state()->slave_mac[1],
					get_dashboard_state()->slave_mac[0]);
			}
		} else {
			if (get_dashboard_state()->esb_channel != 0xff) {
				sprintf(device_name, "%s_%d_L_%02X%02X%02X", DEVICE_NAME,
					get_dashboard_state()->esb_channel,
					get_dashboard_state()->slave_mac[2],
					get_dashboard_state()->slave_mac[1],
					get_dashboard_state()->slave_mac[0]);
			} else {
				sprintf(device_name, "%s_L_%02X%02X%02X", DEVICE_NAME,
					get_dashboard_state()->slave_mac[2],
					get_dashboard_state()->slave_mac[1],
					get_dashboard_state()->slave_mac[0]);
			}
		}
	}

	sd[0].data = device_name;
	sd[0].data_len = strlen(device_name);

	err = bt_set_name(device_name);
	if (err) {
		G1_LOG_ERR("Failed to set device name (err %d)\n\n", err);
	} else {
		err = bt_le_adv_start(
			BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, 0x160, 0x1e0, NULL), ad,
			ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
		if (!err) {
			get_dashboard_state()->bt_state = 2;
			printk("Advertising %s successfully started uptime %lld\n", device_name,
			       k_ticks_to_ms_ceil64(k_uptime_ticks()));
		}
	}

	bt_start_busy = 0;
	if (err) {
		system_reboot_delayed();
	}
	return err;
}

int ble_link_start(struct ble_context *ctx)
{
	int err;
	int bt_retry_count = 5;
	int wait = 50;

	link_ctx = ctx;
	G1_LOG_ERR("Starting Apple Notification Center Service client\n\n");

	while (bt_retry_count) {
		while (get_dashboard_state()->bt_state == 0 && wait != 0) {
			wait--;
			k_msleep(100);
		}
		err = bt_enable(NULL);
		if (!err) {
			break;
		}
		bt_disable();
		printk("BLE init failed (err %d) bt_retry_count %d\n", err, bt_retry_count);
		bt_retry_count--;
		k_msleep(1000);
	}

	bt_foreach_bond(BT_ID_DEFAULT, check_bonded_addr, NULL);
	settings_load();

	err = ancs_c_init(&ctx->notif);
	if (err) {
		printk("ANCS client init failed (err %d)\n", err);
		goto fail;
	}
	err = gattp_init();
	if (err) {
		printk("GATT Service client init failed (err %d)\n", err);
		goto fail;
	}
	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		printk("Failed to register authorization callbacks\n");
		goto fail;
	}
	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		printk("Failed to register authorization info callbacks.\n");
		goto fail;
	}
	bt_gatt_cb_register(&gatt_callbacks);

	err = init_nus();
	if (err) {
		printk("Failed to init nus.\n");
		goto fail;
	}
	err = bt_start();
	if (err) {
		printk("Advertising %s failed to start (err %d)\n", device_name, err);
		goto fail;
	}
	start_ancs_work_thread(ctx);
	return 0;

fail:
	printk("ancs or ncs init failure, reboot it\n");
	k_msleep(500);
	system_reboot_delayed();
	return err;
}

struct bt_conn *get_current_conn(void)
{
	return current_conn;
}

/* advertising / bond state helpers */
int stop_ble_advertising(void)
{
	int err = 0;

	if (get_dashboard_state()->bt_state == 2) {
		err = bt_le_adv_stop();
		if (err == 0) {
			get_dashboard_state()->bt_state = 3;
		}
	}
	return err;
}

bool is_ble_device_bound(void)
{
	bt_addr_le_t addr;

	memset(&addr, 0, sizeof(addr));
	if (bt_addr_le_cmp(&get_dashboard_state()->bonded_addr, &addr) == 0) {
		return false;
	}
	memset(&addr, 0xff, sizeof(addr));
	return bt_addr_le_cmp(&get_dashboard_state()->bonded_addr, &addr) != 0;
}
