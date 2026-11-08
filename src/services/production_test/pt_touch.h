/*
 * Declarations of pt_touch.c.
 */
#ifndef G1_SERVICES_PT_PT_TOUCH_H_
#define G1_SERVICES_PT_PT_TOUCH_H_

#include "services/production_test/production_test.h"

void product_test_touch_key_thread(void *p1, void *p2, void *p3);
void enter_silent_mode(void);
void exit_silent_mode(void);

#endif /* G1_SERVICES_PT_PT_TOUCH_H_ */
