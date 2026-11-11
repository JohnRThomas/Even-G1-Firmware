/* Manager sector: magic, reserved, 4 x {state, start, end, ts, crc32} LE. */
#include <zephyr/sys/crc.h>

#include "fakes.h"

#include "sensors_audio/audio_stream.c"

#define BASE     0x421000u
#define BLOCK_SZ 0x20000u
#define MAGIC    0x5aa5aa5au

static uint8_t *const manager = &g1_fake_flash_image[0x400000];

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

/* a block record of the manager sector; the host is little-endian like the lens */
struct blk {
  uint32_t state, start, end, ts, crc;
};

static struct blk get_block(int index)
{
  struct blk block;

  memcpy(&block, manager + 8 + index * sizeof(block), sizeof(block));
  return block;
}

static void put_block(int index, struct blk block)
{
  memcpy(manager + 8 + index * sizeof(block), &block, sizeof(block));
}

static void put_manager(void)
{
  memset(manager, 0, 0x1000);
  sys_put_le32(MAGIC, manager);
}

static struct blk used(int index, uint32_t ts)
{
  return (struct blk){2, BASE + index * BLOCK_SZ, BASE + index * BLOCK_SZ + 0x100, ts,
          0x1234 + index};
}

/* as the storage thread does */
static void drain(void)
{
  for (int guard = 0; getAudioMessageQueueNum() && guard < 100; guard++) {
    audioStreamFileManagerHandler();
  }
  zassert_equal(getAudioMessageQueueNum(), 0, "queue not drained");
}

/* the audio of blocks first .. first + count - 1, AUDIO_STREAM_MSG_DATA bytes each */
static void fill_audio(uint8_t *data, int first, int count)
{
  for (int pos = 0; pos < count * AUDIO_STREAM_MSG_DATA; pos++) {
    int block = first + pos / AUDIO_STREAM_MSG_DATA;

    data[pos] = (uint8_t)(block * 31 + (pos % AUDIO_STREAM_MSG_DATA) * 7);
  }
}

