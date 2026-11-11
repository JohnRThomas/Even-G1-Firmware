/* 0x49: an acknowledged no-op POST command. */
#include <stddef.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_display.h"
#include "fakes_esb.h"
#include "features.h"

ZTEST(features, test_0x49_payload_is_ignored)
{
	g1_send_hex("49 06 00 01 02 03");

	expect_frame_padded("49 c9", 20);
	expect_no_frame();
}

/* shares the 0x4D path, but only 0x4D gives tx_sem */
ZTEST(features, test_0x49_does_not_mark_the_session_as_up)
{
	g1_send_hex("49");

	zassert_equal(k_sem_count_get(&g1_ble()->tx_sem), 0);
}

/* ble_context changes with the request itself */
ZTEST(features, test_0x49_changes_no_state)
{
	static struct GlassesState before;
	const size_t ctx_at = offsetof(struct GlassesState, ble_context);
	const size_t ctx_end = ctx_at + sizeof(struct ble_context);
	const uint8_t *now = (const uint8_t *)g1_state();

	memcpy(&before, g1_state(), sizeof(before));

	g1_send_hex("49 06 00 01 02 03");

	zassert_mem_equal(now, &before, ctx_at, "GlassesState changed before ble_context");
	zassert_mem_equal(now + ctx_end, (const uint8_t *)&before + ctx_end,
			  sizeof(before) - ctx_end, "GlassesState changed after ble_context");
	zassert_equal(update_persist_task_status_fake.call_count, 0);
	zassert_equal(g1_fake_ipc_send_fake.call_count, 0);
	zassert_equal(send_response_data_to_msgqueue_fake.call_count, 0);
}
