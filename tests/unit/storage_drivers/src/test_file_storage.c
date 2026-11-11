#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>

#include "harness.h"
#include "fakes_display.h"
#include "nor_flash.h"

#include "storage/file_storage.c"

#define FILE_ADDR 0x300000

static void file_storage_before(void *fixture)
{
	ARG_UNUSED(fixture);
	file_addr = 0;
	file_buf_len = 0;
	file_type = 0;
}

ZTEST_SUITE(file_storage, NULL, NULL, file_storage_before, NULL, NULL);

/* ---- queue ---- */

ZTEST(file_storage, test_queue_returns_messages_in_order_with_their_length)
{
	static const uint8_t a[3] = { 1, 2, 3 };
	static const uint8_t b[200] = { 4 };
	struct file_msg m;

	zassert_ok(enqueue_file(a, sizeof(a)));
	zassert_ok(enqueue_file(b, sizeof(b)));

	zassert_ok(dequeue_file(&m));
	zassert_equal(m.len, 3);
	zassert_mem_equal(m.data, a, 3);
	zassert_ok(dequeue_file(&m));
	zassert_equal(m.len, 200);
	zassert_mem_equal(m.data, b, 200);
	zassert_equal(dequeue_file(&m), -ENOMSG);
}

ZTEST(file_storage, test_full_queue_drops_the_oldest_message)
{
	struct file_msg m;

	for (uint8_t i = 0; i < 31; i++) {
		zassert_ok(enqueue_file(&i, 1));
	}
	for (uint8_t i = 1; i <= 30; i++) {
		zassert_ok(dequeue_file(&m));
		zassert_equal(m.data[0], i, "got %u, want %u", m.data[0], i);
	}
	zassert_equal(dequeue_file(&m), -ENOMSG);
}

/* DEFECT: no length check; 255 bytes overflow enqueue_file()'s stack frame. Skipped. */
ZTEST(file_storage, test_enqueue_refuses_more_than_200_bytes)
{
	static uint8_t big[255];

	ztest_test_skip();
	zassert_not_equal(enqueue_file(big, sizeof(big)), 0);
}

/* ---- upload assembly ---- */

static uint8_t payload[0x1200];

static void make_payload(size_t n)
{
	for (size_t k = 0; k < n; k++) {
		payload[k] = (uint8_t)(k * 7 + 3);
	}
}

static void chunk_first(uint32_t addr, const uint8_t *data, size_t n)
{
	uint8_t msg[FILE_MSG_LEN];

	msg[0] = 0x01;
	sys_put_be32(addr, &msg[1]);
	memcpy(&msg[5], data, n);
	zassert_ok(enqueue_file(msg, 5 + n));
	try_to_save_file(g1_state());
}

static void chunk_next(uint8_t id, const uint8_t *data, size_t n)
{
	uint8_t msg[FILE_MSG_LEN];

	msg[0] = id;
	memcpy(&msg[1], data, n);
	zassert_ok(enqueue_file(msg, 1 + n));
	try_to_save_file(g1_state());
}

static void chunk_end(void)
{
	static const uint8_t end[2] = { 0x0d, 0x0e };

	zassert_ok(enqueue_file(end, sizeof(end)));
	try_to_save_file(g1_state());
}

/* LAST_FILE_CRC: zlib.crc32 of the 4 address bytes + the data */
ZTEST(file_storage, test_small_upload_is_stored_at_its_address_with_its_crc)
{
	chunk_first(FILE_ADDR, (const uint8_t *)"123456789abc", 12);
	zassert_equal(g1_nor_stats()->calls[G1_NOR_WRITE], 0, "written before the end");

	chunk_end();

	zassert_mem_equal(g1_nor_mem(FILE_ADDR, 12), "123456789abc", 12);
	zassert_equal(*g1_nor_mem(FILE_ADDR + 12, 1), 0xff);
	zassert_equal(LAST_FILE_CRC, 0xe679ec0f, "crc 0x%08x", LAST_FILE_CRC);
}

