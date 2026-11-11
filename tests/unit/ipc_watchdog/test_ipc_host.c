/* Packets are [receiver id][payload] in both directions. */
#include "harness.h"
#include "fakes.h"

#include "ipc/ipc_host.c"

#define IPC0 DEVICE_DT_GET(DT_NODELABEL(ipc0))

/* IPC service fakes: register_endpoint() keeps the endpoint, send() returns len */
FAKE_VALUE_FUNC(int, ipc_service_open_instance, const struct device *);
FAKE_VALUE_FUNC(int, ipc_service_register_endpoint, const struct device *, struct ipc_ept *,
    const struct ipc_ept_cfg *);
FAKE_VALUE_FUNC(int, ipc_service_send, struct ipc_ept *, const void *, size_t);

static const struct ipc_ept_cfg *ipc0_cfg;
/* the network core binds as soon as the endpoint is registered */
static bool bind_on_register;

static int register_endpoint(const struct device *instance, struct ipc_ept *ept,
           const struct ipc_ept_cfg *cfg)
{
  ARG_UNUSED(instance);
  ARG_UNUSED(ept);
  ipc0_cfg = cfg;
  if (bind_on_register && cfg->cb.bound) {
    cfg->cb.bound(cfg->priv);
  }
  return 0;
}

static int send_len(struct ipc_ept *ept, const void *data, size_t len)
{
  ARG_UNUSED(ept);
  ARG_UNUSED(data);
  return (int)len;
}

/* the network core sends a packet on ipc0 */
static void deliver(const void *data, size_t len)
{
  zassert_not_null(ipc0_cfg, "no endpoint registered");
  ipc0_cfg->cb.received(data, len, ipc0_cfg->priv);
}

FAKE_VALUE_FUNC(int, recv_a, struct GlassesState *, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, recv_b, struct GlassesState *, const uint8_t *, size_t);

static struct ipc_ep_cfg ep_a = {.id = 1, .name = "cpuapp-hw-id", .recv_cb = recv_a};
static struct ipc_ep_cfg ep_b = {.id = 4, .name = "cpunet-esbs", .recv_cb = recv_b};

/* a packet for receiver 1 */
static const uint8_t packet[4] = {1, 8, 0, 0};

static void ipc_host_before(void *fixture)
{
  ARG_UNUSED(fixture);

  RESET_FAKE(ipc_service_open_instance);
  RESET_FAKE(ipc_service_register_endpoint);
  RESET_FAKE(ipc_service_send);
  RESET_FAKE(recv_a);
  RESET_FAKE(recv_b);
  ipc_service_register_endpoint_fake.custom_fake = register_endpoint;
  ipc_service_send_fake.custom_fake = send_len;
  ipc0_cfg = NULL;
  bind_on_register = false;
  GLOBAL_IPC_SERVICE_CONTEXT = NULL;
  ipc_ready = 0;
  ipc_send_fail_cnt = 0;
  k_sem_reset(&ipc0_bound_sem);

  /* as init_context() does */
  register_ipc_service_context(&g1_state()->ipc_ctx);
}

ZTEST_SUITE(ipc_host, NULL, NULL, ipc_host_before, NULL, NULL);

static void ipc_up(void)
{
  bind_on_register = true;
  zassert_equal(serialization_init(), 0);
  zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_context_gets_the_send_and_register_functions)
{
  struct ipc_service_context *ctx = &g1_state()->ipc_ctx;

  zassert_equal_ptr(ctx->send, global_ipc_service_send);
  zassert_not_null(ctx->register_recv_cb);
  zassert_equal(ctx->count, 0);
}

ZTEST(ipc_host, test_receivers_register_up_to_the_table_size)
{
  struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
  static struct ipc_ep_cfg eps[IPC_RECEIVER_MAX + 1];

  for (int index = 0; index < IPC_RECEIVER_MAX; index++) {
    eps[index].id = index;
    zassert_equal(ctx->register_recv_cb(&eps[index]), 0, "receiver %d", index);
  }
  zassert_equal(ctx->register_recv_cb(&eps[IPC_RECEIVER_MAX]), -1);

  zassert_equal(ctx->count, IPC_RECEIVER_MAX);
  zassert_equal_ptr(ctx->receivers[IPC_RECEIVER_MAX - 1], &eps[IPC_RECEIVER_MAX - 1]);
}

ZTEST(ipc_host, test_init_opens_ipc0_and_registers_its_endpoint)
{
  bind_on_register = true;

  zassert_equal(serialization_init(), 0);

  zassert_equal(ipc_service_open_instance_fake.call_count, 1);
  zassert_equal_ptr(ipc_service_open_instance_fake.arg0_val, IPC0);
  zassert_equal(ipc_service_register_endpoint_fake.call_count, 1);
  zassert_equal_ptr(ipc_service_register_endpoint_fake.arg0_val, IPC0);
  zassert_not_null(ipc0_cfg);
  expect_str(ipc0_cfg->name, "ipc0");
  zassert_not_null(ipc0_cfg->cb.bound);
  zassert_not_null(ipc0_cfg->cb.received);
  zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_init_accepts_an_instance_that_is_already_open)
{
  G1_FAKE_RETURN(ipc_service_open_instance, -EALREADY);
  bind_on_register = true;

  zassert_equal(serialization_init(), 0);

  zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_init_fails_when_ipc0_cannot_be_opened)
{
  G1_FAKE_RETURN(ipc_service_open_instance, -EIO);

  zassert_equal(serialization_init(), -1);

  zassert_equal(ipc_service_register_endpoint_fake.call_count, 0);
  zassert_equal(ipc_ready, 0);
}

