#ifndef AW_TYPE_H_
#define AW_TYPE_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AW_TRUE
#define AW_TRUE (1)
#endif

#ifndef AW_FALSE
#define AW_FALSE (0)
#endif

#ifndef OFFSET_BIT_0
#define OFFSET_BIT_0 (0U)
#endif

#ifndef OFFSET_BIT_8
#define OFFSET_BIT_8 (8U)
#endif

#ifndef OFFSET_BIT_16
#define OFFSET_BIT_16 (16U)
#endif

#ifndef OFFSET_BIT_24
#define OFFSET_BIT_24 (24U)
#endif

#ifndef WORD_LEN
#define WORD_LEN (4)
#endif

#ifndef GET_BITS_7_0
#define GET_BITS_7_0 (0x00FFU)
#endif

#ifndef GET_BITS_15_8
#define GET_BITS_15_8 (0xFF00U)
#endif

#ifndef GET_BITS_24_16
#define GET_BITS_24_16 (0x00FF0000U)
#endif

#ifndef GET_BITS_31_25
#define GET_BITS_31_25 (0xFF000000U)
#endif

#ifdef __cplusplus
}
#endif

#endif  /* AW_TYPE_H_ */
