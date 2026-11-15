/* test_mc6847_golden.c — the golden images (design.md §9.6, EL §5.7),
 * adapted from pico-atom's.
 *
 * Every scene in vdg_scenes.c, drawn through the snapshot and the
 * renderer, against its committed PPM in test/golden/, which
 * tools/vdg-ppm writes. On a mismatch the drawing is written beside the
 * test binary as <name>.actual.ppm to compare by eye, and the first
 * differing pixel is named: a one-pixel change fails here.
 */

#include <stdio.h>
#include <string.h>

#include "frame_ppm.h"
#include "mc6847_font.h"
#include "test_util.h"
#include "vdg_scenes.h"

static coco_t m;
static coco_frame_t f;
static mc6847_t vdg;
static uint8_t got[FRAME_RGB_BYTES], want[FRAME_RGB_BYTES];

int main(void) {
    mc6847_init(&vdg);
    mc6847_set_font(&vdg, font_6847);
    unsigned compared = 0;
    for (unsigned i = 0; i < vdg_scene_count; i++) {
        const vdg_scene_t *s = &vdg_scenes[i];
        vdg_scene_frame(s, &m, &f);
        mc6847_set_artefact(&vdg, s->artefact);
        frame_rgb(&vdg, &f, got);

        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", PICO_COCO_GOLDEN_DIR, s->name);
        if (!frame_read_ppm(path, want)) {
            CHECK(false, "%s: missing or malformed; write it with vdg-ppm test/golden and "
                  "look at it first", path);
            continue;
        }
        if (memcmp(got, want, sizeof got) != 0) {
            size_t first = 0, n = 0;
            for (size_t p = 0; p < FRAME_RGB_BYTES; p += 3u)
                if (memcmp(&got[p], &want[p], 3u)) { if (!n) first = p / 3u; n++; }
            char actual[256];
            snprintf(actual, sizeof actual, "%s.actual.ppm", s->name);
            frame_write_ppm(actual, got);
            CHECK(false, "%s: %zu pixel(s) differ, the first at (%zu,%zu); drawn to %s", s->name,
                  n, first % COCO_PIC_W, first / COCO_PIC_W, actual);
        }
        compared++;
    }
    printf("%u golden image(s) compared\n", compared);
    TEST_DONE();
}
