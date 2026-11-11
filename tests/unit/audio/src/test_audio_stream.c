/* Manager sector: magic, reserved, 4 x {state, start, end, ts, crc32} LE. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_audio.h"
#include "fakes_display.h"
#include "fakes_ipc.h"
#include "fakes_storage.h"

#include "sensors_audio/audio_stream.c"

#define MGR      0x400000u
#define BASE     0x421000u
#define BLOCK_SZ 0x20000u
#define MAGIC    0x5aa5aa5au

static void stream_before(void *fixture)
{
	ARG_UNUSED(fixture);

	k_msgq_purge(&audio_stream_msgq);
	audio_stream_crc = 0;
	audio_cache_offset = 0;
	audio_sector_count = 0;
	atomic_clear(&audio_block_in_use);
	audio_record_state = 0;
	audio_block_num = 0;
	memset(audio_cache, 0, sizeof(audio_cache));
}

ZTEST_SUITE(audio_stream, NULL, NULL, stream_before, NULL, NULL);

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;

	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) {
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
		}
	}
	return ~c;
}

static uint32_t get_le32(uint32_t addr)
{
	const uint8_t *p = &g1_fake_flash_image[addr];

	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint32_t addr, uint32_t v)
{
	g1_fake_flash_image[addr] = v;
	g1_fake_flash_image[addr + 1] = v >> 8;
	g1_fake_flash_image[addr + 2] = v >> 16;
	g1_fake_flash_image[addr + 3] = v >> 24;
}

struct blk {
	uint32_t state, start, end, ts, crc;
};

static struct blk get_block(int i)
{
	uint32_t a = MGR + 8 + i * 20;

	return (struct blk){get_le32(a), get_le32(a + 4), get_le32(a + 8), get_le32(a + 12),
			    get_le32(a + 16)};
}

static void put_block(int i, struct blk b)
{
	uint32_t a = MGR + 8 + i * 20;

	put_le32(a, b.state);
	put_le32(a + 4, b.start);
	put_le32(a + 8, b.end);
	put_le32(a + 12, b.ts);
	put_le32(a + 16, b.crc);
}

static void put_manager(void)
{
	memset(&g1_fake_flash_image[MGR], 0, 0x1000);
	put_le32(MGR, MAGIC);
}

static struct blk used(int i, uint32_t ts)
{
	return (struct blk){2, BASE + i * BLOCK_SZ, BASE + i * BLOCK_SZ + 0x100, ts, 0x1234 + i};
}

/* as the storage thread does */
static void drain(void)
{
	for (int guard = 0; getAudioMessageQueueNum() && guard < 100; guard++) {
		audioStreamFileManagerHandler();
	}
	zassert_equal(getAudioMessageQueueNum(), 0, "queue not drained");
}

static void fill_block(uint8_t *b, int n)
{
	for (int i = 0; i < AUDIO_STREAM_MSG_DATA; i++) {
		b[i] = (uint8_t)(n * 31 + i * 7);
	}
}

static void record(int count)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA];

	zassert_equal(startAudioStreamRecord(), 0);
	drain();
	for (int n = 0; n < count; n++) {
		fill_block(b, n);
		zassert_equal(sendAudioStram2Cache(b), 0, "block %d", n);
		drain();
	}
}

/* main() clears bit 2 only when the stream ends */
ZTEST(audio_stream, test_request_dmic_stream_start_wakes_main_once)
{
	struct GlassesState *gs = g1_state();

	request_dmic_stream_start();
	zassert_true(gs->pending & BIT(2));
	zassert_equal(k_sem_take(&gs->main_sem, K_NO_WAIT), 0, "main_sem given");

	request_dmic_stream_start();
	zassert_equal(k_sem_count_get(&gs->main_sem), 0, "given again while pending");
	zassert_true(gs->pending & BIT(2));
}

ZTEST(audio_stream, test_start_record_queues_a_start_and_wakes_the_storage_thread)
{
	struct audio_stream_msg m;

	zassert_equal(startAudioStreamRecord(), 0);

	zassert_equal(getAudioMessageQueueNum(), 1);
	zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
	zassert_equal(k_msgq_get(&audio_stream_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.cmd, AUDIO_STREAM_START);
}

ZTEST(audio_stream, test_audio_data_is_queued_200_bytes_at_a_time)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA];
	struct audio_stream_msg m;

	fill_block(b, 3);
	zassert_equal(sendAudioStram2Cache(b), 0);

	zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
	zassert_equal(k_msgq_get(&audio_stream_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.cmd, AUDIO_STREAM_DATA);
	zassert_equal(m.len, 200);
	zassert_mem_equal(m.data, b, 200);
}