ZTEST(ipc_host, test_init_fails_when_the_endpoint_cannot_be_registered)
{
  G1_FAKE_RETURN(ipc_service_register_endpoint, -EINVAL);

  zassert_equal(serialization_init(), -1);

  zassert_equal(ipc_ready, 0);
}

static volatile int init_ret;
static volatile bool init_done;

static void init_entry(void *arg, void *unused1, void *unused2)
{
  ARG_UNUSED(arg);
  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  init_ret = serialization_init();
  init_done = true;
}

ZTEST(ipc_host, test_init_waits_for_the_network_core_to_bind)
{
  bool done_before_bind;
  int sent_before_bind, joined;
  k_tid_t tid;

  init_done = false;
  init_ret = 0x7fffffff;
  tid = g1_thread_start(init_entry, NULL, K_PRIO_PREEMPT(5));
  k_sleep(K_MSEC(1000));
  done_before_bind = init_done;
  sent_before_bind = global_ipc_service_send(packet, sizeof(packet));

  zassert_not_null(ipc0_cfg, "no endpoint registered");
  ipc0_cfg->cb.bound(ipc0_cfg->priv); /* the network core binds */
  joined = k_thread_join(tid, K_SECONDS(1));

  zassert_false(done_before_bind, "serialization_init() did not wait for the bind");
  zassert_equal(sent_before_bind, -1, "sent before the endpoint was bound");
  zassert_equal(joined, 0);
  zassert_equal(init_ret, 0);
  zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_init_reboots_when_the_network_core_never_binds)
{
  (void)serialization_init();
  zassert_equal(system_reboot_delayed_fake.call_count, 1);
}

ZTEST(ipc_host, test_packet_goes_to_the_receiver_of_its_first_byte)
{
  struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
  static const uint8_t pkt[] = {0x04, 0xaa, 0xbb, 0xcc};

  ctx->register_recv_cb(&ep_a);
  ctx->register_recv_cb(&ep_b);
  ipc_up();

  deliver(pkt, sizeof(pkt));

  zassert_equal(recv_a_fake.call_count, 0);
  zassert_equal(recv_b_fake.call_count, 1);
  zassert_equal_ptr(recv_b_fake.arg0_val, g1_state());
  expect_bytes(recv_b_fake.arg1_val, recv_b_fake.arg2_val, "aa bb cc");
}

ZTEST(ipc_host, test_packet_for_an_unknown_receiver_is_dropped)
{
  struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
  static const uint8_t pkt[] = {0x09, 1, 2, 3, 4, 5, 6, 7};

  ctx->register_recv_cb(&ep_a);
  ctx->register_recv_cb(&ep_b);
  ipc_up();

  deliver(pkt, sizeof(pkt));

  zassert_equal(recv_a_fake.call_count, 0);
  zassert_equal(recv_b_fake.call_count, 0);
}

ZTEST(ipc_host, test_empty_packet_is_dropped)
{
  struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
  static const uint8_t stale[] = {0x04};

  ctx->register_recv_cb(&ep_b);
  ipc_up();

  deliver(stale, 0);

  zassert_equal(recv_b_fake.call_count, 0, "receiver called with length %zu",
          recv_b_fake.arg2_val);
}

ZTEST(ipc_host, test_send_hands_the_packet_to_the_endpoint_unchanged)
{
  ipc_up();

  zassert_equal(g1_state()->ipc_ctx.send(packet, sizeof(packet)), 4);

  zassert_equal(ipc_service_send_fake.call_count, 1);
  zassert_equal_ptr(ipc_service_send_fake.arg0_val, &ipc0_ept);
  expect_bytes(ipc_service_send_fake.arg1_val, ipc_service_send_fake.arg2_val, "01 08 00 00");
}

ZTEST(ipc_host, test_send_before_the_endpoint_is_bound_fails)
{
  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), -1);

  zassert_equal(ipc_service_send_fake.call_count, 0);
}

ZTEST(ipc_host, test_send_is_refused_in_the_low_power_modes)
{
  ipc_up();

  g1_state()->work_mode = 8;
  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), -1);
  g1_state()->work_mode = 9;
  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), -1);
  zassert_equal(ipc_service_send_fake.call_count, 0);

  g1_state()->work_mode = 0;
  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), 4);
  zassert_equal(ipc_service_send_fake.call_count, 1);
}

ZTEST(ipc_host, test_send_returns_the_endpoint_error)
{
  ipc_up();
  G1_FAKE_RETURN(ipc_service_send, -EIO);

  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), -EIO);
}

ZTEST(ipc_host, test_third_failed_send_in_a_row_reboots_the_lens)
{
  ipc_up();
  G1_FAKE_RETURN(ipc_service_send, -EIO);

  global_ipc_service_send(packet, sizeof(packet));
  global_ipc_service_send(packet, sizeof(packet));
  zassert_equal(system_reboot_delayed_fake.call_count, 0);
  global_ipc_service_send(packet, sizeof(packet));

  zassert_equal(system_reboot_delayed_fake.call_count, 1);
}

ZTEST(ipc_host, test_successful_send_clears_the_failure_count)
{
  ipc_up();
  G1_FAKE_RETURN(ipc_service_send, -EIO);
  global_ipc_service_send(packet, sizeof(packet));
  global_ipc_service_send(packet, sizeof(packet));
  G1_FAKE_RETURN(ipc_service_send, 4);
  zassert_equal(global_ipc_service_send(packet, sizeof(packet)), 4);
  G1_FAKE_RETURN(ipc_service_send, -EIO);
  global_ipc_service_send(packet, sizeof(packet));
  global_ipc_service_send(packet, sizeof(packet));

  zassert_equal(system_reboot_delayed_fake.call_count, 0);
}
