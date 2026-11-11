/* Packets are [receiver id][payload] in both directions. */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_ipc.h"
#include "fakes_misc.h"

#include "ipc/ipc_host.c"

#define IPC0 DEVICE_DT_GET(DT_NODELABEL(ipc0))

static int ipc0_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

DEVICE_DT_DEFINE(DT_NODELABEL(ipc0), ipc0_init, NULL, NULL, NULL, POST_KERNEL, 99, NULL);

struct received {
	int calls;
	struct GlassesState *gs;
	size_t len;
	uint8_t data[64];
};

static struct received rx_a, rx_b;

static void record(struct received *r, struct GlassesState *gs, const uint8_t *data, size_t len)
{
	r->calls++;
	r->gs = gs;
	r->len = len;
	memcpy(r->data, data, MIN(len, sizeof(r->data)));
}

static int recv_a(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	record(&rx_a, gs, data, len);
	return 0;
}

static int recv_b(struct GlassesState *gs, const uint8_t *data, size_t len)
{
	record(&rx_b, gs, data, len);
	return 0;
}

static struct ipc_ep_cfg ep_a = {.id = 1, .name = "cpuapp-hw-id", .recv_cb = recv_a};
static struct ipc_ep_cfg ep_b = {.id = 4, .name = "cpunet-esbs", .recv_cb = recv_b};

static void ipc_host_before(void *f)
{
	ARG_UNUSED(f);

	GLOBAL_IPC_SERVICE_CONTEXT = NULL;
	ipc_ready = 0;
	ipc_send_fail_cnt = 0;
	memset(&ipc0_ept, 0, sizeof(ipc0_ept));
	k_sem_reset(&ipc0_bound_sem);
	memset(&rx_a, 0, sizeof(rx_a));
	memset(&rx_b, 0, sizeof(rx_b));

	/* as init_context() does */
	register_ipc_service_context(&g1_state()->ipc_ctx);
}

ZTEST_SUITE(ipc_host, NULL, NULL, ipc_host_before, NULL, NULL);

static void ipc_up(void)
{
	g1_fake_ipc.bind_on_register = true;
	zassert_equal(serialization_init(), 0);
	zassert_equal(ipc_ready, 1);
	g1_pkt_log_clear(&g1_ipc_service_log);
}

static void make_send_fail(void)
{
	ipc_service_send_fake.custom_fake = NULL;
	ipc_service_send_fake.return_val = -EIO;
}

static void make_send_work(void)
{
	ipc_service_send_fake.custom_fake = NULL;
	ipc_service_send_fake.return_val = 4;
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

	for (int i = 0; i < IPC_RECEIVER_MAX; i++) {
		eps[i].id = i;
		zassert_equal(ctx->register_recv_cb(&eps[i]), 0, "receiver %d", i);
	}
	zassert_equal(ctx->register_recv_cb(&eps[IPC_RECEIVER_MAX]), -1);

	zassert_equal(ctx->count, IPC_RECEIVER_MAX);
	zassert_equal_ptr(ctx->receivers[IPC_RECEIVER_MAX - 1], &eps[IPC_RECEIVER_MAX - 1]);
}

ZTEST(ipc_host, test_init_opens_ipc0_and_registers_its_endpoint)
{
	g1_fake_ipc.bind_on_register = true;

	zassert_equal(serialization_init(), 0);

	zassert_equal(ipc_service_open_instance_fake.call_count, 1);
	zassert_equal_ptr(ipc_service_open_instance_fake.arg0_val, IPC0);
	zassert_equal(ipc_service_register_endpoint_fake.call_count, 1);
	zassert_equal_ptr(ipc_service_register_endpoint_fake.arg0_val, IPC0);
	zassert_not_null(g1_fake_ipc.cfg);
	zassert_equal(strcmp(g1_fake_ipc.cfg->name, "ipc0"), 0);
	zassert_not_null(g1_fake_ipc.cfg->cb.bound);
	zassert_not_null(g1_fake_ipc.cfg->cb.received);
	zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_init_accepts_an_instance_that_is_already_open)
{
	ipc_service_open_instance_fake.custom_fake = NULL;
	ipc_service_open_instance_fake.return_val = -EALREADY;
	g1_fake_ipc.bind_on_register = true;

	zassert_equal(serialization_init(), 0);

	zassert_equal(ipc_ready, 1);
}

ZTEST(ipc_host, test_init_fails_when_ipc0_cannot_be_opened)
{
	ipc_service_open_instance_fake.custom_fake = NULL;
	ipc_service_open_instance_fake.return_val = -EIO;

	zassert_equal(serialization_init(), -1);

	zassert_equal(ipc_service_register_endpoint_fake.call_count, 0);
	zassert_equal(ipc_ready, 0);
}

ZTEST(ipc_host, test_init_fails_when_the_endpoint_cannot_be_registered)
{
	ipc_service_register_endpoint_fake.custom_fake = NULL;
	ipc_service_register_endpoint_fake.return_val = -EINVAL;

	zassert_equal(serialization_init(), -1);

	zassert_equal(ipc_ready, 0);
}

static K_THREAD_STACK_DEFINE(init_stack, 4096);
static struct k_thread init_thread;
static volatile int init_ret;
static volatile bool init_done;

