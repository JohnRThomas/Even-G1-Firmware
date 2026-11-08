/*
 * Digital microphone (dmic.c): PDM capture, LC3 encoding and the queue of
 * encoded frames.
 */
#ifndef G1_DRIVERS_MIC_DMIC_H_
#define G1_DRIVERS_MIC_DMIC_H_

#include <stdint.h>
#include <zephyr/sys/atomic.h>

#define DMIC_MSG_SIZE 200

extern atomic_t audio_stream_recording;
extern uint8_t dmic_send_pending; /* pending 0xf1 dmic request */

int enqueue_dmic(void *msg);
uint32_t get_dmic_num_messages(void);
int dequeue_dmic(void *msg);
void app_codec_lc3_test(void *pcm, int len);
uint8_t dmic_get_stop_state(void);
int dmic_stream_stop(void);
int dmic_stream_pause(int pause);
int dmic_stream_start(void);
int erase_audio_buffer(void);

#endif /* G1_DRIVERS_MIC_DMIC_H_ */
