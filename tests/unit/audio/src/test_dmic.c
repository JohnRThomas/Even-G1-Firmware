/* dmic_stream_start() loops, so it runs in a thread; the test plays the PDM. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_ble_queue.h"
#include "fakes_misc.h"
#include "fakes_nrfx.h"
#include "pdm_driver.h"

#include "sensors_audio/dmic_audio.c"
#include "ble/ble_proto.h"

#define BLOCK_SAMPLES   1600 /* 100 ms at 16 kHz */
#define LC3_FRAMES      10
#define LC3_BYTES       20
#define F1_PAYLOAD      (LC3_FRAMES * LC3_BYTES)
#define PDMCLKCTRL_1280 168427520u /* nRF5340 PS Table 42 */

#define MIC_PRIORITY 0 /* as main() */

static K_THREAD_STACK_DEFINE(mic_stack, 16384);
static struct k_thread mic_thread;
static k_tid_t mic_tid;
static volatile bool mic_done;
static volatile int mic_ret;

static int16_t tone[BLOCK_SAMPLES];

static void mic_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	mic_ret = dmic_stream_start();
	mic_done = true;
}

static void mic_start(void)
{
	mic_done = false;
	mic_ret = 0x7fffffff;
	mic_tid = k_thread_create(&mic_thread, mic_stack, K_THREAD_STACK_SIZEOF(mic_stack),
				  mic_entry, NULL, NULL, NULL, MIC_PRIORITY, 0, K_NO_WAIT);
	g1_clock_advance_ms(1);
}

static void mic_block(const int16_t *pcm)
{
	zassert_equal(g1_fake_pdm_capture(pcm, BLOCK_SAMPLES), 0, "PDM not running");
	g1_clock_advance_ms(1);
}

/* the loop ends after the next block */
static int mic_stop(void)
{
	dmic_stream_stop();
	if (g1_fake_pdm.op_state == G1_PDM_RUNNING) {
		mic_block(NULL);
	}
	g1_clock_advance_ms(1);
	zassert_true(mic_done, "stream still running");
	return mic_ret;
}

static void dmic_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* 1 kHz tone at 16 kHz */
	static const int16_t period[16] = {0,     3061,  5657,  7391,  8000,  7391,
					   5657,  3061,  0,     -3061, -5657, -7391,
					   -8000, -7391, -5657, -3061};

	for (int i = 0; i < BLOCK_SAMPLES; i++) {
		tone[i] = period[i % 16];
	}
	dmic_stop_state = 1;
	audio_flash_offset = AUDIO_FLASH_START;
	dmic_send_pending = 0;
	atomic_clear(&audio_stream_recording);
	mic_tid = NULL;
	mic_done = true;
}

static void dmic_after(void *fixture)
{
	ARG_UNUSED(fixture);

	if (mic_tid && !mic_done) {
		dmic_stop_state = 1;
		if (g1_fake_pdm.op_state == G1_PDM_RUNNING) {
			(void)g1_fake_pdm_capture(NULL, 0);
		}
		g1_clock_advance_ms(1);
		if (!mic_done) {
			g1_clock_advance_ms(READ_TIMEOUT + 10);
		}
		if (!mic_done) {
			k_thread_abort(mic_tid);
		}
	}
	mic_tid = NULL;
	pdm_driver_reset();
	(void)clean_dmic_msgq();
}

ZTEST_SUITE(dmic, NULL, NULL, dmic_before, dmic_after, NULL);

/* 10 ms frames, 16 kHz, 20 bytes, S16, fresh encoder */
static void lc3_reference(const int16_t *pcm, uint8_t *out)
{
	unsigned int size = lc3_encoder_size(10000, 16000);
	void *mem = malloc(size);
	lc3_encoder_t enc;

	zassert_not_null(mem);
	enc = lc3_setup_encoder(10000, 16000, 0, mem);
	zassert_not_null(enc);
	for (int i = 0; i < LC3_FRAMES; i++) {
		zassert_equal(lc3_encode(enc, LC3_PCM_FORMAT_S16, pcm + i * 160, 1, LC3_BYTES,
					 out + i * LC3_BYTES),
			      0);
	}
	free(mem);
}

