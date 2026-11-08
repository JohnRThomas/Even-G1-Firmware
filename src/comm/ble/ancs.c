/*
 * ANCS client.  Once the link is encrypted it discovers the GATT service and
 * the Apple Notification Center Service on the phone, subscribes to the
 * notification and data sources, and on its own thread fetches the
 * attributes of every notification event, filters them against the app
 * whitelist and hands the result to the application protocol as a
 * notification request.  Based on the nRF Connect SDK sample
 * peripheral_ancs_client.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/gatt_dm.h>
#include "comm/ble/ancs_attr_max.h" /* <bluetooth/services/ancs_client.h> with a 256-byte attribute limit */
#include <bluetooth/services/gattp.h>

#include "system/logging.h"
#include "comm/ble/ble.h"
#include "comm/ble/ancs.h"
#include "comm/ble/proto.h"
#include "system/clock.h"
#include "services/notifications/notify.h"
#include "services/notifications/whitelist.h"
#include "services/production_test/production_test.h"
#include "util/common_utils.h"

#define ATTR_DATA_SIZE 32

/* every buffer ancs_c_init() registers has to fit the ANCS client's limit */
BUILD_ASSERT(sizeof(((struct notif_msg *)0)->message) <= BT_ANCS_ATTR_DATA_MAX);
BUILD_ASSERT(sizeof(((struct notif_msg *)0)->display_name) <= BT_ANCS_ATTR_DATA_MAX);

#define ANCS_MSGQ_MAX 10
#define UID_MSGQ_MAX  10

#define ANCS_THREAD_STACK_SIZE 5120
#define ANCS_THREAD_PRIORITY   (-11)

enum {
	DISCOVERY_ANCS_ONGOING,
	DISCOVERY_ANCS_SUCCEEDED,
	SERVICE_CHANGED_INDICATED
};

/* forward declarations */
static void discover_ancs_error_found_cb(struct bt_conn *conn, int err, void *ctx);
static void discover_ancs_completed_cb(struct bt_gatt_dm *dm, void *ctx);
static void discover_ancs_not_found_cb(struct bt_conn *conn, void *ctx);
static void indicate_sc_cb(struct bt_gattp *gattp, const struct bt_gattp_handle_range *handle_range,
			   int err);

static uint8_t app_id_received;

static uint8_t indicate_sc_count;
static struct bt_ancs_client ancs_c;
static struct bt_gattp gattp;

static atomic_t ancs_discovery_flags;
static struct bt_ancs_evt_notif notification_latest;
static struct bt_ancs_attr notif_attr_app_id_latest;
static struct k_msgq ancs_msgq;
static struct k_msgq uid_msgq;

static K_THREAD_STACK_DEFINE(ancs_work_thread_stack, ANCS_THREAD_STACK_SIZE);
static struct k_thread ancs_work_thread;

static const char *lit_eventid[BT_ANCS_EVT_ID_COUNT] = {"Added", "Modified", "Removed"};

static void bt_ancs_notification_source_handler(struct bt_ancs_client *ancs_c, int err,
						const struct bt_ancs_evt_notif *notif);
static void bt_ancs_data_source_handler(struct bt_ancs_client *ancs_c,
					const struct bt_ancs_attr_response *response);

static const struct bt_gatt_dm_cb discover_ancs_cb = {
	.completed = discover_ancs_completed_cb,
	.service_not_found = discover_ancs_not_found_cb,
	.error_found = discover_ancs_error_found_cb,
};

/**@brief Function for printing out errors that originated from the Notification Provider (iOS).
 *
 * @param[in] err_code_np Error code received from NP.
 */
