/* xroar_render.h — XRoar's composite renderers, a line at a time, for the
 * VDG oracle (test_vdg_oracle.c, design.md §9.5, §15.2).
 *
 * As xroar_vdg.h: the oracle sees only this header, and xroar_render.c
 * is compiled with XRoar's include paths and linked with its
 * vo_render.c, ntsc.c, filter.c, colourspace.c, messenger.c and
 * vdg_palette.c from the pinned checkout (D2, D8).
 */
#ifndef PICO_COCO_TEST_XROAR_RENDER_H
#define PICO_COCO_TEST_XROAR_RENDER_H

#include <stdint.h>

/* XRoar's cross-colour renderers (its -ccr), as the oracle uses them. */
typedef enum { XR_CCR_SIMPLE, XR_CCR_5BIT } xr_ccr_t;

/* One active line through XRoar's renderer as a CoCo sets it up: its
 * "ideal" VDG palette, NTSC, the MC1372's lead and lag, the active area
 * at sample 254, and the TV input KBRW (`krbw` 0) or KRBW (1). `half`
 * is the active line's 512 half-pixels, XRoar's VDG colour indices;
 * `border` the index either side of it. Each of the 256 pixels comes
 * back as XRoar drew both its halves: an artefact colour as XRoar's
 * table has it in 8-bit R, G, B, or, for a pixel drawn from the
 * palette, the index it was drawn from with *from_palette set. */
typedef struct {
    uint8_t r, g, b;
    uint8_t index;
    uint8_t from_palette;   /* 1: index; 0: r, g, b; 2: halves differ */
} xr_pixel_t;

void xr_render_line(xr_ccr_t ccr, int krbw, const uint8_t half[512], uint8_t border,
                    xr_pixel_t out[256]);

/* XRoar's own verdict on a VDG colour: 3 white, 2 black, 0 neither
 * (vo_render.c's is_black_or_white), from the same palette. */
int xr_black_or_white(uint8_t index);

#endif /* PICO_COCO_TEST_XROAR_RENDER_H */