ZTEST(dmic, test_start_programs_a_1280_khz_pdm_clock_with_ratio_80_for_16_khz)
{
	mic_start();

	zassert_true(g1_fake_pdm.initialized);
	zassert_equal(NRF_PDM0->PDMCLKCTRL, PDMCLKCTRL_1280, "PDMCLKCTRL 0x%08x",
		      NRF_PDM0->PDMCLKCTRL);
	zassert_equal(NRF_PDM0->RATIO, 1, "RATIO %u (1 = Ratio80)", NRF_PDM0->RATIO);
	zassert_equal(NRF_PDM0->MCLKCONFIG, 0, "MCLKCONFIG %u (0 = PCLK32M)", NRF_PDM0->MCLKCONFIG);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_start_samples_one_microphone_on_the_left_channel)
{
	mic_start();

	/* Mono = 1, EDGE LeftFalling = 0 */
	zassert_equal(NRF_PDM0->MODE, 0x1, "MODE 0x%x", NRF_PDM0->MODE);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_gain_0x40_is_set_after_configure_and_before_start)
{
	int init, gain, start;

	mic_start();

	init = g1_fake_pdm_log_find(G1_PDM_OP_INIT, 0);
	gain = g1_fake_pdm_log_find(G1_PDM_OP_GAIN_SET, 0);
	start = g1_fake_pdm_log_find(G1_PDM_OP_START, 0);
	zassert_true(init >= 0 && gain > init && start > gain, "init %d, gain %d, start %d", init,
		     gain, start);
	zassert_equal(g1_fake_pdm.log[gain].a, 0x40);
	zassert_equal(g1_fake_pdm.log[gain].b, 0x40);
	zassert_equal(nrf_pdm_gain_set_fake.arg0_val, NRF_PDM0);
	zassert_equal(g1_fake_pdm.gain_l_at_start, 0x40, "GAINL 0x%x", g1_fake_pdm.gain_l_at_start);
	zassert_equal(g1_fake_pdm.gain_r_at_start, 0x40, "GAINR 0x%x", g1_fake_pdm.gain_r_at_start);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_dma_buffers_are_100_ms_blocks)
{
	int i;

	mic_start();

	i = g1_fake_pdm_log_find(G1_PDM_OP_BUFFER_SET, 0);
	zassert_true(i >= 0);
	zassert_equal(g1_fake_pdm.log[i].b, BLOCK_SAMPLES, "%u samples",
		      (unsigned int)g1_fake_pdm.log[i].b);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_microphone_module_off_starts_nothing)
{
	g1_state()->module_mask &= ~BIT(1);

	zassert_equal(dmic_stream_start(), 0);
	zassert_equal(nrfx_pdm_init_fake.call_count, 0);
	zassert_equal(nrfx_pdm_start_fake.call_count, 0);
}

ZTEST(dmic, test_driver_configure_failure_ends_the_stream_without_starting)
{
	nrfx_pdm_init_fake.custom_fake = NULL;
	nrfx_pdm_init_fake.return_val = NRFX_ERROR_INTERNAL;

	zassert_equal(dmic_stream_start(), -1);
	zassert_equal(nrf_pdm_gain_set_fake.call_count, 0);
	zassert_equal(nrfx_pdm_start_fake.call_count, 0);
	zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT);
}

/* two blocks are held by the DMA double buffer */
ZTEST(dmic, test_blocks_rotate_through_the_slab_without_running_out)
{
	mic_start();
	zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT - 2);

	for (int i = 0; i < 3 * BLOCK_COUNT; i++) {
		mic_block(tone);
		zassert_equal(pdm_driver_queued(), 0, "block %d not read", i);
		zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT - 2,
			      "block %d: %u free", i, k_mem_slab_num_free_get(&dmic_mem_slab));
	}
	zassert_equal(g1_fake_pdm.captured_blocks, 3 * BLOCK_COUNT);
	zassert_equal(g1_fake_pdm_log_find(G1_PDM_OP_EVT_OVERFLOW, 0), -1, "PDM overflow");
	zassert_equal(nrfx_pdm_stop_fake.call_count, 0, "driver stopped the PDM");

	zassert_equal(mic_stop(), 0);
	zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT);
}

ZTEST(dmic, test_f1_frame_is_sequence_and_200_lc3_bytes)
{
	uint8_t exp[2 + F1_PAYLOAD] = {0xf1, 0x00};
	const uint8_t *f;
	size_t len;

	mic_start();
	mic_block(tone);

	zassert_equal(send_dmic_msg(g1_ble()), 0);

	zassert_equal(g1_frame_count(), 1);
	f = g1_frame_peek(0, &len);
	zassert_equal(len, 2 + F1_PAYLOAD, "F1 frame of %u bytes", (unsigned int)len);
	lc3_reference(tone, exp + 2);
	zassert_mem_equal(f, exp, sizeof(exp));
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_dmic_queue_keeps_the_newest_10_blocks)
{
	uint8_t blk[DMIC_MSG_SIZE];

	zassert_equal(init_dmic_msgq(), 0);
	for (int i = 0; i < 11; i++) {
		memset(blk, i, sizeof(blk));
		zassert_equal(enqueue_dmic(blk), 0, "block %d", i);
	}
	zassert_equal(get_dmic_num_messages(), 10);
	zassert_equal(dequeue_dmic(blk), 0);
	zassert_equal(blk[0], 1, "oldest kept block %d", blk[0]);
	zassert_equal(blk[DMIC_MSG_SIZE - 1], 1);
	zassert_equal(clean_dmic_msgq(), 0);
}

