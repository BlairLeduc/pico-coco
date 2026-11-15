/* vdg-ppm.c — the golden images' scenes, written as PPMs (design.md
 * §9.6, EL §5.7), adapted from pico-atom's.
 *
 *   vdg-ppm <outdir>        to look at
 *   vdg-ppm test/golden     regenerate the goldens test_mc6847_golden
 *                           compares against
 *
 * Regenerating blesses whatever the renderer does now: look at every
 * image that changed before committing it. Host build only.
 */

#include <stdio.h>

#include "frame_ppm.h"
#include "mc6847_font.h"
#include "vdg_scenes.h"

static coco_t m;
static coco_frame_t f;
static mc6847_t vdg;
static uint8_t rgb[FRAME_RGB_BYTES];

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    mc6847_init(&vdg);
    mc6847_set_font(&vdg, font_6847);
    int bad = 0;
    for (unsigned i = 0; i < vdg_scene_count; i++) {
        const vdg_scene_t *s = &vdg_scenes[i];
        vdg_scene_frame(s, &m, &f);
        mc6847_set_artefact(&vdg, s->artefact);
        frame_rgb(&vdg, &f, rgb);
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", dir, s->name);
        if (frame_write_ppm(path, rgb)) printf("%s\n", path);
        else { perror(path); bad = 1; }
    }
    return bad;
}
