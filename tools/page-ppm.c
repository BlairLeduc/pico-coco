/* page-ppm.c — the missing-ROM page in each of its cases, written as
 * PPMs to be looked at (design.md §12.1, §14). Host build only.
 *
 *   build/host/page-ppm out/m7
 *
 * Renders src/port/roms.c's pages through the guest's renderer, as core
 * 1 presents them.
 */

#include <stdio.h>
#include <string.h>

#include "frame_ppm.h"
#include "mc6847_font.h"
#include "roms.h"

static coco_frame_t f;
static mc6847_t vdg;
static uint8_t rgb[FRAME_RGB_BYTES];

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    mc6847_init(&vdg);
    mc6847_set_font(&vdg, font_6847);

    struct { const char *name; card_job_t job; } cases[] = {
        { "page-nocard",   { .state = CARD_NONE, .basic = ROM_UNKNOWN } },
        { "page-unusable", { .state = CARD_UNUSABLE, .basic = ROM_UNKNOWN } },
        { "page-nodir",    { .state = CARD_MOUNTED, .basic = ROM_UNKNOWN } },
        { "page-none",     { .state = CARD_MOUNTED, .dir = true, .files = 1, .unknown = 1,
                             .basic = ROM_UNKNOWN } },
        { "page-bas13",    { .state = CARD_MOUNTED, .dir = true, .files = 4, .unknown = 2,
                             .found = { [ROM_COCO_BAS13] = true, [ROM_COCO_BAS11] = true },
                             .basic = ROM_COCO_BAS13 } },
        { "page-bas11",    { .state = CARD_MOUNTED, .dir = true, .files = 2,
                             .found = { [ROM_COCO_BAS11] = true, [ROM_COCO_EXTBAS11] = true },
                             .basic = ROM_COCO_BAS11, .extended = true } },
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        roms_page(&cases[i].job, &f);
        frame_rgb(&vdg, &f, rgb);
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", dir, cases[i].name);
        if (frame_write_ppm(path, rgb)) printf("%s\n", path);
        else { perror(path); bad = 1; }
    }
    return bad;
}