static void record(int count)
{
  uint8_t data[AUDIO_STREAM_MSG_DATA];

  zassert_equal(startAudioStreamRecord(), 0);
  drain();
  for (int block = 0; block < count; block++) {
    fill_audio(data, block, 1);
    zassert_equal(sendAudioStram2Cache(data), 0, "block %d", block);
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
  struct audio_stream_msg msg;

  zassert_equal(startAudioStreamRecord(), 0);

  zassert_equal(getAudioMessageQueueNum(), 1);
  zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
  zassert_equal(k_msgq_get(&audio_stream_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.cmd, AUDIO_STREAM_START);
}

ZTEST(audio_stream, test_audio_data_is_queued_200_bytes_at_a_time)
{
  uint8_t data[AUDIO_STREAM_MSG_DATA];
  struct audio_stream_msg msg;

  fill_audio(data, 3, 1);
  zassert_equal(sendAudioStram2Cache(data), 0);

  zassert_equal(k_sem_count_get(&flash_ops_sem), 1);
  zassert_equal(k_msgq_get(&audio_stream_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.cmd, AUDIO_STREAM_DATA);
  zassert_equal(msg.len, 200);
  zassert_mem_equal(msg.data, data, 200);
}

ZTEST(audio_stream, test_stop_and_delete_requests_are_queued)
{
  struct audio_stream_msg msg;

  zassert_equal(stopAudioStreamRecord(), 0);
  zassert_equal(delAudioStreamRecord(3), 0);

  zassert_equal(k_msgq_get(&audio_stream_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.cmd, AUDIO_STREAM_STOP);
  zassert_equal(k_msgq_get(&audio_stream_msgq, &msg, K_NO_WAIT), 0);
  zassert_equal(msg.cmd, AUDIO_STREAM_DELETE);
  zassert_equal(msg.len, 1);
  zassert_equal(msg.data[0], 3);
  zassert_equal(k_sem_count_get(&flash_ops_sem), 2);
}

ZTEST(audio_stream, test_cache_queue_holds_18_messages_and_drops_the_19th)
{
  uint8_t data[AUDIO_STREAM_MSG_DATA] = {0};

  for (int index = 0; index < 18; index++) {
    zassert_equal(sendAudioStram2Cache(data), 0, "message %d", index);
  }
  zassert_equal(sendAudioStram2Cache(data), -1);
  zassert_equal(getAudioMessageQueueNum(), 18);
  zassert_equal(k_sem_count_get(&flash_ops_sem), 18);
  zassert_equal(stopAudioStreamRecord(), -1);
  zassert_equal(delAudioStreamRecord(1), -1);
}

/* sync 6 {1, 0}: quick note ends */
ZTEST(audio_stream, test_start_with_a_full_queue_ends_quick_note_on_both_lenses)
{
  uint8_t data[AUDIO_STREAM_MSG_DATA] = {0};

  for (int index = 0; index < 18; index++) {
    sendAudioStram2Cache(data);
  }
  zassert_equal(startAudioStreamRecord(), -1);

  zassert_equal(sync_to_slave_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.arg1_val, 6);
  expect_pkt(&g1_sync_to_slave_log, 0, "01 00");
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
  uint8_t data[AUDIO_STREAM_MSG_DATA] = {0};

  sendAudioStram2Cache(data);
  sendAudioStram2Cache(data);
  audioStreamFileManagerHandler();

  zassert_equal(getAudioMessageQueueNum(), 0);
  zassert_equal(g1_fake_flash_write_fake.call_count, 0);
  zassert_equal(audio_record_state, 0);
}

ZTEST(audio_stream, test_start_formats_a_blank_manager_sector)
{
  zassert_equal(startAudioStreamRecord(), 0);
  drain();

  zassert_equal(sys_get_le32(manager), MAGIC);
  zassert_equal(sys_get_le32(manager + 4), 0);
  expect_fill(manager + 8, 4 * sizeof(struct blk), 0x00, "cleared block records");
  zassert_equal(audio_record_state, 1, "recording");
  zassert_equal(audio_block_num, 0);
}

ZTEST(audio_stream, test_start_picks_the_first_free_block)
{
  uint8_t data[AUDIO_STREAM_MSG_DATA];
  struct blk block;

  put_manager();
  put_block(0, used(0, 100));
  put_block(1, used(1, 200));

  record(1);
  stopAudioStreamRecord();
  drain();

  block = get_block(2);
  zassert_equal(block.state, 2, "index 2 state %u", block.state);
  zassert_equal(block.start, BASE + 2 * BLOCK_SZ, "index 2 start 0x%x", block.start);
  fill_audio(data, 0, 1);
  zassert_mem_equal(&g1_fake_flash_image[BASE + 2 * BLOCK_SZ], data, sizeof(data));
  zassert_equal(get_block(3).state, 0, "index 3 written too");
}

/* the dates of 4 recordings, the oldest at index 1 */
static const uint32_t used_ts[4] = {500, 100, 300, 400};

/* every block holds one of used_ts; records one more at date 1717171717 */
static void record_with_all_blocks_used(void)
{
  put_manager();
  for (int index = 0; index < 4; index++) {
    put_block(index, used(index, used_ts[index]));
  }
  g1_clock_set_date(1717171717);

  record(1);
  stopAudioStreamRecord();
  drain();
}

/* the new recording replaced the one at new_index, the others are kept */
static void expect_new_recording_at(int new_index)
{
  for (int index = 0; index < 4; index++) {
    uint32_t want = index == new_index ? 1717171717 : used_ts[index];

    zassert_equal(get_block(index).ts, want, "index %d (ts %u): ts %u, want %u", index,
            used_ts[index], get_block(index).ts, want);
  }
}

ZTEST(audio_stream, test_all_blocks_used_picks_the_oldest)
{
  record_with_all_blocks_used();

  expect_new_recording_at(1);
}

ZTEST(audio_stream, test_oldest_block_in_use_is_skipped)
{
  setAudioBlockInUse(2); /* block 2 = index 1 */
  record_with_all_blocks_used();

  expect_new_recording_at(2);
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
  uint8_t expected[21 * AUDIO_STREAM_MSG_DATA];

  record(20);
  zassert_equal(g1_fake_flash_erase_fake.call_count, 1, "only the manager format");
  zassert_equal(audio_cache_offset, 4000);

  fill_audio(expected, 0, 21);
  sendAudioStram2Cache(&expected[20 * AUDIO_STREAM_MSG_DATA]); /* block 20 */
  drain();

  zassert_equal(g1_fake_flash_erase_fake.arg1_val, BASE, "erase at 0x%x",
          g1_fake_flash_erase_fake.arg1_val);
  zassert_equal(g1_fake_flash_erase_fake.arg2_val, 0x1000);
  zassert_mem_equal(&g1_fake_flash_image[BASE], expected, 0x1000);
  zassert_equal(audio_cache_offset, 21 * 200 - 0x1000, "remainder %d", audio_cache_offset);
  zassert_equal(audio_sector_count, 1);
}

ZTEST(audio_stream, test_stop_records_the_block_with_length_date_and_crc)
{
  static uint8_t all[25 * 200];
  struct blk block;

  put_manager();
  g1_clock_set_date(1717171717);
  record(25); /* one sector and 904 bytes */
  zassert_equal(stopAudioStreamRecord(), 0);
  drain();

  fill_audio(all, 0, 25);
  zassert_mem_equal(&g1_fake_flash_image[BASE], all, sizeof(all));
  block = get_block(0);
  zassert_equal(block.state, 2);
  zassert_equal(block.start, BASE);
  zassert_equal(block.end, BASE + 5000, "end 0x%x", block.end);
  zassert_equal(block.ts, 1717171717);
  zassert_equal(block.crc, crc32_ieee(all, sizeof(all)), "crc 0x%08x, expected 0x%08x",
          block.crc, crc32_ieee(all, sizeof(all)));
  zassert_equal(sys_get_le32(manager), MAGIC);
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
  zassert_equal(sys_get_le32(manager), MAGIC, "manager magic 0x%08x", sys_get_le32(manager));
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
  uint8_t data[AUDIO_STREAM_MSG_DATA];
  int recorded;

  put_manager();
  record(368); /* 17 sectors + 3968 cached */
  zassert_equal(audio_sector_count, 17);
  zassert_equal(sync_to_slave_fake.call_count, 0);

  fill_audio(data, 368, 1);
  sendAudioStram2Cache(data);
  drain();

  zassert_equal(sync_to_slave_fake.call_count, 1);
  zassert_equal(sync_to_slave_fake.arg1_val, 6);
  expect_pkt(&g1_sync_to_slave_log, 0, "01 00");
  zassert_equal(display_DelayClose_fake.arg0_val, 10000);
  recorded = get_block(0).end - BASE;
  zassert_equal(get_block(0).state, 2);
  zassert_equal(recorded, 73600, "%d bytes recorded", recorded);
  zassert_equal(audio_record_state, 0);
}

ZTEST(audio_stream, test_delete_frees_one_block)
{
  put_manager();
  for (int index = 0; index < 4; index++) {
    put_block(index, used(index, 100 + index));
  }

  delAudioStreamRecord(2);
  drain();

  zassert_equal(get_block(1).state, 0);
  zassert_equal(get_block(1).ts, 0);
  zassert_equal(get_block(0).state, 2);
  zassert_equal(get_block(2).state, 2);
  zassert_equal(get_block(3).state, 2);
  zassert_equal(get_block(3).ts, 103);
  zassert_equal(sys_get_le32(manager), MAGIC);
}

ZTEST(audio_stream, test_delete_0xff_frees_all_blocks)
{
  put_manager();
  for (int index = 0; index < 4; index++) {
    put_block(index, used(index, 100 + index));
  }

  delAudioStreamRecord(0xff);
  drain();

  zassert_equal(sys_get_le32(manager), MAGIC);
  expect_fill(manager + 8, 4 * sizeof(struct blk), 0x00, "cleared block records");
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