ZTEST(audio_stream, test_stop_and_delete_requests_are_queued)
{
	struct audio_stream_msg m;

	zassert_equal(stopAudioStreamRecord(), 0);
	zassert_equal(delAudioStreamRecord(3), 0);

	zassert_equal(k_msgq_get(&audio_stream_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.cmd, AUDIO_STREAM_STOP);
	zassert_equal(k_msgq_get(&audio_stream_msgq, &m, K_NO_WAIT), 0);
	zassert_equal(m.cmd, AUDIO_STREAM_DELETE);
	zassert_equal(m.len, 1);
	zassert_equal(m.data[0], 3);
	zassert_equal(k_sem_count_get(&flash_ops_sem), 2);
}

ZTEST(audio_stream, test_cache_queue_holds_18_messages_and_drops_the_19th)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA] = {0};

	for (int i = 0; i < 18; i++) {
		zassert_equal(sendAudioStram2Cache(b), 0, "message %d", i);
	}
	zassert_equal(sendAudioStram2Cache(b), -1);
	zassert_equal(getAudioMessageQueueNum(), 18);
	zassert_equal(k_sem_count_get(&flash_ops_sem), 18);
	zassert_equal(stopAudioStreamRecord(), -1);
	zassert_equal(delAudioStreamRecord(1), -1);
}

/* sync 6 {1, 0}: quick note ends */
ZTEST(audio_stream, test_start_with_a_full_queue_ends_quick_note_on_both_lenses)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA] = {0};

	for (int i = 0; i < 18; i++) {
		sendAudioStram2Cache(b);
	}
	zassert_equal(startAudioStreamRecord(), -1);

	zassert_equal(sync_to_slave_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.arg1_val, 6);
	zassert_equal(g1_sync_to_slave_log.len[0], 2);
	zassert_mem_equal(g1_sync_to_slave_log.data[0], ((uint8_t[]){0x01, 0x00}), 2);
	zassert_equal(display_DelayClose_fake.call_count, 1);
	zassert_equal(display_DelayClose_fake.arg0_val, 10000);
}

ZTEST(audio_stream, test_handler_with_an_empty_queue_touches_no_flash)
{
	zassert_equal(audioStreamFileManagerHandler(), 0);
	zassert_equal(g1_fake_flash_read_fake.call_count, 0);
	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
	zassert_equal(g1_fake_flash_write_fake.call_count, 0);
}

ZTEST(audio_stream, test_data_without_a_start_is_discarded)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA] = {0};

	sendAudioStram2Cache(b);
	sendAudioStram2Cache(b);
	audioStreamFileManagerHandler();

	zassert_equal(getAudioMessageQueueNum(), 0);
	zassert_equal(g1_fake_flash_write_fake.call_count, 0);
	zassert_equal(audio_record_state, 0);
}

ZTEST(audio_stream, test_start_formats_a_blank_manager_sector)
{
	zassert_equal(startAudioStreamRecord(), 0);
	drain();

	zassert_equal(get_le32(MGR), MAGIC);
	zassert_equal(get_le32(MGR + 4), 0);
	for (int i = 0; i < 4; i++) {
		struct blk b = get_block(i);

		zassert_true(b.state == 0 && b.start == 0 && b.end == 0 && b.ts == 0 && b.crc == 0,
			     "block %d not cleared", i);
	}
	zassert_equal(audio_record_state, 1, "recording");
	zassert_equal(audio_block_num, 0);
}

ZTEST(audio_stream, test_start_picks_the_first_free_block)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA];
	struct blk blk;

	put_manager();
	put_block(0, used(0, 100));
	put_block(1, used(1, 200));

	record(1);
	stopAudioStreamRecord();
	drain();

	blk = get_block(2);
	zassert_equal(blk.state, 2, "index 2 state %u", blk.state);
	zassert_equal(blk.start, BASE + 2 * BLOCK_SZ, "index 2 start 0x%x", blk.start);
	fill_block(b, 0);
	zassert_mem_equal(&g1_fake_flash_image[BASE + 2 * BLOCK_SZ], b, sizeof(b));
	zassert_equal(get_block(3).state, 0, "index 3 written too");
}