static void err_code_print(uint8_t err_code_np)
{
	switch (err_code_np) {
	case BT_ATT_ERR_ANCS_NP_UNKNOWN_COMMAND:
		BLE_PRINTK("Error: Command ID was not recognized by the Notification Provider.\n");
		break;
	case BT_ATT_ERR_ANCS_NP_INVALID_COMMAND:
		BLE_PRINTK("Error: Command failed to be parsed on the Notification Provider.\n");
		break;
	case BT_ATT_ERR_ANCS_NP_INVALID_PARAMETER:
		BLE_PRINTK(
			"Error: Parameter does not refer to an existing object on the Notification Provider.\n");
		break;
	case BT_ATT_ERR_ANCS_NP_ACTION_FAILED:
		BLE_PRINTK(
			"Error: Perform Notification Action Failed on the Notification Provider.\n");
		break;
	default:
		break;
	}
}

static void bt_ancs_write_response_handler(struct bt_ancs_client *ancs_c, uint8_t err)
{
	err_code_print(err);
}

static void discover_gattp_completed_cb(struct bt_gatt_dm *dm, void *ctx);
static void discover_gattp_not_found_cb(struct bt_conn *conn, void *ctx);
static void discover_gattp_error_found_cb(struct bt_conn *conn, int err, void *ctx);

static const struct bt_gatt_dm_cb discover_gattp_cb = {
	.completed = discover_gattp_completed_cb,
	.service_not_found = discover_gattp_not_found_cb,
	.error_found = discover_gattp_error_found_cb,
};

void discover_gattp(struct bt_conn *conn)
{
	int err;

	err = bt_gatt_dm_start(conn, BT_UUID_GATT, &discover_gattp_cb, &gattp);
	if (err) {
		printk("Failed to start discovery for GATT Service (err %d)\n", err);
	}
}

static uint32_t last_notif_uid = 0xffffffff;

static void discover_ancs(struct bt_conn *conn, bool retry)
{
	int err;

	if (get_dashboard_state()->is_master != 2 || get_pt_test_mode() == 1) {
		return;
	}

	/* 1 Service Discovery at a time */
	if (atomic_test_and_set_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING)) {
		return;
	}

	/* If ANCS is found, do not discover again. */
	if (atomic_test_bit(&ancs_discovery_flags, DISCOVERY_ANCS_SUCCEEDED)) {
		atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
		return;
	}

	/* Check that Service Changed indication is received before discovering ANCS again. */
	if (retry &&
	    (!atomic_test_and_clear_bit(&ancs_discovery_flags, SERVICE_CHANGED_INDICATED))) {
		atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
		return;
	}

	err = bt_gatt_dm_start(conn, BT_UUID_ANCS, &discover_ancs_cb, &ancs_c);
	if (err) {
		printk("Failed to start discovery for ANCS (err %d)\n", err);
		atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
	}
}

static void discover_ancs_first(struct bt_conn *conn)
{
	discover_ancs(conn, false);
}

static void discover_ancs_again(struct bt_conn *conn)
{
	discover_ancs(conn, true);
}

static void discover_gattp_error_found_cb(struct bt_conn *conn, int err, void *ctx)
{
	printk("The discovery procedure for GATT Service failed, err %d\n", err);
	discover_ancs_first(conn);
}

static void discover_ancs_error_found_cb(struct bt_conn *conn, int err, void *ctx)
{
	printk("The discovery procedure for ANCS failed, err %d\n", err);
	atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
	discover_ancs_again(conn);
}

static void enable_ancs_notifications(struct bt_ancs_client *ancs_c)
{
	int err;

	err = bt_ancs_subscribe_notification_source(ancs_c, bt_ancs_notification_source_handler);
	if (err) {
		printk("Failed to enable Notification Source notification (err %d)\n", err);
	}

	err = bt_ancs_subscribe_data_source(ancs_c, bt_ancs_data_source_handler);
	if (err) {
		printk("Failed to enable Data Source notification (err %d)\n", err);
	}
}

