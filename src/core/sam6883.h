/* sam6883.h — the MC6883 SAM's register (design.md §8.2, EL §4.2).
 *
 * The generic part: sixteen bits, each cleared by a write to the even
 * address of its pair in $FFC0-$FFDF and set by a write to the odd one,
 * whatever is written. From them the machine (coco.c) derives the memory
 * map, the video address for each scanline (sam6883.c), and the CPU's
 * rate. The
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

/* ---- the video address counter (design.md §9.2) ---------------------- */

/* The SAM, not the VDG, makes the address of each byte the VDG shows. It
 * is a ripple counter, B15-B0, loaded with F at FS's rise and clocked by
 * the VDG's fetches, with a divider between B3 and B4 (the X divider)
 * and one between B4 and B5 (the Y divider), each chosen by V, and part
 * of it cleared at each fall of HS. The mismatched pairs of SAM and VDG
 * modes, semigraphics 8, 12 and 24 among them, are what this counter
 * does with a VDG fetching a different number of bytes a line.
 *
 * The divisions and the clears are the datasheet's table as XRoar has
 * it, and the edges are XRoar's model of the counter, from Stewart
 * Orchard's research for the SAMx4 (§18: medium). A change of V while the
 * counter runs, and the glitches it makes, are not modelled: the
 * snapshot is built with one mode a field (§9.4). */
typedef struct {
    uint16_t hi;            /* B15-B5                                    */
    uint8_t  b4;            /* B4                                        */
    uint8_t  lo;            /* B3-B0                                     */
    uint8_t  x2, x3;        /* the X divider's two counters, fed by B3   */
    uint8_t  y2, y3, y4;    /* the Y divider's: y2 and y3 fed by B4, y4
                             * by y3, for division by 2, 3 and 12        */
    uint8_t  v;             /* the mode the dividers are wired for       */
} sam6883_vc_t;

/* FS rises: the counter cleared and B15-B9 loaded from F, for mode V. */
void     sam6883_vc_load(sam6883_vc_t *c, const sam6883_t *s);

/* HS falls: B1-B4 cleared, or B1-B3, or nothing, by V. */
void     sam6883_vc_hsync(sam6883_vc_t *c);

/* The VDG fetches up to `n` bytes. Returns how many lie in order from
 * *addr, at most to the next 16-byte boundary, and advances the counter
 * past them; the caller calls again for the rest. */
unsigned sam6883_vc_fetch(sam6883_vc_t *c, unsigned n, uint16_t *addr);

#endif /* PICO_COCO_SAM6883_H */
