/*
 * Calendar helpers (date.c).
 */
#ifndef G1_UTIL_DATE_H_
#define G1_UTIL_DATE_H_

#include <stdint.h>

struct bp_date {
	uint16_t year;
	uint16_t month;  /* 1..12 */
	uint16_t day;    /* 1..31 */
	uint16_t hour;
	uint16_t minute;
	uint16_t second;
};

/* splits a UTC time stamp into date fields */
int timestamp_to_date(uint32_t timestamp, struct bp_date *date);
/* day of week, 0 = Sunday (Zeller) */
int day_of_week(int year, int month, int day);
/* three-letter weekday name of a time stamp in the current UI language */
const char *get_weekday_name(uint32_t ts);

#endif /* G1_UTIL_DATE_H_ */
