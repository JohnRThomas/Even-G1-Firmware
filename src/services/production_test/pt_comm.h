/*
 * Declarations of pt_comm.c.
 */
#ifndef G1_SERVICES_PT_PT_COMM_H_
#define G1_SERVICES_PT_PT_COMM_H_

#include "services/production_test/production_test.h"

uint8_t pt_comm_busy_get(void); /* a production-test transfer is in progress */
void pt_comm_busy_set(void);
void pt_comm_busy_clear(void);
int comm_write(int type, void *ctx, uint8_t *data, size_t len);
int comm_read(int type, void *ctx, uint8_t **data, uint32_t *len);

#endif /* G1_SERVICES_PT_PT_COMM_H_ */
