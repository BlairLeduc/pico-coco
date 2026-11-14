/* test_skeleton.c — M0 (design.md §17). Compiles the core's headers with
 * the system compiler and no Pico SDK, so an SDK #include in one of them
 * fails the host build (EL §2.1), and checks config.h's sizes against each
 * other. */

#include "config.h"
#include "hot.h"
#include "test_util.h"

/* A host build is tier 0: the macro must leave an ordinary function. */
static int COCO_HOT2(hot_identity)(int x) { return x; }

int main(void) {
    CHECK(hot_identity(42) == 42, "COCO_HOT2 changed the function");

    CHECK(COCO_PAGE_COUNT * COCO_PAGE_SIZE == COCO_ADDR_SPACE,
          "%u pages of %u bytes", COCO_PAGE_COUNT, COCO_PAGE_SIZE);
    CHECK(COCO_IO_PAGE == COCO_PAGE_COUNT - 1u,
          "the I/O page is not the last, where the vectors are (§3.2)");

    CHECK(COCO_PIC_X + COCO_PIC_W <= COCO_PANEL_W &&
              COCO_PIC_Y + COCO_PIC_H <= COCO_PANEL_H,
          "the picture does not fit the panel (§9.6)");
    CHECK(COCO_SNAP_VRAM_MAX == (COCO_PIC_W / 8u) * COCO_PIC_H,
          "%u bytes is not one 32-byte row per scanline (§9.3)",
          COCO_SNAP_VRAM_MAX);

    TEST_DONE();
}
