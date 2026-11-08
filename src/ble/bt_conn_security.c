/*
 * Security behaviour of LE connections, layered over the Zephyr host with the
 * linker's --wrap (see CMakeLists.txt): every call of bt_conn_add_le() and
 * bt_conn_set_security() from outside conn.c lands in the __wrap_ functions
 * below, and __real_bt_conn_add_le() is the host's own function.
 *
 * - A new LE connection starts at security level 2: sec_level and
 *   required_sec_level are L2 instead of L1.  bt_conn_get_security() reports L2
 *   until an encryption change sets the real level, so, for example, GATT does
 *   not request security on reconnection for a subscription that needs L2.
 *   Attribute permissions are unaffected: they test the link's encryption.
 * - bt_conn_set_security() always starts security (as peripheral: sends an SMP
 *   Security Request), even when the connection is already at, or already
 *   requires, the requested level.  Zephyr's version returns 0 without doing
 *   anything in that case.
 */
#include <errno.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <../subsys/bluetooth/host/conn_internal.h>
#include <../subsys/bluetooth/host/smp.h>

struct bt_conn *__real_bt_conn_add_le(uint8_t id, const bt_addr_le_t *peer);
struct bt_conn *__wrap_bt_conn_add_le(uint8_t id, const bt_addr_le_t *peer);
int __wrap_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec);

struct bt_conn *__wrap_bt_conn_add_le(uint8_t id, const bt_addr_le_t *peer)
{
	struct bt_conn *conn = __real_bt_conn_add_le(id, peer);

	if (conn) {
		conn->sec_level = BT_SECURITY_L2;
		conn->required_sec_level = BT_SECURITY_L2;
	}
	return conn;
}

/* Zephyr's bt_conn_set_security() for an LE-only host, without its
 * "nothing to do" early return. */
int __wrap_bt_conn_set_security(struct bt_conn *conn, bt_security_t sec)
{
	int err;

	if (conn->state != BT_CONN_CONNECTED) {
		return -ENOTCONN;
	}

	if (IS_ENABLED(CONFIG_BT_SMP_SC_ONLY)) {
		sec = BT_SECURITY_L4;
	}

	if (IS_ENABLED(CONFIG_BT_SMP_OOB_LEGACY_PAIR_ONLY)) {
		sec = BT_SECURITY_L3;
	}

	atomic_set_bit_to(conn->flags, BT_CONN_FORCE_PAIR, sec & BT_SECURITY_FORCE_PAIR);
	conn->required_sec_level = sec & ~BT_SECURITY_FORCE_PAIR;

	err = bt_smp_start_security(conn);

	/* reset required security level in case of error */
	if (err) {
		conn->required_sec_level = conn->sec_level;
	}

	return err;
}
