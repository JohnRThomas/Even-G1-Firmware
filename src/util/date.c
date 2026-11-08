/*
 * Calendar helpers: UTC time stamp to date fields, weekday names.
 */
#include <stdint.h>
#include "util/date.h"
#include "ui/gui/gui.h"

#define IS_LEAP_YEAR(y) (((y) % 4 == 0 && (y) % 100 != 0) || (y) % 400 == 0)
#define SECONDS_PER_DAY 86400

static const uint16_t days_of_month[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};
/* day of the week (0 = Sunday), Zeller-like congruence */
int day_of_week(int year, int month, int day)
{
	if (month == 1 || month == 2) {
		month += 12;
		year--;
	}
	return (day + 2 * month + 3 * (month + 1) / 5 + year + year / 4 - year / 100 + year / 400 +
		1) %
	       7;
}

int timestamp_to_date(uint32_t timestamp, struct bp_date *date)
{
	uint32_t days = timestamp / SECONDS_PER_DAY;
	uint32_t secs = timestamp % SECONDS_PER_DAY;
	uint16_t year = 1970;
	uint16_t month;
	uint16_t n;

	while (1) {
		n = IS_LEAP_YEAR(year) ? 366 : 365;
		if (days < n) {
			break;
		}
		days -= n;
		year++;
	}
	date->year = year;

	for (month = 0; month < 12; month++) {
		n = days_of_month[month];
		if (month == 1 && IS_LEAP_YEAR(year)) {
			n = 29;
		}
		if (days < n) {
			break;
		}
		days -= n;
	}

	date->month = month + 1;
	date->day = days + 1;
	date->hour = secs / 3600;
	secs %= 3600;
	date->minute = secs / 60;
	date->second = secs % 60;

	return 0;
}

/* weekday name of a UTC timestamp */
const char *get_weekday_name(uint32_t ts)
{
	struct bp_date date = {0};

	timestamp_to_date(ts, &date);
	printk("date: %u: %04d/%02d/%02d-%02d:%02d:%02d\n", ts, date.year, date.month, date.day,
	       date.hour, date.minute, date.second);

	return get_string((uint8_t)(day_of_week(date.year, date.month, date.day) + 0x3b));
}
