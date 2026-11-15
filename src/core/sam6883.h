/* sam6883.h — the MC6883 SAM's register (design.md §8.2, EL §4.2).
 *
 * The generic part: sixteen bits, each cleared by a write to the even
 * address of its pair in $FFC0-$FFDF and set by a write to the odd one,
 * whatever is written. From them the machine (coco.c) derives the memory
 * map, the video address for each scanline (M4), and the CPU's rate. The
 * bit layout is the datasheet's as XRoar decodes it (§18: medium):
 *
 *   bits 0-2   V0-V2  VDG addressing mode       $FFC0-$FFC5
 *   bits 3-9   F0-F6  display offset, x 512     $FFC6-$FFD3
 *   bit  10    P1     page                      $FFD4-$FFD5
 *   bits 11-12 R0-R1  CPU rate                  $FFD6-$FFD9
 *   bits 13-14 M0-M1  memory size               $FFDA-$FFDD
 *   bit  15    TY     map type                  $FFDE-$FFDF
 */
#ifndef PICO_COCO_SAM6883_H
#define PICO_COCO_SAM6883_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t reg;
} sam6883_t;

static inline void sam6883_reset(sam6883_t *s) { s->reg = 0; }

/* A write anywhere in $FFC0-$FFDF. Returns the bits that changed. */
static inline uint16_t sam6883_write(sam6883_t *s, uint16_t a) {
    unsigned bit = (a >> 1) & 15u;
    uint16_t old = s->reg;
    if (a & 1u) s->reg = (uint16_t)(s->reg | (1u << bit));
    else        s->reg = (uint16_t)(s->reg & ~(1u << bit));
    return (uint16_t)(old ^ s->reg);
}

#define SAM_V_MASK   0x0007u
#define SAM_F_MASK   0x03F8u
#define SAM_P_BIT    0x0400u
#define SAM_R_MASK   0x1800u
#define SAM_M_MASK   0x6000u
#define SAM_TY_BIT   0x8000u

/* The bits that choose the memory map: a change rebuilds the page table
 * (§8.1). */
#define SAM_MAP_BITS (SAM_P_BIT | SAM_M_MASK | SAM_TY_BIT)

static inline unsigned sam6883_v(const sam6883_t *s)  { return s->reg & SAM_V_MASK; }
static inline unsigned sam6883_f(const sam6883_t *s)  { return (s->reg & SAM_F_MASK) >> 3; }
static inline bool     sam6883_p(const sam6883_t *s)  { return (s->reg & SAM_P_BIT) != 0; }
static inline unsigned sam6883_r(const sam6883_t *s)  { return (s->reg & SAM_R_MASK) >> 11; }
static inline unsigned sam6883_m(const sam6883_t *s)  { return (s->reg & SAM_M_MASK) >> 13; }
static inline bool     sam6883_ty(const sam6883_t *s) { return (s->reg & SAM_TY_BIT) != 0; }

/* Where the display starts: F x 512. */
static inline uint16_t sam6883_display_base(const sam6883_t *s) {
    return (uint16_t)(sam6883_f(s) << 9);
}

#endif /* PICO_COCO_SAM6883_H */