ZTEST(file_storage, test_upload_is_written_in_4_kib_blocks_and_the_tail_at_the_end)
{
	make_payload(22 * 192);
	chunk_first(FILE_ADDR, payload, 192);
	for (int i = 1; i < 22; i++) {
		chunk_next((uint8_t)(i + 1), &payload[i * 192], 192);
	}

	zassert_equal(g1_nor_stats()->calls[G1_NOR_WRITE], 1);
	zassert_mem_equal(g1_nor_mem(FILE_ADDR, 0x1000), payload, 0x1000);
	zassert_equal(file_addr, FILE_ADDR + 0x1000);
	zassert_equal(file_buf_len, 128);

	chunk_end();

	zassert_mem_equal(g1_nor_mem(FILE_ADDR + 0x1000, 128), &payload[0x1000], 128);
	zassert_equal(LAST_FILE_CRC, 0xb14f3a7b, "crc 0x%08x", LAST_FILE_CRC);
}

ZTEST(file_storage, test_demo_image_upload_starts_the_demo_task)
{
	static const uint8_t data[4] = { 1, 2, 3, 4 };

	chunk_first(0x1c0000, data, sizeof(data));

	zassert_equal(g1_state()->dash_buf->type, 0x0c);
	zassert_equal(g1_state()->dash_buf->data[0], 10);
	zassert_equal(update_persist_task_status_fake.call_count, 1);
	zassert_equal_ptr(update_persist_task_status_fake.arg0_val, g1_state());
	zassert_equal(update_persist_task_status_fake.arg1_val, 0x10);
	zassert_equal(update_persist_task_status_fake.arg2_val, 2);
}

ZTEST(file_storage, test_demo_image_upload_while_the_demo_task_runs_does_not_restart_it)
{
	static const uint8_t data[4] = { 1, 2, 3, 4 };

	g1_state()->task_status->task = 0x10;
	g1_state()->dash_buf->data[0] = 3;

	chunk_first(0x1c0000, data, sizeof(data));

	zassert_equal(g1_state()->dash_buf->type, 0x0c);
	zassert_equal(g1_state()->dash_buf->data[0], 3);
	zassert_equal(update_persist_task_status_fake.call_count, 0);
}

/* the demo region ends at 0x200000 */
static void fill_demo_region(void)
{
	make_payload(192);
	chunk_first(0x1c0000, payload, 192);
	for (int i = 0; file_addr < 0x200000; i++) {
		zassert_true(i < 2000, "demo region never filled");
		chunk_next((uint8_t)i, payload, 192);
	}
}

ZTEST(file_storage, test_demo_image_data_past_the_region_is_not_written)
{
	fill_demo_region();

	for (int i = 0; i < 3; i++) {
		chunk_next(0x55, payload, 192);
	}
	chunk_end();

	zassert_equal(g1_nor_stats()->last_addr[G1_NOR_WRITE], 0x200000 - 0x1000);
	zassert_equal(*g1_nor_mem(0x200000, 1), 0xff);
}

/* DEFECT: chunks past the region are still appended to file_buf, which overflows. */
ZTEST_EXPECT_FAIL(file_storage, test_demo_image_data_past_the_region_is_not_buffered);
ZTEST(file_storage, test_demo_image_data_past_the_region_is_not_buffered)
{
	uint32_t len;

	fill_demo_region();
	len = file_buf_len;

	for (int i = 0; i < 3; i++) {
		chunk_next(0x55, payload, 192);
	}

	zassert_equal(file_buf_len, len, "buffered %u bytes past the region",
		      file_buf_len - len);
}

/* ---- commit request ---- */

ZTEST(file_storage, test_commit_request_sets_the_command_and_wakes_the_commit_thread)
{
	storage_commit_request(6, 0);

	zassert_equal(g1_state()->dashboard_cmd, 6);
	zassert_equal(sys_settings_dirty, 0);
	zassert_equal(k_sem_count_get(&g1_state()->dashboard_position_sem), 1);
}

ZTEST(file_storage, test_commit_requests_coalesce_with_the_newest_command_and_sticky_dirty)
{
	storage_commit_request(6, 1);
	storage_commit_request(3, 0);

	zassert_equal(g1_state()->dashboard_cmd, 3);
	zassert_equal(sys_settings_dirty, 1);
	zassert_equal(k_sem_count_get(&g1_state()->dashboard_position_sem), 1);
}
