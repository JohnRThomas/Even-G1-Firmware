/*
 * MD5 message digest (RFC 1321 reference implementation, "md5c.c"),
 * exported under mbedtls-style names.
 *
 * Derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm:
 *   MD5Init      -> mbedtls_md5_starts
 *   MD5Transform -> mbedtls_internal_md5_process
 *   MD5Update    -> mbedtls_md5_update
 *   MD5Final     -> mbedtls_md5_finish
 *   Encode / Decode -> Encode / md5_decode (both static)
 * mbedtls_md5 (one-shot starts/update/finish) is an addition.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "md5.h"

/* Constants for MD5Transform routine. */
#define S11 7U
#define S12 12U
#define S13 17U
#define S14 22U
#define S21 5U
#define S22 9U
#define S23 14U
#define S24 20U
#define S31 4U
#define S32 11U
#define S33 16U
#define S34 23U
#define S41 6U
#define S42 10U
#define S43 15U
#define S44 21U

static unsigned char PADDING[64] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* MD5_F, MD5_G, MD5_H and MD5_I are the basic MD5 functions F, G, H and I. */
#define MD5_F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~(z))))

/* ROTATE_LEFT rotates x left n bits. */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32U - (n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. */
#define FF(a, b, c, d, x, s, ac)                                                                                       \
  {                                                                                                                    \
    (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac);                                                                \
    (a) = ROTATE_LEFT((a), (s));                                                                                       \
    (a) += (b);                                                                                                        \
  }
#define GG(a, b, c, d, x, s, ac)                                                                                       \
  {                                                                                                                    \
    (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac);                                                                \
    (a) = ROTATE_LEFT((a), (s));                                                                                       \
    (a) += (b);                                                                                                        \
  }
#define HH(a, b, c, d, x, s, ac)                                                                                       \
  {                                                                                                                    \
    (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac);                                                                \
    (a) = ROTATE_LEFT((a), (s));                                                                                       \
    (a) += (b);                                                                                                        \
  }
#define II(a, b, c, d, x, s, ac)                                                                                       \
  {                                                                                                                    \
    (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac);                                                                \
    (a) = ROTATE_LEFT((a), (s));                                                                                       \
    (a) += (b);                                                                                                        \
  }

/* Encodes input (uint32_t) into output (unsigned char). len is a multiple of 4. */
static void Encode(unsigned char *output, const uint32_t *input, unsigned int len) {
  unsigned int i = 0;
  unsigned int j;

  for (j = 0; j < len; j += 4U) {
    output[j] = (unsigned char)(input[i] & 0xffU);
    output[j + 1U] = (unsigned char)((input[i] >> 8U) & 0xffU);
    output[j + 2U] = (unsigned char)((input[i] >> 16U) & 0xffU);
    output[j + 3U] = (unsigned char)((input[i] >> 24U) & 0xffU);
    i++;
  }
}

/* Decodes input (unsigned char) into output (uint32_t). len is a multiple of 4. */
static void md5_decode(uint32_t *output, const unsigned char *input, unsigned int len) {
  unsigned int i = 0;
  unsigned int j;

  for (j = 0; j < len; j += 4U) {
    output[i] = ((uint32_t)input[j]) | (((uint32_t)input[j + 1U]) << 8U) | (((uint32_t)input[j + 2U]) << 16U) |
                (((uint32_t)input[j + 3U]) << 24U);
    i++;
  }
}

void mbedtls_md5_starts(mbedtls_md5_context *context) {
  context->count[1] = 0;
  context->count[0] = 0;
  context->state[0] = 0x67452301U;
  context->state[1] = 0xefcdab89U;
  context->state[2] = 0x98badcfeU;
  context->state[3] = 0x10325476U;
}

void mbedtls_internal_md5_process(uint32_t state[4], const unsigned char block[64]) {
  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t x[64];

  md5_decode(x, block, 64);

  /* Round 1 */
  FF(a, b, c, d, x[0], S11, 0xd76aa478U);
  FF(d, a, b, c, x[1], S12, 0xe8c7b756U);
  FF(c, d, a, b, x[2], S13, 0x242070dbU);
  FF(b, c, d, a, x[3], S14, 0xc1bdceeeU);
  FF(a, b, c, d, x[4], S11, 0xf57c0fafU);
  FF(d, a, b, c, x[5], S12, 0x4787c62aU);
  FF(c, d, a, b, x[6], S13, 0xa8304613U);
  FF(b, c, d, a, x[7], S14, 0xfd469501U);
  FF(a, b, c, d, x[8], S11, 0x698098d8U);
  FF(d, a, b, c, x[9], S12, 0x8b44f7afU);
  FF(c, d, a, b, x[10], S13, 0xffff5bb1U);
  FF(b, c, d, a, x[11], S14, 0x895cd7beU);
  FF(a, b, c, d, x[12], S11, 0x6b901122U);
  FF(d, a, b, c, x[13], S12, 0xfd987193U);
  FF(c, d, a, b, x[14], S13, 0xa679438eU);
  FF(b, c, d, a, x[15], S14, 0x49b40821U);

  /* Round 2 */
  GG(a, b, c, d, x[1], S21, 0xf61e2562U);
  GG(d, a, b, c, x[6], S22, 0xc040b340U);
  GG(c, d, a, b, x[11], S23, 0x265e5a51U);
  GG(b, c, d, a, x[0], S24, 0xe9b6c7aaU);
  GG(a, b, c, d, x[5], S21, 0xd62f105dU);
  GG(d, a, b, c, x[10], S22, 0x2441453U);
  GG(c, d, a, b, x[15], S23, 0xd8a1e681U);
  GG(b, c, d, a, x[4], S24, 0xe7d3fbc8U);
  GG(a, b, c, d, x[9], S21, 0x21e1cde6U);
  GG(d, a, b, c, x[14], S22, 0xc33707d6U);
  GG(c, d, a, b, x[3], S23, 0xf4d50d87U);
  GG(b, c, d, a, x[8], S24, 0x455a14edU);
  GG(a, b, c, d, x[13], S21, 0xa9e3e905U);
  GG(d, a, b, c, x[2], S22, 0xfcefa3f8U);
  GG(c, d, a, b, x[7], S23, 0x676f02d9U);
  GG(b, c, d, a, x[12], S24, 0x8d2a4c8aU);

  /* Round 3 */
  HH(a, b, c, d, x[5], S31, 0xfffa3942U);
  HH(d, a, b, c, x[8], S32, 0x8771f681U);
  HH(c, d, a, b, x[11], S33, 0x6d9d6122U);
  HH(b, c, d, a, x[14], S34, 0xfde5380cU);
  HH(a, b, c, d, x[1], S31, 0xa4beea44U);
  HH(d, a, b, c, x[4], S32, 0x4bdecfa9U);
  HH(c, d, a, b, x[7], S33, 0xf6bb4b60U);
  HH(b, c, d, a, x[10], S34, 0xbebfbc70U);
  HH(a, b, c, d, x[13], S31, 0x289b7ec6U);
  HH(d, a, b, c, x[0], S32, 0xeaa127faU);
  HH(c, d, a, b, x[3], S33, 0xd4ef3085U);
  HH(b, c, d, a, x[6], S34, 0x4881d05U);
  HH(a, b, c, d, x[9], S31, 0xd9d4d039U);
  HH(d, a, b, c, x[12], S32, 0xe6db99e5U);
  HH(c, d, a, b, x[15], S33, 0x1fa27cf8U);
  HH(b, c, d, a, x[2], S34, 0xc4ac5665U);

  /* Round 4 */
  II(a, b, c, d, x[0], S41, 0xf4292244U);
  II(d, a, b, c, x[7], S42, 0x432aff97U);
  II(c, d, a, b, x[14], S43, 0xab9423a7U);
  II(b, c, d, a, x[5], S44, 0xfc93a039U);
  II(a, b, c, d, x[12], S41, 0x655b59c3U);
  II(d, a, b, c, x[3], S42, 0x8f0ccc92U);
  II(c, d, a, b, x[10], S43, 0xffeff47dU);
  II(b, c, d, a, x[1], S44, 0x85845dd1U);
  II(a, b, c, d, x[8], S41, 0x6fa87e4fU);
  II(d, a, b, c, x[15], S42, 0xfe2ce6e0U);
  II(c, d, a, b, x[6], S43, 0xa3014314U);
  II(b, c, d, a, x[13], S44, 0x4e0811a1U);
  II(a, b, c, d, x[4], S41, 0xf7537e82U);
  II(d, a, b, c, x[11], S42, 0xbd3af235U);
  II(c, d, a, b, x[2], S43, 0x2ad7d2bbU);
  II(b, c, d, a, x[9], S44, 0xeb86d391U);

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

void mbedtls_md5_update(mbedtls_md5_context *context, const unsigned char *input, unsigned int inputLen) {
  unsigned int i;
  unsigned int index;
  unsigned int partLen;

  /* Compute number of bytes mod 64 */
  index = (context->count[0] >> 3U) & 0x3FU;
  partLen = 64U - index;

  /* Update number of bits */
  context->count[0] += ((uint32_t)inputLen << 3U);
  if (context->count[0] < ((uint32_t)inputLen << 3U)) {
    context->count[1]++;
  }
  context->count[1] += ((uint32_t)inputLen >> 29U);

  /* Transform as many times as possible. */
  if (partLen > inputLen) {
    i = 0;
  } else {
    memcpy(&context->buffer[index], input, partLen);
    mbedtls_internal_md5_process(context->state, context->buffer);

    for (i = partLen; (i + 64U) <= inputLen; i += 64U) {
      /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound): i + 64 <= inputLen */
      mbedtls_internal_md5_process(context->state, &input[i]);
    }

    index = 0;
  }

  /* Buffer remaining input */
  /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound): i <= inputLen */
  memcpy(&context->buffer[index], &input[i], inputLen - i);
}

void mbedtls_md5_finish(mbedtls_md5_context *context, unsigned char digest[16]) {
  unsigned char bits[8];
  unsigned int index;
  unsigned int padLen;

  /* Pad out to 56 mod 64. */
  index = (context->count[0] >> 3U) & 0x3fU;
  padLen = (index < 56U) ? (56U - index) : (120U - index);

  /* Save number of bits */
  Encode(bits, context->count, 8);
  mbedtls_md5_update(context, PADDING, padLen);

  /* Append length (before padding) */
  mbedtls_md5_update(context, bits, 8);

  /* Store state in digest */
  Encode(digest, context->state, 16);
}

int mbedtls_md5(const unsigned char *input, size_t ilen, unsigned char output[16]) {
  mbedtls_md5_context ctx;
  unsigned char digest[16];

  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, input, ilen);
  mbedtls_md5_finish(&ctx, digest);
  memcpy(output, digest, 16);

  return 0;
}