ZTEST(audio_stream, test_all_blocks_used_picks_the_oldest)
{
	static const uint32_t ts[4] = {500, 100, 300, 400};

	put_manager();
	for (int i = 0; i < 4; i++) {
		put_block(i, used(i, ts[i]));
	}
	g1_clock_set_date(1717171717);

	record(1);
	stopAudioStreamRecord();
	drain();

	for (int i = 0; i < 4; i++) {
		uint32_t want = i == 1 ? 1717171717 : ts[i];

		zassert_equal(get_block(i).ts, want, "index %d (ts %u): ts %u, want %u", i, ts[i],
			      get_block(i).ts, want);
	}
}

ZTEST(audio_stream, test_oldest_block_in_use_is_skipped)
{
	static const uint32_t ts[4] = {500, 100, 300, 400};

	put_manager();
	for (int i = 0; i < 4; i++) {
		put_block(i, used(i, ts[i]));
	}
	setAudioBlockInUse(2); /* block 2 = index 1 */
	g1_clock_set_date(1717171717);

	record(1);
	stopAudioStreamRecord();
	drain();

	for (int i = 0; i < 4; i++) {
		uint32_t want = i == 2 ? 1717171717 : ts[i];

		zassert_equal(get_block(i).ts, want, "index %d (ts %u): ts %u, want %u", i, ts[i],
			      get_block(i).ts, want);
	}
}

ZTEST(audio_stream, test_in_use_flag_set_get_clear)
{
	zassert_equal(getAudioBlockInUse(), 0, "none at boot");
	zassert_equal(setAudioBlockInUse(3), 0, "returns the previous value");
	zassert_equal(getAudioBlockInUse(), 3);
	zassert_equal(clearAudioBlockInUse(), 3);
	zassert_equal(getAudioBlockInUse(), 0);
}

ZTEST(audio_stream, test_audio_is_written_in_4_kib_sectors)
{
	uint8_t exp[0x1000];
	uint8_t b[AUDIO_STREAM_MSG_DATA];

	record(20);
	zassert_equal(g1_fake_flash_erase_fake.call_count, 1, "only the manager format");
	zassert_equal(audio_cache_offset, 4000);

	fill_block(b, 20);
	sendAudioStram2Cache(b);
	drain();

	zassert_equal(g1_fake_flash_erase_fake.arg1_val, BASE, "erase at 0x%x",
		      g1_fake_flash_erase_fake.arg1_val);
	zassert_equal(g1_fake_flash_erase_fake.arg2_val, 0x1000);
	for (int n = 0; n < 21; n++) {
		fill_block(b, n);
		memcpy(&exp[n * 200], b, MIN(200, 0x1000 - n * 200));
	}
	zassert_mem_equal(&g1_fake_flash_image[BASE], exp, 0x1000);
	zassert_equal(audio_cache_offset, 21 * 200 - 0x1000, "remainder %d", audio_cache_offset);
	zassert_equal(audio_sector_count, 1);
}

ZTEST(audio_stream, test_stop_records_the_block_with_length_date_and_crc)
{
	static uint8_t all[25 * 200];
	uint8_t b[AUDIO_STREAM_MSG_DATA];
	struct blk blk;

	put_manager();
	g1_clock_set_date(1717171717);
	record(25); /* one sector and 904 bytes */
	zassert_equal(stopAudioStreamRecord(), 0);
	drain();

	for (int n = 0; n < 25; n++) {
		fill_block(b, n);
		memcpy(&all[n * 200], b, 200);
	}
	zassert_mem_equal(&g1_fake_flash_image[BASE], all, sizeof(all));
	blk = get_block(0);
	zassert_equal(blk.state, 2);
	zassert_equal(blk.start, BASE);
	zassert_equal(blk.end, BASE + 5000, "end 0x%x", blk.end);
	zassert_equal(blk.ts, 1717171717);
	zassert_equal(blk.crc, crc32_ref(all, sizeof(all)), "crc 0x%08x, expected 0x%08x", blk.crc,
		      crc32_ref(all, sizeof(all)));
	zassert_equal(get_le32(MGR), MAGIC);
	zassert_equal(requestAudioInfoToApp_fake.call_count, 1);
	zassert_equal(requestAudioInfoToApp_fake.arg0_val, 1);
	zassert_equal(audio_record_state, 0, "idle again");
}

ZTEST(audio_stream, test_stop_keeps_the_other_recordings)
{
	put_manager();
	put_block(0, used(0, 100));
	put_block(2, used(2, 300));

	record(3);
	stopAudioStreamRecord();
	drain();

	zassert_equal(get_block(1).state, 2, "new recording in the first free block");
	zassert_equal(get_block(0).ts, 100);
	zassert_equal(get_block(0).crc, used(0, 100).crc);
	zassert_equal(get_block(2).ts, 300);
	zassert_equal(get_block(3).state, 0);
}