static void discover_ancs_completed_cb(struct bt_gatt_dm *dm, void *ctx)
{
	int err;
	struct bt_ancs_client *ancs_c = (struct bt_ancs_client *)ctx;
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

	printk("The discovery procedure for ANCS succeeded\n");

	err = bt_ancs_handles_assign(dm, ancs_c);
	if (err) {
		printk("Could not init ANCS client object, error: %d\n", err);
	} else {
		atomic_set_bit(&ancs_discovery_flags, DISCOVERY_ANCS_SUCCEEDED);
		enable_ancs_notifications(ancs_c);
	}

	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Could not release the discovery data, error "
		       "code: %d\n",
		       err);
	}

	atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
	discover_ancs_again(conn);
}

static void discover_ancs_not_found_cb(struct bt_conn *conn, void *ctx)
{
	printk("ANCS could not be found during the discovery\n");
	atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
	discover_ancs_again(conn);
}

static void enable_gattp_indications(struct bt_gattp *gattp)
{
	int err;

	err = bt_gattp_subscribe_service_changed(gattp, indicate_sc_cb);
	if (err) {
		printk("Cannot subscribe to Service Changed indication (err %d)\n", err);
	}
}

static void discover_gattp_completed_cb(struct bt_gatt_dm *dm, void *ctx)
{
	int err;
	struct bt_gattp *gattp = (struct bt_gattp *)ctx;
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

	/* Checks if the service is empty.
	 * Discovery Manager handles empty services.
	 */
	if (bt_gatt_dm_attr_cnt(dm) > 1) {
		printk("The discovery procedure for GATT Service succeeded\n");
		err = bt_gattp_handles_assign(dm, gattp);
		if (err) {
			printk("Could not init GATT Service client object, error: %d\n", err);
		} else {
			enable_gattp_indications(gattp);
		}
	} else {
		printk("GATT Service could not be found during the discovery\n");
	}

	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Could not release the discovery data, error "
		       "code: %d\n",
		       err);
	}

	discover_ancs_first(conn);
}

static void discover_gattp_not_found_cb(struct bt_conn *conn, void *ctx)
{
	printk("GATT Service could not be found during the discovery\n");
	discover_ancs_first(conn);
}

void clear_ancs_discovery_flags(void)
{
	atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_ONGOING);
	atomic_clear_bit(&ancs_discovery_flags, DISCOVERY_ANCS_SUCCEEDED);
	atomic_clear_bit(&ancs_discovery_flags, SERVICE_CHANGED_INDICATED);
}

/* plain store of the initial value, for the start of a new encrypted link */
void reset_ancs_discovery_flags(void)
{
	ancs_discovery_flags = ATOMIC_INIT(0);
}

