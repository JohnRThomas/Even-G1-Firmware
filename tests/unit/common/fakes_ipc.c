#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <string.h>

#include "fakes_ipc.h"

#include "threads/threads.h"

DEFINE_FAKE_VALUE_FUNC(int, ipc_service_open_instance, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, ipc_service_register_endpoint, const struct device *,
		       struct ipc_ept *, const struct ipc_ept_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, ipc_service_send, struct ipc_ept *, const void *, size_t);

DEFINE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);

struct g1_pkt_log g1_ipc_service_log;
struct g1_pkt_log g1_sync_to_slave_log;
struct g1_pkt_log g1_sync_to_master_log;
struct g1_fake_ipc g1_fake_ipc;

static int ipc_open_instance_default(const struct device *instance)
{
	g1_fake_ipc.instance = instance;
	return 0;
}

static int ipc_register_endpoint_default(const struct device *instance, struct ipc_ept *ept,
					 const struct ipc_ept_cfg *cfg)
{
	if (ept) {
		ept->instance = instance;
	}
	g1_fake_ipc.ept = ept;
	g1_fake_ipc.cfg = cfg;
	if (g1_fake_ipc.bind_on_register && cfg && cfg->cb.bound) {
		cfg->cb.bound(cfg->priv);
	}
	return 0;
}

static int ipc_send_default(struct ipc_ept *ept, const void *data, size_t len)
{
	ARG_UNUSED(ept);
	g1_pkt_log_add(&g1_ipc_service_log, data, data ? len : 0);
	return (int)len;
}

int g1_fake_ipc_bind(void)
{
	if (g1_fake_ipc.cfg == NULL) {
		return -ENODEV;
	}
	if (g1_fake_ipc.cfg->cb.bound) {
		g1_fake_ipc.cfg->cb.bound(g1_fake_ipc.cfg->priv);
	}
	return 0;
}

int g1_fake_ipc_deliver(const void *data, size_t len)
{
	if (g1_fake_ipc.cfg == NULL) {
		return -ENODEV;
	}
	if (g1_fake_ipc.cfg->cb.received) {
		g1_fake_ipc.cfg->cb.received(data, len, g1_fake_ipc.cfg->priv);
	}
	return 0;
}

/* FFF return_val_seq semantics */
#define FAKE_RETURN(fake)                                                                          \
	((fake).return_val_seq_len == 0                                                            \
		 ? (fake).return_val                                                               \
		 : ((fake).return_val_seq_idx < (fake).return_val_seq_len                          \
			    ? (fake).return_val_seq[(fake).return_val_seq_idx++]                   \
			    : (fake).return_val_seq[(fake).return_val_seq_len - 1]))

static int sync_to_slave_default(struct GlassesState *state, int cmd, void *data, size_t len)
{
	ARG_UNUSED(state);
	ARG_UNUSED(cmd);
	g1_pkt_log_add(&g1_sync_to_slave_log, data, data ? len : 0);
	return FAKE_RETURN(sync_to_slave_fake);
}

static unsigned int sync_to_master_default(struct GlassesState *state, int cmd, void *data,
					   size_t len)
{
	ARG_UNUSED(state);
	ARG_UNUSED(cmd);
	g1_pkt_log_add(&g1_sync_to_master_log, data, data ? len : 0);
	return FAKE_RETURN(sync_to_master_fake);
}

static void fakes_ipc_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(ipc_service_open_instance);
	RESET_FAKE(ipc_service_register_endpoint);
	RESET_FAKE(ipc_service_send);
	ipc_service_open_instance_fake.custom_fake = ipc_open_instance_default;
	ipc_service_register_endpoint_fake.custom_fake = ipc_register_endpoint_default;
	ipc_service_send_fake.custom_fake = ipc_send_default;
	g1_pkt_log_clear(&g1_ipc_service_log);
	memset(&g1_fake_ipc, 0, sizeof(g1_fake_ipc));

	RESET_FAKE(sync_to_slave);
	RESET_FAKE(sync_to_master);
	sync_to_slave_fake.custom_fake = sync_to_slave_default;
	sync_to_master_fake.custom_fake = sync_to_master_default;
	g1_pkt_log_clear(&g1_sync_to_slave_log);
	g1_pkt_log_clear(&g1_sync_to_master_log);
}

G1_UNIT_RESET_HOOK(fakes_ipc_reset);
