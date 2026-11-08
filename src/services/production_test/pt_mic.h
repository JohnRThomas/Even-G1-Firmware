/*
 * Declarations of pt_mic.c.
 */
#ifndef G1_SERVICES_PT_PT_MIC_H_
#define G1_SERVICES_PT_PT_MIC_H_

#include "services/production_test/production_test.h"

extern struct k_sem mic_transm_sem; /* mic_transm_thread trigger */
void mic_transm_thread(void *p1, void *p2, void *p3);
void start_mic_transm_thread(void);

#endif /* G1_SERVICES_PT_PT_MIC_H_ */
