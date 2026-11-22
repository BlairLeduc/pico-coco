/* display.h — what core 1 puts on the panel (design.md §9).
 *
 * Core 1 only. Owns the renderer, and so the only graphics table, the
 * presenter's shadow (present.h) and the DMA line buffers (§6). There is
 * no framebuffer (§9.1): every pixel sent is generated from a snapshot
 * as it goes.
 *
 * Adapted from pico-atom's, the family's other MC6847: the dirty-band
 * present, the border drawn only when its colour changes, and the two
 * text lines in the VDG's own glyphs. The plan of which bands to send is
 * the core's (present.c), where the host tests it.
 */
#ifndef PICO_COCO_DISPLAY_H
#define PICO_COCO_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"

typedef struct {
    uint32_t us;        /* wall time of the present                    */
    uint16_t bands;     /* bands sent (COCO_BANDS for a full redraw)   */
    uint32_t pixels;    /* pixels on the wire                          */
    bool     full;      /* every band: a new mode, or invalidated      */
    bool     border;    /* the border was filled too                   */
} display_stats_t;

/* The renderer with the VDG's character set, the colour palette, NTSC
 * artefacts in XRoar's default phase and black backgrounds, as §9.5
 * makes the CoCo's defaults until the menu's settings (M9); the shadow
 * invalid. The panel is as lcd_init left it, black. */
void display_init(void);

/* Present one snapshot: plan it against the shadow (present.h), fill the
 * border if its colour changed, and send each dirty band's span as one
 * window in the picture at (32, 64) (§9.6). The frame may go back to the
 * pool as soon as this returns. */
void display_present(const coco_frame_t *f, display_stats_t *st);

/* Forget what is on the panel, so the next present sends every band. */
void display_invalidate(void);

/* The MC6847T1 drawn, the CoCo 2B's VDG with lower case, or the MC6847
 * (mc6847.h, design.md §9.2): the next present repaints every band, and
 * the border if its colour changes. */
void display_set_t1(bool t1);

/* The perf line at the panel's top and the status line at its foot
 * (§9.6): COCO_TEXT_COLS characters in the VDG's glyphs, capitals only,
 * the screen's green on black, each drawn only when its text differs
 * from what is there. Shorter text is padded with spaces. */
void display_perf(const char *text);
void display_status(const char *text);

#endif /* PICO_COCO_DISPLAY_H */