static void indicate_sc_cb(struct bt_gattp *gattp, const struct bt_gattp_handle_range *handle_range,
			   int err)
{
	if (!err) {
		indicate_sc_count++;
		if (indicate_sc_count >= 5) {
			indicate_sc_count = 0;
			G1_LOG_ERR("disconnect because can not discover ancs.\n");
			bt_conn_disconnect(gattp->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			return;
		}
		atomic_set_bit(&ancs_discovery_flags, SERVICE_CHANGED_INDICATED);
		discover_ancs_again(gattp->conn);
	}
}

static int init_msgq_ancs(void)
{
	int err;

	err = k_msgq_alloc_init(&ancs_msgq, sizeof(struct notif_msg), ANCS_MSGQ_MAX);
	if (err) {
		G1_LOG_ERR("k_msgq alloc failed\n");
	}
	return err;
}

static int enqueue_ancs(struct notif_msg *data)
{
	struct notif_msg msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	if (ancs_msgq.max_msgs == 0) {
		return 1;
	}
	if (k_msgq_num_used_get(&ancs_msgq) == ANCS_MSGQ_MAX) {
		k_msgq_get(&ancs_msgq, &msg, K_NO_WAIT);
		BLE_PRINTK("enqueue ancs drop package! \n");
	}
	memcpy(&msg, data, sizeof(msg));
	err = k_msgq_put(&ancs_msgq, &msg, K_NO_WAIT);
	if (err) {
		G1_LOG_ERR("en ancs F\n");
	}
	return err;
}

/* the dashboard is not in a mode that suppresses notifications */
#define ANCS_NOTIF_MODE_ALLOWED()                                                                  \
	(get_dashboard_state()->dashboard_mode != 11 &&                                            \
	 get_dashboard_state()->dashboard_mode != 10 &&                                            \
	 get_dashboard_state()->dashboard_mode != 9 &&                                             \
	 get_dashboard_state()->dashboard_mode != 12 &&                                            \
	 get_dashboard_state()->dashboard_mode != 16 &&                                            \
	 get_dashboard_state()->dashboard_mode != 20 &&                                            \
	 get_dashboard_state()->dashboard_mode != 14 &&                                            \
	 get_dashboard_state()->dashboard_mode != 15 &&                                            \
	 get_dashboard_state()->dashboard_mode != 17 &&                                            \
	 get_dashboard_state()->dashboard_mode != 18 &&                                            \
	 get_dashboard_state()->dashboard_mode != 19)

/* wait while the ESB link is busy */
#define ANCS_WAIT_ESB_IDLE()                                                                       \
	while (get_dashboard_state()->work_mode == 1 || get_dashboard_state()->work_mode == 8) {   \
		k_sleep(K_MSEC(5000));                                                             \
	}

static void bt_ancs_data_source_handler(struct bt_ancs_client *ancs_c,
					const struct bt_ancs_attr_response *response)
{
	struct ble_context *ctx = get_ble_context();

	G1_LOG_ERR("bt_ancs_data_source_handler response->command_id %d\n", response->command_id);

	switch (response->command_id) {
	case BT_ANCS_COMMAND_ID_GET_NOTIF_ATTRIBUTES:
		if (response->attr.attr_id == BT_ANCS_NOTIF_ATTR_ID_APP_IDENTIFIER) {
			notif_attr_app_id_latest = response->attr;
			app_id_received = 1;
		}
		break;
	case BT_ANCS_COMMAND_ID_GET_APP_ATTRIBUTES:
		if (app_id_received && notification_latest.notif_uid != last_notif_uid) {
			if (k_uptime_get_32() - get_dashboard_state()->ble_context.connected_time <
			    10000) {
				break;
			}
			ANCS_WAIT_ESB_IDLE();
			if (ANCS_NOTIF_MODE_ALLOWED()) {
				last_notif_uid = notification_latest.notif_uid;
				ctx->notif.action = notification_latest.evt_id;
				enqueue_ancs(&ctx->notif);
				G1_LOG_ERR(
					"=========================>bt_ancs_get_attr_data_sem %d evt_id %d action %d\n",
					notification_latest.notif_uid, notification_latest.evt_id,
					ctx->notif.action);
				memset(&ctx->notif, 0, sizeof(ctx->notif));
				k_sem_give(&ctx->ancs_get_attr_data_sem);
				app_id_received = 0;
			}
		}
		break;
	default:
		/* No implementation needed. */
		break;
	}
}

static int dequeue_ancs(struct notif_msg *data)
{
	struct notif_msg msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	if (ancs_msgq.max_msgs == 0) {
		return 1;
	}
	err = k_msgq_get(&ancs_msgq, &msg, K_NO_WAIT);
	if (err) {
		G1_LOG_ERR("D ancs F\n");
	} else {
		memcpy(data, &msg, sizeof(msg));
	}
	return err;
}

static int init_msgq_uid(void)
{
	int err;

	err = k_msgq_alloc_init(&uid_msgq, sizeof(struct bt_ancs_evt_notif), UID_MSGQ_MAX);
	if (err) {
		G1_LOG_ERR("k_msgq alloc failed\n");
	}
	return err;
}

static int enqueue_uid(const struct bt_ancs_evt_notif *notif)
{
	struct bt_ancs_evt_notif local_data;
	int err;

	memset(&local_data, 0, sizeof(local_data));
	if (uid_msgq.max_msgs == 0) {
		return 1;
	}
	if (k_msgq_num_used_get(&uid_msgq) == UID_MSGQ_MAX) {
		k_msgq_get(&uid_msgq, &local_data, K_NO_WAIT);
		BLE_PRINTK("enqueue uid drop package! \n");
	}
	memcpy(&local_data, notif, sizeof(local_data));
	G1_LOG_ERR("local_data uid %d\n", local_data.notif_uid);
	err = k_msgq_put(&uid_msgq, &local_data, K_NO_WAIT);
	if (err) {
		G1_LOG_ERR("en uid F\n");
	}
	return err;
}

static void bt_ancs_notification_source_handler(struct bt_ancs_client *ancs_c, int err,
						const struct bt_ancs_evt_notif *notif)
{
	struct ble_context *ctx = get_ble_context();

	if (err) {
		return;
	}
	printk("\nNotification UID:         %u\nEvent:       %s\n", notif->notif_uid,
	       lit_eventid[notif->evt_id]);
	if (k_uptime_get_32() - get_dashboard_state()->ble_context.connected_time < 10000) {
		return;
	}
	ANCS_WAIT_ESB_IDLE();
	if (!ANCS_NOTIF_MODE_ALLOWED() || notif->evt_id >= BT_ANCS_EVT_ID_COUNT) {
		return;
	}
	if (enqueue_uid(notif)) {
		return;
	}
	G1_LOG_ERR(
		"============================>bt_ancs_get_attr_req_sem %d count %d notif->evt_id %d\n",
		notif->notif_uid, k_sem_count_get(&ctx->ancs_get_attr_req_sem), notif->evt_id);
	memset(&ctx->notif, 0, sizeof(ctx->notif));
	if (notif->evt_id == BT_ANCS_EVENT_ID_NOTIFICATION_REMOVED) {
		ctx->notif.uid = notif->notif_uid;
	}
	k_sem_give(&ctx->ancs_get_attr_req_sem);
}

static int dequeue_uid(struct bt_ancs_evt_notif *notif)
{
	struct bt_ancs_evt_notif local_data;
	int err;

	memset(&local_data, 0, sizeof(local_data));
	if (uid_msgq.max_msgs == 0) {
		return 1;
	}
	err = k_msgq_get(&uid_msgq, &local_data, K_NO_WAIT);
	if (err) {
		G1_LOG_ERR("D uid F\n");
	} else {
		memcpy(notif, &local_data, sizeof(local_data));
	}
	return err;
}

int ancs_c_init(struct notif_msg *data)
{
	int err;

	err = bt_ancs_client_init(&ancs_c);
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_APP_IDENTIFIER,
				    data->app_identifier, ATTR_DATA_SIZE);
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_app_attr(&ancs_c, BT_ANCS_APP_ATTR_ID_DISPLAY_NAME,
					data->display_name, sizeof(data->display_name));
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_TITLE, data->title,
				    ATTR_DATA_SIZE);
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_SUBTITLE, data->subtitle,
				    ATTR_DATA_SIZE);
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_MESSAGE, data->message,
				    sizeof(data->message));
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	err = bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_DATE, data->date,
				    ATTR_DATA_SIZE);
	if (err) {
		G1_LOG_ERR("%d: err=%d\n", __LINE__, err);
		return err;
	}

	return 0;
}

