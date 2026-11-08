/*
 * UTF-8 decoding (utf8.c).
 */
#ifndef G1_UTIL_UTF8_H_
#define G1_UTIL_UTF8_H_

#include <stdint.h>

uint16_t utf8_decode_char(const uint8_t *s, int len);
int utf8_to_unicode(const char *utf8, int len, uint16_t *out, int max);
/* decodes into a shared buffer of 0x400 code points */
uint16_t *utf8_to_unicode_buf(const char *utf8, uint16_t *count);

#endif /* G1_UTIL_UTF8_H_ */
