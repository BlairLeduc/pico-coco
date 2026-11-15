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
    uint8_t mode;      /* the mode the panel shows                      */
    uint8_t shadow[COCO_SNAP_VRAM_MAX];   /* the bytes the panel shows  */
} present_t;

static inline void present_invalidate(present_t *p) { p->valid = false; }

/* Plan the present of `f`: fill `bands` and return how many are dirty.
 * The shadow and mode then describe `f`, as the panel will once the
 * dirty bands are sent. */
unsigned present_plan(present_t *p, const coco_frame_t *f, present_band_t bands[COCO_BANDS]);

#endif /* PICO_COCO_PRESENT_H */