int gattp_init(void)
{
	return bt_gattp_init(&gattp);
}

static int request_ancs_attr_ext(void)
{
	struct ble_context *ctx = get_ble_context();
	uint16_t retry_time = 0;
	bool app_id_ready = false;
	int err;

	err = bt_ancs_request_attrs(&ancs_c, &notification_latest, bt_ancs_write_response_handler);

	/* wait up to about one second for the app identifier attribute */
	do {
		if (notif_attr_app_id_latest.attr_id == BT_ANCS_NOTIF_ATTR_ID_APP_IDENTIFIER &&
		    notif_attr_app_id_latest.attr_len != 0 &&
		    notif_attr_app_id_latest.attr_data[0] != '\0' &&
		    ctx->notif.app_identifier[0] != '\0') {
			app_id_ready = true;
			break;
		}
		k_sleep(K_MSEC(1));
		retry_time++;
	} while (retry_time != 1001);

	if (!app_id_ready) {
		G1_LOG_ERR("notif_attr_app_id_latest.attr_id %d "
			   "notif_attr_app_id_latest.attr_len %d "
			   "strlen(notif_attr_app_id_latest.attr_data) %d "
			   "c->notifi_data.app_identifier %d\n",
			   notif_attr_app_id_latest.attr_id, notif_attr_app_id_latest.attr_len,
			   strlen(notif_attr_app_id_latest.attr_data),
			   ctx->notif.app_identifier[0]);
	}

	if ((app_id_ready ||
	     notif_attr_app_id_latest.attr_id == BT_ANCS_NOTIF_ATTR_ID_APP_IDENTIFIER) &&
	    (notif_attr_app_id_latest.attr_len != 0 &&
	     notif_attr_app_id_latest.attr_data[0] != '\0')) {
		BLE_PRINTK("Request for %s: retry_time %d\n", notif_attr_app_id_latest.attr_data,
			   retry_time);
		while (retry_time < 200) {
			err = bt_ancs_request_app_attr(&ancs_c, notif_attr_app_id_latest.attr_data,
						       notif_attr_app_id_latest.attr_len,
						       bt_ancs_write_response_handler);
			if (!err) {
				break;
			}
			retry_time++;
			k_sleep(K_MSEC(5));
		}
		if (err) {
			printk("Failed requesting attributes for a given app (err: %d)\n", err);
		}
	}

	BLE_PRINTK("notif_attr_app_id_latest.attr_id (%d/%d) retry_time %d\n",
		   notif_attr_app_id_latest.attr_id, notif_attr_app_id_latest.attr_len, retry_time);
	return err;
}

