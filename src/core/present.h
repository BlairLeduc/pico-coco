/* present.h — what the presenter repaints (design.md §9.3, EL §5.3).
 *
 * Core 1 presents each snapshot it takes. This decides which bands of
 * COCO_BAND_ROWS lines to send and which columns of each, and keeps the
 * shadow it decides against. The mode belongs in the shadow (EL §5.3): a
 * change of the VDG's mode or colour set, or of how colours are drawn
 * (palette, dark background, artefacts: the caller says so with
 * present_invalidate), repaints every band, since every byte can stay the
 * same while every pixel changes. Otherwise a band is sent where its
 * bytes differ from the shadow, across the columns that differ.
 *
 * A frame's lines may have modes of their own, and changes of mode
 * along them (§9.4), so the shadow keeps how each line is drawn: a band
 * is repainted whole where that changed for any of its lines, and a band
 * whose lines are not all drawn one way is sent whole when any byte of
 * it changes.
 *
 * Here, not in the port, so the host can hold it to a full render.
 */
#ifndef PICO_COCO_PRESENT_H
#define PICO_COCO_PRESENT_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"

typedef struct {
    bool     dirty;
    uint16_t x0, x1;   /* inclusive pixel columns                       */
} present_band_t;

typedef struct {
    bool    valid;     /* false: the next plan repaints everything      */
    uint8_t mode;      /* the mode whose border the panel shows
                        * (coco_frame_border_mode)                      */
    bool    full;      /* the last plan repainted for that mode, or all */
    uint8_t reach;     /* pixels a changed byte's colours reach beside
                        * it: 2 with the full artefacts (§9.5), else 0;
                        * the caller's, kept by present_invalidate      */
    coco_line_t how[COCO_PIC_H];          /* each line's, as shown      */
    uint8_t shadow[COCO_SNAP_VRAM_MAX];   /* the bytes the panel shows  */
} present_t;

static inline void present_invalidate(present_t *p) { p->valid = false; }

/* Plan the present of `f`: fill `bands` and return how many are dirty.
 * The shadow and modes then describe `f`, as the panel will once the
 * dirty bands are sent. */
unsigned present_plan(present_t *p, const coco_frame_t *f, present_band_t bands[COCO_BANDS]);

/* The renderer for a mode: the caller's, which may keep one table or
 * several (display.c keeps two, for a title that alternates). */
typedef const mc6847_t *(*present_vdg_fn)(void *ctx, uint8_t mode);

/* Line y of `f` as COCO_PIC_W pixels into `dst`, drawn as f->how[y]
 * says (§9.4): in the line's mode, and from each change along it in
 * that change's, from the same bytes; then the full artefacts over the
 * line as composed, where the renderers draw them (mc6847_artefact_line,
 * §9.5). `tmp` is COCO_PIC_W pixels of scratch. */
void present_render_line(const coco_frame_t *f, unsigned y, present_vdg_fn vdg, void *ctx,
                         uint16_t *dst, uint16_t *tmp);

#endif /* PICO_COCO_PRESENT_H */
