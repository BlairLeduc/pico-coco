/* present.c — see present.h. */

#include "present.h"

#include <string.h>

unsigned present_plan(present_t *p, const coco_frame_t *f, present_band_t bands[COCO_BANDS]) {
    unsigned n = 0;
    bool all = !p->valid || f->mode != p->mode;
    for (unsigned b = 0; b < COCO_BANDS; b++) {
        present_band_t *band = &bands[b];
        if (all) {
            band->dirty = true;
            band->x0 = 0;
            band->x1 = COCO_PIC_W - 1u;
        } else {
            band->dirty = mc6847_band_span(f->mode, f->lines, p->shadow, b, &band->x0, &band->x1);
        }
        n += band->dirty;
    }
    memcpy(p->shadow, f->lines, sizeof p->shadow);
    p->mode = f->mode;
    p->valid = true;
    return n;
}
