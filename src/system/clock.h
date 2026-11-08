/*
 * Wall-clock time (clock.c).
 */
#ifndef G1_SYSTEM_CLOCK_H_
#define G1_SYSTEM_CLOCK_H_

#include <stdint.h>

uint32_t get_date(void);          /* current UTC time stamp in seconds */
uint32_t set_date(uint32_t ts);
void set_wall_clock_ms(int64_t ts);
void get_wall_clock_ms(int64_t *ts);
void init_boot_seconds(void);
int get_boot_seconds(void);
/* adds the seconds elapsed since the previous call (per idx) to *total_s */
void accumulate_elapsed_seconds(uint32_t *total_s, int idx, int restart);

#endif /* G1_SYSTEM_CLOCK_H_ */
