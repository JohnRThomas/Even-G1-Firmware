/*
 * Voice recordings in external flash (audio_stream.c): the quick-note voice
 * blocks, their queue and the transfer to the phone.
 */
#ifndef G1_SERVICES_AUDIO_AUDIO_STREAM_H_
#define G1_SERVICES_AUDIO_AUDIO_STREAM_H_

#include <stdint.h>
#include <zephyr/sys/atomic.h>

int clearAudioMessageQueue(void);
int sendAudioStram2Cache(void *data);
uint32_t getAudioMessageQueueNum(void);
int startAudioStreamRecord(void);
int stopAudioStreamRecord(void);
int delAudioStreamRecord(uint8_t num);
atomic_val_t setAudioBlockInUse(atomic_val_t num);
atomic_val_t clearAudioBlockInUse(void);
int audioStreamFileManagerHandler(void);
int request_dmic_stream_start(void);

#endif /* G1_SERVICES_AUDIO_AUDIO_STREAM_H_ */
