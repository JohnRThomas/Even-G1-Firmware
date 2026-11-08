/*
 * State shared between the production-test files.
 */
#ifndef G1_PT_INTERNAL_H_
#define G1_PT_INTERNAL_H_

#include <stdint.h>
#include <stdbool.h>

#define MIC_TRANSM_CHUNK 200
#define MIC_TRANSM_FRAME (6 + MIC_TRANSM_CHUNK)

/* production_test.c */
extern uint32_t mic_flash_addr; /* mic data base address in ext flash */

/* pt_aging.c */
bool charger_enable_is_ok(void);

#endif /* G1_PT_INTERNAL_H_ */
