/* mame_sp0256.h — MAME's SP0256 for test_sp0256 (mame_sp0256.cpp). */
#ifndef PICO_COCO_MAME_SP0256_H
#define PICO_COCO_MAME_SP0256_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void    mame_sp0256_init(const uint8_t *al2);
int     mame_sp0256_lrq(void);
int     mame_sp0256_sby(void);
void    mame_sp0256_ald(uint8_t allophone);
int16_t mame_sp0256_sample(void);
#ifdef __cplusplus
}
#endif
#endif
