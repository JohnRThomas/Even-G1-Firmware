#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>

#include "fakes_audio.h"
#include "harness.h"

#include "display/proxy_thread.h"
#include "production_test/production_test.h"
#include "sensors_audio/sensors_audio.h"

DEFINE_FAKE_VALUE_FUNC(uint8_t, dmic_get_stop_state);
DEFINE_FAKE_VALUE_FUNC(int, dmic_stream_stop);
DEFINE_FAKE_VALUE_FUNC(int, dmic_stream_pause, int);
DEFINE_FAKE_VALUE_FUNC(uint32_t, get_dmic_num_messages);
DEFINE_FAKE_VALUE_FUNC(int, dequeue_dmic, void *);
__weak uint8_t dmic_send_pending;

DEFINE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
DEFINE_FAKE_VALUE_FUNC(uint32_t, getAudioMessageQueueNum);
DEFINE_FAKE_VALUE_FUNC(int, audioStreamFileManagerHandler);
DEFINE_FAKE_VALUE_FUNC(int, delAudioStreamRecord, uint8_t);

DEFINE_FAKE_VALUE_FUNC(int, requestAudioInfoToApp, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, sendAudioStreamFileToApp, uint8_t, uint8_t);

__weak struct k_sem mic_transm_sem;

/* 1 on the master, as at boot */
static uint8_t dmic_get_stop_state_default(void)
{
	return get_dashboard_state()->is_master == 1 ? 1 : 0;
}

static void fakes_audio_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(dmic_get_stop_state);
	dmic_get_stop_state_fake.custom_fake = dmic_get_stop_state_default;
	RESET_FAKE(dmic_stream_stop);
	RESET_FAKE(dmic_stream_pause);
	RESET_FAKE(get_dmic_num_messages);
	RESET_FAKE(dequeue_dmic);
	dequeue_dmic_fake.return_val = -ENOMSG;
	dmic_send_pending = 0;

	RESET_FAKE(request_dmic_stream_start);
	RESET_FAKE(getAudioMessageQueueNum);
	RESET_FAKE(audioStreamFileManagerHandler);
	RESET_FAKE(delAudioStreamRecord);

	RESET_FAKE(requestAudioInfoToApp);
	RESET_FAKE(sendAudioStreamFileToApp);

	k_sem_init(&mic_transm_sem, 0, 1);
}

G1_UNIT_RESET_HOOK(fakes_audio_reset);
