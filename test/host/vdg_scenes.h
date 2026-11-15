/* vdg_scenes.h — the fixed screens behind the golden images
 * (design.md §9.6, EL §5.7), adapted from pico-atom's.
 *
 * One list, shared by tools/vdg-ppm.c, which writes the images, and
 * test_mc6847_golden.c, which compares against them, so the two cannot
 * drift. Each scene is RAM, a VDG mode and a SAM mode, drawn through the
 * machine's snapshot (coco_video_snapshot) and the renderer, the path the
 * presenter will take.
 *
 * Built to be checked by eye: in each graphics mode a frame on the
 * outermost source pixels proves the extents, a diagonal the
 * proportions, and bars the colour order; the text scenes show every
 * glyph, plain and inverse, and every semigraphics byte. A golden image
 * proves nothing until someone has looked at it.
 */
#ifndef PICO_COCO_VDG_SCENES_H
#define PICO_COCO_VDG_SCENES_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"
#include "mc6847.h"

typedef struct {
    const char    *name;      /* file stem                               */
    uint8_t        mode;      /* the VDG's, packed                       */
    uint8_t        sam_v;
    vdg_artefact_t artefact;
    void         (*fill)(uint8_t *ram, uint8_t mode);   /* from $0400 */
} vdg_scene_t;

extern const vdg_scene_t vdg_scenes[];
extern const unsigned    vdg_scene_count;

/* The display offset every scene uses. */
#define VDG_SCENE_BASE 0x0400u

/* A scene's snapshot, from a machine `m` the caller owns. */
void vdg_scene_frame(const vdg_scene_t *s, coco_t *m, coco_frame_t *f);

#endif /* PICO_COCO_VDG_SCENES_H */
