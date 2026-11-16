/* test_roms.c — which BASIC a CoCo of each size runs (roms.h, design.md
 * §14): 4 KB is Color BASIC 1.1 alone, Extended needs 16 KB and 1.3.
 * Needs no ROM: the card's job is described, not read.
 */

#include <string.h>

#include "roms.h"
#include "test_util.h"

static card_job_t job(rom_id_t basic, bool ext, bool bas11) {
    card_job_t j;
    memset(&j, 0, sizeof j);
    j.state = CARD_MOUNTED;
    j.basic = basic;
    j.extended = ext;
    j.bas11 = bas11;
    return j;
}

int main(void) {
    const char *why;
    bool ext;
    card_job_t all = job(ROM_COCO_BAS13, true, true);

    /* 4 KB: 1.1, never 1.3, never Extended. */
    ext = true;
    CHECK(roms_basic_for(&all, 4, &ext, &why) == ROM_COCO_BAS11 && !ext && why,
          "4 KB asked for Extended: 1.1 alone, and said so");
    ext = false;
    CHECK(roms_basic_for(&all, 4, &ext, &why) == ROM_COCO_BAS11 && !ext && !why,
          "4 KB: 1.1, nothing to say");
    card_job_t no11 = job(ROM_COCO_BAS13, true, false);
    ext = false;
    CHECK(roms_basic_for(&no11, 4, &ext, &why) == ROM_UNKNOWN && why,
          "4 KB without 1.1 runs nothing: 1.3 is not taken");

    /* 16 KB and up: Extended over 1.3, or a Color BASIC alone. */
    for (unsigned k = 16; k <= 64; k *= 2) {
        ext = true;
        CHECK(roms_basic_for(&all, k, &ext, &why) == ROM_COCO_BAS13 && ext && !why,
              "%u KB: Extended over 1.3", k);
        ext = false;
        CHECK(roms_basic_for(&all, k, &ext, &why) == ROM_COCO_BAS13 && !ext,
              "%u KB: Color BASIC 1.3 alone", k);
    }
    card_job_t only11 = job(ROM_COCO_BAS11, true, true);
    ext = true;
    CHECK(roms_basic_for(&only11, 64, &ext, &why) == ROM_COCO_BAS11 && !ext && why,
          "Extended without 1.3: 1.1 alone, and said so");
    card_job_t noext = job(ROM_COCO_BAS13, false, false);
    ext = true;
    CHECK(roms_basic_for(&noext, 16, &ext, &why) == ROM_COCO_BAS13 && !ext && why,
          "no extbas11.rom: 1.3 alone, and said so");
    card_job_t none = job(ROM_UNKNOWN, false, false);
    ext = false;
    CHECK(roms_basic_for(&none, 64, &ext, &why) == ROM_UNKNOWN && why, "no BASIC at all");

    TEST_DONE();
}
