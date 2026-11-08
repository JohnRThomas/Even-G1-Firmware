/*
 * Work mode (work_mode.c): operating mode of the glasses and the module power
 * switching that goes with it, DFU mode switch, DFU flash erase.
 */
#ifndef G1_SYSTEM_WORK_MODE_H_
#define G1_SYSTEM_WORK_MODE_H_

#include <stdint.h>
#include <zephyr/kernel.h>

extern uint8_t runtime_mode;

int get_work_mode(void);
int change_work_mode(int mode);
void switch_to_dfu_mode(struct k_sem *sem);
int erase_dfu_flash(void);

#endif /* G1_SYSTEM_WORK_MODE_H_ */