/* Rewrites an ANCS date "yyyyMMddTHHmmSS" in place as "MM-dd HH:mm". */
static void format_ancs_date(uint8_t *s)
{
	s[0] = s[4];
	s[1] = s[5];
	s[2] = '-';
	s[3] = s[6];
	s[4] = s[7];
	s[5] = ' ';
	s[6] = s[9];
	s[7] = s[10];
	s[8] = ':';
	s[9] = s[11];
	s[10] = s[12];
	s[11] = 0;
}

static void recv_ble_data(struct ble_context *ctx)
{
	uint8_t hdr[4] = {0x4b, 0, 0, 0};
	struct notif_msg data;
	int ret;

	memset(&data, 0, sizeof(data));
	if (dequeue_ancs(&data) == 0 && (data.action >= 2 || data.app_identifier[0] != '\0')) {
		data.recv_time = get_date();
		if (data.recv_time == 0) {
			data.recv_time = 1;
		}
		data.uid = notification_latest.notif_uid;
		dump_ancs("FROM:ancs", &data);
		if (data.action >= 2) {
			hdr[1] = 2;
		} else {
			ret = is_on_whitelist_by_identifier(data.app_identifier);
			if (ret == 2) {
				hdr[1] = 2;
			} else if (ret == 3) {
				hdr[1] = 4;
			} else {
				if (ret == 1) {
					BLE_PRINTK("blocked! app(%s) not in whitelist \n",
						   data.app_identifier);
					send_whilelist_app_info_wrapper(&data);
				}
				return;
			}
		}
		if (notification_latest.category_id == BT_ANCS_CATEGORY_ID_MISSED_CALL) {
			strncpy(data.app_identifier, "com.apple.mobilephone_missed",
				sizeof(data.app_identifier));
		}
		format_ancs_date(data.date);
		ble_process_req_dispatch(ctx, (struct ble_req_hdr *)hdr, (uint8_t *)&data);
	}
}

