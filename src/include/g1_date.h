/*
 * Date helpers shared by several modules.
 *
 * timestamp_to_date() splits a UTC time stamp into six uint16_t fields;
 * get_date() / set_date() (dashboard/dashboard.c) read / write the current
 * time stamp.
 */
#ifndef G1_DATE_H_
#define G1_DATE_H_

#include <stdint.h>

struct g1_date {
  uint16_t year;
  uint16_t month;  /* 1..12 */
  uint16_t day;    /* 1..31 */
  uint16_t hour;
  uint16_t minute;
  uint16_t second;
};

int timestamp_to_date(uint32_t timestamp, struct g1_date *date);
uint32_t get_date(void);
uint32_t set_date(uint32_t ts);

#endif /* G1_DATE_H_ */
