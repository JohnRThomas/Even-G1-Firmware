/* Microphone and audio pipeline fakes. */
#ifndef G1_UNIT_FAKES_AUDIO_H_
#define G1_UNIT_FAKES_AUDIO_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(uint8_t, dmic_get_stop_state);
DECLARE_FAKE_VALUE_FUNC(int, dmic_stream_stop);
DECLARE_FAKE_VALUE_FUNC(int, dmic_stream_pause, int);
DECLARE_FAKE_VALUE_FUNC(uint32_t, get_dmic_num_messages);
DECLARE_FAKE_VALUE_FUNC(int, dequeue_dmic, void *);
extern uint8_t dmic_send_pending;

DECLARE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
DECLARE_FAKE_VALUE_FUNC(uint32_t, getAudioMessageQueueNum);
DECLARE_FAKE_VALUE_FUNC(int, audioStreamFileManagerHandler);
DECLARE_FAKE_VALUE_FUNC(int, delAudioStreamRecord, uint8_t);

DECLARE_FAKE_VALUE_FUNC(int, requestAudioInfoToApp, uint8_t);
DECLARE_FAKE_VALUE_FUNC(int, sendAudioStreamFileToApp, uint8_t, uint8_t);

extern struct k_sem mic_transm_sem;

#endif /* G1_UNIT_FAKES_AUDIO_H_ */
