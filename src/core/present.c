/* present.c — see present.h. */

#include "present.h"

#include <string.h>

/* Are the band's lines all drawn in one mode, end to end? */
static bool band_plain(const coco_line_t *how) {
    for (unsigned i = 0; i < COCO_BAND_ROWS; i++) {
        if (how[i].mode != how[0].mode) return false;
        for (unsigned k = 0; k < COCO_LINE_SPLITS; k++)
            if (how[i].x[k]) return false;
    }
    return true;
}

unsigned present_plan(present_t *p, const coco_frame_t *f, present_band_t bands[COCO_BANDS]) {
    unsigned n = 0;
    uint8_t border = coco_frame_border_mode(f);
    bool all = !p->valid || border != p->mode;
    for (unsigned b = 0; b < COCO_BANDS; b++) {
        present_band_t *band = &bands[b];
        unsigned y0 = b * COCO_BAND_ROWS;
        const coco_line_t *how = &f->how[y0];
        band->x0 = 0;
        band->x1 = COCO_PIC_W - 1u;
        if (all || memcmp(how, &p->how[y0], COCO_BAND_ROWS * sizeof how[0]) != 0) {
            band->dirty = true;
        } else if (band_plain(how)) {
            band->dirty = mc6847_band_span(how[0].mode, f->lines, p->shadow, b, &band->x0,
                                           &band->x1);
        } else {
            /* The columns a byte covers differ by the line and along
             * it, so any change sends the band whole. */
            band->dirty = memcmp(&f->lines[y0 * COCO_VDG_LINE_BYTES],
                                 &p->shadow[y0 * COCO_VDG_LINE_BYTES],
                                 COCO_BAND_ROWS * COCO_VDG_LINE_BYTES) != 0;
        }
        n += band->dirty;
    }
    memcpy(p->shadow, f->lines, sizeof p->shadow);
    memcpy(p->how, f->how, sizeof p->how);
    p->full = all;
    p->mode = border;
    p->valid = true;
    return n;
}

void present_render_line(const coco_frame_t *f, unsigned y, present_vdg_fn vdg, void *ctx,
                         uint16_t *dst, uint16_t *tmp) {
    const coco_line_t *how = &f->how[y];
    const uint8_t *bytes = &f->lines[y * COCO_VDG_LINE_BYTES];
    mc6847_render_line(vdg(ctx, how->mode), bytes, y, dst);
    for (unsigned k = 0; k < COCO_LINE_SPLITS && how->x[k]; k++) {
        /* The whole line in the new mode, and its pixels from the
         * change on: a later change draws over this one's. */
        mc6847_render_line(vdg(ctx, how->m[k]), bytes, y, tmp);
        memcpy(dst + how->x[k], tmp + how->x[k], (COCO_PIC_W - how->x[k]) * sizeof dst[0]);
    }
}