static void ble_ancs_data_req_thread(void *p1, void *p2, void *p3)
{
	struct ble_context *ctx = p1;

	G1_LOG_INF("enter\n");
	k_sem_init(&ctx->ancs_get_attr_req_sem, 0, 10);
	k_sem_init(&ctx->ancs_get_attr_data_sem, 0, 10);
	init_msgq_ancs();
	init_msgq_uid();
	k_sleep(K_MSEC(5000));

	while (1) {
		while (get_dashboard_state()->work_mode == 1 ||
		       get_dashboard_state()->work_mode == 8) {
			k_sleep(K_MSEC(5000));
		}
		if (!(get_dashboard_state()->module_mask & BIT(7))) {
			k_msleep(1000);
			continue;
		}
		G1_LOG_ERR("!!!!!!!!!wait -> ancs_get_attr_req_sem\n");
		k_sem_take(&ctx->ancs_get_attr_req_sem, K_FOREVER);
		if (dequeue_uid(&notification_latest)) {
			continue;
		}
		G1_LOG_ERR("!!!!!!!!!!!!!!get -> ancs_get_attr_req_sem %d evt_id %d\n",
			   notification_latest.notif_uid, notification_latest.evt_id);
		if (notification_latest.evt_id < BT_ANCS_EVENT_ID_NOTIFICATION_REMOVED) {
			request_ancs_attr_ext();
			G1_LOG_ERR(
				"!!!!!!!!!!!!wait -> ancs_get_attr_req_sem add done, and wait -> "
				"ancs_get_attr_data_sem\n");
			k_sem_take(&ctx->ancs_get_attr_data_sem, K_MSEC(1000));
		} else {
			ctx->notif.action = BT_ANCS_EVENT_ID_NOTIFICATION_REMOVED;
			ctx->notif.uid = notification_latest.notif_uid;
			ctx->notif.sync_state = 0;
			/* a string literal is stored into a char (keeps the
			 * low byte of its address) */
			ctx->notif.app_identifier[0] = (char)(uintptr_t)"\r";
			ctx->notif.display_name[0] = (char)(uintptr_t)"\r";
			ctx->notif.busy = 0;
			enqueue_ancs(&ctx->notif);
			memset(&ctx->notif, 0, sizeof(ctx->notif));
			G1_LOG_ERR(
				"!!!!!!!!!!!!wait -> ancs_get_attr_req_sem remove done, and wait -> "
				"ancs_get_attr_data_sem\n");
		}
		recv_ble_data(ctx);
		G1_LOG_ERR("!!!!!!!!!!!!!!!!wait -> ancs_get_attr_data_sem done\n");
	}
}

void start_ancs_work_thread(struct ble_context *ctx)
{
	if (get_dashboard_state()->is_master != 2 || get_pt_test_mode() == 1) {
		return;
	}
	k_thread_create(&ancs_work_thread, ancs_work_thread_stack,
			K_THREAD_STACK_SIZEOF(ancs_work_thread_stack), ble_ancs_data_req_thread,
			ctx, NULL, NULL, ANCS_THREAD_PRIORITY, 0, K_NO_WAIT);
	G1_LOG_INF("exit\n\n");
}

void ancs_delete_notification(uint32_t uid)
{
	struct ble_context *ctx = get_ble_context();
	struct bt_ancs_evt_notif notif;

	notif.notif_uid = uid;
	notif.evt_id = BT_ANCS_EVENT_ID_NOTIFICATION_REMOVED;
	if (!enqueue_uid(&notif)) {
		k_sem_give(&ctx->ancs_get_attr_req_sem);
	}
}

int set_ancs_enable(int enable)
{
	clear_ancs_discovery_flags();
	if (enable) {
		struct bt_conn *conn = get_current_conn();

		if (conn == NULL) {
			return 0;
		}
		discover_gattp(conn);
		if (get_dashboard_state()->bonded) {
			return 0;
		}
		get_dashboard_state()->bonded = 1;
	} else {
		if (get_dashboard_state()->bonded != 1) {
			return 0;
		}
		get_dashboard_state()->bonded = 0;
	}
	get_dashboard_state()->dashboard_cmd = 0;
	k_sem_give(&get_dashboard_state()->dashboard_position_sem);
	return 0;
}