/* DEFECT: an empty stop erases the manager sector without writing it back. */
ZTEST_EXPECT_FAIL(audio_stream, test_stopping_an_empty_recording_keeps_the_other_recordings);
ZTEST(audio_stream, test_stopping_an_empty_recording_keeps_the_other_recordings)
{
	put_manager();
	put_block(1, used(1, 200));

	record(0);
	stopAudioStreamRecord();
	drain();

	zassert_equal(audio_record_state, 0);
	zassert_equal(requestAudioInfoToApp_fake.call_count, 0, "nothing recorded, nothing told");
	zassert_equal(get_le32(MGR), MAGIC, "manager magic 0x%08x", get_le32(MGR));
	zassert_equal(get_block(1).state, 2, "recording in block 2 lost");
}

ZTEST(audio_stream, test_start_during_recording_abandons_it)
{
	put_manager();
	record(3);

	startAudioStreamRecord();
	drain();

	zassert_equal(audio_record_state, 0);
	zassert_equal(get_block(0).state, 0);
	zassert_equal(requestAudioInfoToApp_fake.call_count, 0);
}

/* 18th sector: past the block area */
ZTEST(audio_stream, test_recording_stops_itself_at_the_end_of_its_block_area)
{
	uint8_t b[AUDIO_STREAM_MSG_DATA];
	int n;

	put_manager();
	record(368); /* 17 sectors + 3968 cached */
	zassert_equal(audio_sector_count, 17);
	zassert_equal(sync_to_slave_fake.call_count, 0);

	fill_block(b, 368);
	sendAudioStram2Cache(b);
	drain();

	zassert_equal(sync_to_slave_fake.call_count, 1);
	zassert_equal(sync_to_slave_fake.arg1_val, 6);
	zassert_mem_equal(g1_sync_to_slave_log.data[0], ((uint8_t[]){0x01, 0x00}), 2);
	zassert_equal(display_DelayClose_fake.arg0_val, 10000);
	n = get_block(0).end - BASE;
	zassert_equal(get_block(0).state, 2);
	zassert_equal(n, 73600, "%d bytes recorded", n);
	zassert_equal(audio_record_state, 0);
}

ZTEST(audio_stream, test_delete_frees_one_block)
{
	put_manager();
	for (int i = 0; i < 4; i++) {
		put_block(i, used(i, 100 + i));
	}

	delAudioStreamRecord(2);
	drain();

	zassert_equal(get_block(1).state, 0);
	zassert_equal(get_block(1).ts, 0);
	zassert_equal(get_block(0).state, 2);
	zassert_equal(get_block(2).state, 2);
	zassert_equal(get_block(3).state, 2);
	zassert_equal(get_block(3).ts, 103);
	zassert_equal(get_le32(MGR), MAGIC);
}

ZTEST(audio_stream, test_delete_0xff_frees_all_blocks)
{
	put_manager();
	for (int i = 0; i < 4; i++) {
		put_block(i, used(i, 100 + i));
	}

	delAudioStreamRecord(0xff);
	drain();

	zassert_equal(get_le32(MGR), MAGIC);
	for (int i = 0; i < 4; i++) {
		struct blk b = get_block(i);

		zassert_true(b.state == 0 && b.start == 0 && b.end == 0 && b.ts == 0 && b.crc == 0,
			     "block %d not cleared", i);
	}
}

/* the app sends 1..4 or 0xff */
ZTEST(audio_stream, test_delete_of_block_0_or_5_changes_nothing)
{
	put_manager();
	put_block(0, used(0, 100));

	delAudioStreamRecord(0);
	delAudioStreamRecord(5);
	drain();

	zassert_equal(g1_fake_flash_erase_fake.call_count, 0);
	zassert_equal(g1_fake_flash_write_fake.call_count, 0);
	zassert_equal(get_block(0).state, 2);
}

ZTEST(audio_stream, test_delete_during_recording_deletes_and_stops)
{
	put_manager();
	put_block(3, used(3, 400));
	record(2);

	delAudioStreamRecord(4);
	drain();

	zassert_equal(audio_record_state, 0, "recording stopped");
	zassert_equal(get_block(3).state, 0, "block 4 deleted");
	zassert_equal(get_block(0).state, 0, "the stopped recording is not recorded");
}
