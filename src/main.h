/*
 * Boot and global state (main.c): the GlassesState instance, the device role.
 */
#ifndef G1_MAIN_H_
#define G1_MAIN_H_

#include <stdint.h>
#include "system/glasses_state.h"

extern struct GlassesState *GLOBAL_STATE;

int getDeviceRole(void);
uint8_t is_master(void);

#endif /* G1_MAIN_H_ */
