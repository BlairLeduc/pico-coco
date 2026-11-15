/* sam6883.c — the SAM's video address counter (design.md §9.2). See
 * sam6883.h for what it is and where the model comes from.
 *
 * Each stage counts on the fall of its input, and its output is one of
 * its bits: B3 for B3-B0; bit 0 of a divide-by-2 and of B4; bit 1 of a
 * divide-by-3 (0, 1, 2) and of the divide-by-4 that follows the Y
 * divider's divide-by-3 to make 12. Every counter is clocked whichever is
 * selected, as in the chip; V chooses only which one feeds the next stage.
 */

#include "sam6883.h"

/* By V: the X and Y divisions, and how much of the counter HS clears
 * (4: B1-B4; 3: B1-B3; 0: none, the DMA mode). The datasheet's table. */
static const uint8_t xdiv[8] = { 1, 3, 1, 2, 1, 1, 1, 1 };
static const uint8_t ydiv[8] = { 12, 1, 3, 1, 2, 1, 1, 1 };
static const uint8_t hclr[8] = { 4, 3, 4, 3, 4, 3, 4, 0 };

static void clock_hi(sam6883_vc_t *c) {
    c->hi = (uint16_t)((c->hi + 1u) & 0x7FFu);
}

/* B4's output fell: the Y divider's counters, and B5 through whichever V
 * selects. */
static void b4_fell(sam6883_vc_t *c) {
    bool y2_fell = c->y2 == 1u;
    c->y2 = (uint8_t)((c->y2 + 1u) % 2u);
    bool y3_fell = c->y3 == 2u;
    c->y3 = (uint8_t)((c->y3 + 1u) % 3u);
    bool y4_fell = false;
    if (y3_fell) {
        y4_fell = c->y4 == 3u;
        c->y4 = (uint8_t)((c->y4 + 1u) % 4u);
    }
    switch (ydiv[c->v]) {
    case 1:  clock_hi(c); break;
    case 2:  if (y2_fell) clock_hi(c); break;
    case 3:  if (y3_fell) clock_hi(c); break;
    default: if (y4_fell) clock_hi(c); break;
    }
}

static void clock_b4(sam6883_vc_t *c) {
    c->b4 ^= 1u;
    if (!c->b4) b4_fell(c);
}

/* B3's output fell: the X divider's counters, and B4 through whichever V
 * selects. */
static void b3_fell(sam6883_vc_t *c) {
    bool x2_fell = c->x2 == 1u;
    c->x2 = (uint8_t)((c->x2 + 1u) % 2u);
    bool x3_fell = c->x3 == 2u;
    c->x3 = (uint8_t)((c->x3 + 1u) % 3u);
    switch (xdiv[c->v]) {
    case 1:  clock_b4(c); break;
    case 2:  if (x2_fell) clock_b4(c); break;
    default: if (x3_fell) clock_b4(c); break;
    }
}

void sam6883_vc_load(sam6883_vc_t *c, const sam6883_t *s) {
    *c = (sam6883_vc_t){ 0 };
    c->v = (uint8_t)sam6883_v(s);
    c->hi = (uint16_t)(sam6883_display_base(s) >> 5);
}

void sam6883_vc_hsync(sam6883_vc_t *c) {
    switch (hclr[c->v]) {
    case 4:
        /* B1-B4 are held clear: B3's fall reaches neither divider nor B4,
         * and B4's fall clocks the Y divider (XRoar's CLR4). */
        c->lo = 0;
        if (c->b4) {
            c->b4 = 0;
            b4_fell(c);
        }
        break;
    case 3:
        /* B1-B3 cleared: a fall of B3 is a clock (XRoar's CLR3). */
        if (c->lo & 8u) b3_fell(c);
        c->lo = 0;
        break;
    default:
        break;
    }
}

unsigned sam6883_vc_fetch(sam6883_vc_t *c, unsigned n, uint16_t *addr) {
    *addr = (uint16_t)((c->hi << 5) | ((unsigned)c->b4 << 4) | c->lo);
    unsigned room = 16u - c->lo;
    if (n < room) {
        c->lo = (uint8_t)(c->lo + n);
        return n;
    }
    c->lo = 0;
    b3_fell(c);
    return room;
}
