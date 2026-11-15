/* display.h — what core 1 puts on the panel (design.md §9).
 *
 * Core 1 only. Owns the DMA line buffers (§6). There is no framebuffer
 * (§9.1): every pixel sent is generated as it goes.
 *
 * M7: the presenter (present.h) with its dirty bands (§9.3), and the
 * status and perf lines (§9.6).
 */
#ifndef PICO_COCO_DISPLAY_H
#define PICO_COCO_DISPLAY_H

#include <stdint.h>

/* The bring-up pattern (design.md §17 M6, EL §13.3): a 1-px white border
 * exactly on the picture's 256x192 rectangle at (32, 64) (§9.6, D11),
 * with a 16x16 block in each inside corner (red top-left, green
 * top-right, blue bottom-left, yellow bottom-right), so a mirrored axis
 * or swapped R/B shows as the wrong colour in the wrong corner. A 1-px
 * grey frame on the panel's own edge shows that all 320x320 are
 * addressed. Everything else is black. */
void display_test_pattern(void);

/* The blit costs M6 measures, each the wall time of a whole operation:
 * the picture's rectangle in one colour by DMA with the read address
 * held; every row of it from a line buffer, as the presenter will send
 * them; one 256-pixel row with its own window; and the whole panel in
 * one colour. Leaves the panel black. */
void display_measure(uint32_t *fill_us, uint32_t *blit_us, uint32_t *row_us,
                     uint32_t *panel_us);

#endif /* PICO_COCO_DISPLAY_H */