/* Woken once through rx_sem; while that is pending, "f1 00 cc" is queued instead. */
ZTEST(dmic, test_first_block_wakes_the_ble_thread_through_rx_sem)
{
	struct ble_context *ble = g1_ble();

	mic_start();
	mic_block(tone);

	zassert_equal(dmic_send_pending, 1);
	zassert_equal(k_sem_count_get(&ble->rx_sem), 1);
	zassert_equal(audio_bt_queue_log.count, 0);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_block_while_the_ble_thread_is_awake_queues_an_f1_request)
{
	mic_start();
	mic_block(tone);
	mic_block(tone); /* rx_sem still given */

	zassert_equal(audio_bt_queue_log.count, 1);
	zassert_equal(audio_bt_queue_log.len[0], 3);
	zassert_mem_equal(audio_bt_queue_log.data[0], ((uint8_t[]){0xf1, 0x00, 0xcc}), 3);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_stop_ends_the_stream_after_the_next_block_and_stops_the_pdm)
{
	mic_start();
	mic_block(tone);
	zassert_equal(dmic_get_stop_state(), 0, "running");

	dmic_stream_stop();
	zassert_false(mic_done, "the loop ends after the block it is waiting for");
	mic_block(tone);

	zassert_true(mic_done);
	zassert_equal(mic_ret, 0);
	zassert_equal(dmic_get_stop_state(), 1);
	zassert_equal(nrfx_pdm_stop_fake.call_count, 1);
	zassert_equal(NRF_PDM0->ENABLE, 0);
	zassert_false(pdm_driver_active());
	zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT);
}

ZTEST(dmic, test_no_audio_for_one_second_ends_the_stream_with_an_error)
{
	mic_start();
	g1_clock_advance_ms(1000 + 10);

	zassert_true(mic_done);
	zassert_equal(mic_ret, -1);
	zassert_equal(nrfx_pdm_stop_fake.call_count, 1);
	zassert_equal(k_mem_slab_num_free_get(&dmic_mem_slab), BLOCK_COUNT);
}

ZTEST(dmic, test_pause_keeps_capturing_but_sends_nothing)
{
	struct ble_context *ble = g1_ble();

	mic_start();
	dmic_stream_pause(1);
	zassert_equal(dmic_get_stop_state(), 2, "paused");
	mic_block(tone);
	mic_block(tone);

	zassert_false(mic_done);
	zassert_equal(get_dmic_num_messages(), 2, "blocks still encoded and queued");
	zassert_equal(dmic_send_pending, 0);
	zassert_equal(k_sem_count_get(&ble->rx_sem), 0);
	zassert_equal(audio_bt_queue_log.count, 0);

	dmic_stream_pause(0);
	zassert_equal(dmic_get_stop_state(), 0);
	mic_block(tone);
	zassert_equal(dmic_send_pending, 1);
	zassert_equal(mic_stop(), 0);
}

ZTEST(dmic, test_stop_state_is_reported_by_the_master_only)
{
	zassert_equal(dmic_get_stop_state(), 1, "stopped at boot");
	mic_start();
	zassert_equal(dmic_get_stop_state(), 0);
	zassert_equal(mic_stop(), 0);

	g1_unit_set_role(G1_ROLE_SLAVE);
	zassert_equal(dmic_get_stop_state(), 0);
}

ZTEST(dmic, test_restart_after_stop_reconfigures_the_pdm)
{
	int gain_calls;

	mic_start();
	mic_block(tone);
	zassert_equal(mic_stop(), 0);
	gain_calls = nrf_pdm_gain_set_fake.call_count;
	g1_fake_pdm.log_count = 0;

	mic_start();
	mic_block(tone);

	zassert_true(g1_fake_pdm_log_find(G1_PDM_OP_UNINIT, 0) >= 0, "old configuration released");
	zassert_true(g1_fake_pdm_log_find(G1_PDM_OP_INIT, 0) >= 0);
	zassert_equal(nrf_pdm_gain_set_fake.call_count, gain_calls + 1);
	zassert_equal(g1_fake_pdm.gain_l_at_start, 0x40);
	zassert_equal(get_dmic_num_messages(), 1);
	zassert_equal(mic_stop(), 0);
}
