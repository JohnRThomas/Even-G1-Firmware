/*
 * IPC service and ESB send path fakes. ipc_service_send() logs and returns len.
 * sync_to_slave/master log the data and return the ack wait in ms (0 = at once; >= 3000 counts as
 * failed).
 */
#ifndef G1_UNIT_FAKES_IPC_H_
#define G1_UNIT_FAKES_IPC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/fff.h>
#include <glasses_state.h>
#include "harness.h"

DECLARE_FAKE_VALUE_FUNC(int, ipc_service_open_instance, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, ipc_service_register_endpoint, const struct device *,
			struct ipc_ept *, const struct ipc_ept_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, ipc_service_send, struct ipc_ept *, const void *, size_t);

extern struct g1_pkt_log g1_ipc_service_log;

struct g1_fake_ipc {
	const struct device *instance;
	struct ipc_ept *ept;
	const struct ipc_ept_cfg *cfg;
	/* register_endpoint calls bound() before returning */
	bool bind_on_register;
};
extern struct g1_fake_ipc g1_fake_ipc;

int g1_fake_ipc_bind(void);
int g1_fake_ipc_deliver(const void *data, size_t len);

DECLARE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DECLARE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);

extern struct g1_pkt_log g1_sync_to_slave_log;
extern struct g1_pkt_log g1_sync_to_master_log;

#endif /* G1_UNIT_FAKES_IPC_H_ */