static void init_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
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
	tid = k_thread_create(&init_thread, init_stack, K_THREAD_STACK_SIZEOF(init_stack),
			      init_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_sleep(K_MSEC(1000));
	done_before_bind = init_done;
	sent_before_bind = global_ipc_service_send((uint8_t[]){1, 8, 0, 0}, 4);

	zassert_equal(g1_fake_ipc_bind(), 0);
	joined = k_thread_join(tid, K_SECONDS(1));
	if (joined != 0) {
		k_thread_abort(tid);
	}

	zassert_false(done_before_bind, "serialization_init() did not wait for the bind");
	zassert_equal(sent_before_bind, -1, "sent before the endpoint was bound");
	zassert_equal(joined, 0);
	zassert_equal(init_ret, 0);
	zassert_equal(ipc_ready, 1);
}

/* Skipped: without a bind it spins in while (1), which stops native_sim time. */
ZTEST(ipc_host, test_init_reboots_when_the_network_core_never_binds)
{
	ztest_test_skip();

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

	zassert_equal(g1_fake_ipc_deliver(pkt, sizeof(pkt)), 0);

	zassert_equal(rx_a.calls, 0);
	zassert_equal(rx_b.calls, 1);
	zassert_equal_ptr(rx_b.gs, g1_state());
	zassert_equal(rx_b.len, 3);
	zassert_mem_equal(rx_b.data, &pkt[1], 3);
}

ZTEST(ipc_host, test_packet_for_an_unknown_receiver_is_dropped)
{
	struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
	static const uint8_t pkt[] = {0x09, 1, 2, 3, 4, 5, 6, 7};

	ctx->register_recv_cb(&ep_a);
	ctx->register_recv_cb(&ep_b);
	ipc_up();

	zassert_equal(g1_fake_ipc_deliver(pkt, sizeof(pkt)), 0);

	zassert_equal(rx_a.calls, 0);
	zassert_equal(rx_b.calls, 0);
}

/* DEFECT: len is not checked, so an empty packet reaches the receiver with len SIZE_MAX. */
ZTEST_EXPECT_FAIL(ipc_host, test_empty_packet_is_dropped);
ZTEST(ipc_host, test_empty_packet_is_dropped)
{
	struct ipc_service_context *ctx = &g1_state()->ipc_ctx;
	static const uint8_t stale[sizeof(rx_b.data) + 1] = {0x04};

	ctx->register_recv_cb(&ep_b);
	ipc_up();

	zassert_equal(g1_fake_ipc_deliver(stale, 0), 0);

	zassert_equal(rx_b.calls, 0, "receiver called with length %zu", rx_b.len);
}

ZTEST(ipc_host, test_send_hands_the_packet_to_the_endpoint_unchanged)
{
	static const uint8_t msg[4] = {1, 8, 0, 0};

	ipc_up();

	zassert_equal(g1_state()->ipc_ctx.send(msg, sizeof(msg)), 4);

	zassert_equal(ipc_service_send_fake.call_count, 1);
	zassert_equal_ptr(ipc_service_send_fake.arg0_val, &ipc0_ept);
	zassert_equal(g1_ipc_service_log.count, 1);
	zassert_equal(g1_ipc_service_log.len[0], 4);
	zassert_mem_equal(g1_ipc_service_log.data[0], msg, 4);
}

ZTEST(ipc_host, test_send_before_the_endpoint_is_bound_fails)
{
	zassert_equal(global_ipc_service_send((uint8_t[]){1, 8, 0, 0}, 4), -1);

	zassert_equal(ipc_service_send_fake.call_count, 0);
}

ZTEST(ipc_host, test_send_is_refused_in_the_low_power_modes)
{
	static const uint8_t msg[4] = {1, 8, 0, 0};

	ipc_up();

	g1_state()->work_mode = 8;
	zassert_equal(global_ipc_service_send(msg, 4), -1);
	g1_state()->work_mode = 9;
	zassert_equal(global_ipc_service_send(msg, 4), -1);
	zassert_equal(ipc_service_send_fake.call_count, 0);

	g1_state()->work_mode = 0;
	zassert_equal(global_ipc_service_send(msg, 4), 4);
	zassert_equal(ipc_service_send_fake.call_count, 1);
}

ZTEST(ipc_host, test_send_returns_the_endpoint_error)
{
	ipc_up();
	make_send_fail();

	zassert_equal(global_ipc_service_send((uint8_t[]){1, 8, 0, 0}, 4), -EIO);
}

ZTEST(ipc_host, test_third_failed_send_in_a_row_reboots_the_lens)
{
	static const uint8_t msg[4] = {1, 8, 0, 0};

	ipc_up();
	make_send_fail();

	global_ipc_service_send(msg, 4);
	global_ipc_service_send(msg, 4);
	zassert_equal(system_reboot_delayed_fake.call_count, 0);
	global_ipc_service_send(msg, 4);

	zassert_equal(system_reboot_delayed_fake.call_count, 1);
}

ZTEST(ipc_host, test_successful_send_clears_the_failure_count)
{
	static const uint8_t msg[4] = {1, 8, 0, 0};

	ipc_up();
	make_send_fail();
	global_ipc_service_send(msg, 4);
	global_ipc_service_send(msg, 4);
	make_send_work();
	zassert_equal(global_ipc_service_send(msg, 4), 4);
	make_send_fail();
	global_ipc_service_send(msg, 4);
	global_ipc_service_send(msg, 4);

	zassert_equal(system_reboot_delayed_fake.call_count, 0);
}
