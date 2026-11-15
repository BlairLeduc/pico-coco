/* test_boot.c — the CoCo boots its real ROMs (design.md §17 M3, §15.3).
 *
 * Color BASIC 1.3 alone, and with Extended Color BASIC 1.1, in 16, 32 and
 * 64 KB, to its banner and "OK", read from video RAM; then a sum typed
 * through the matrix and answered on the screen; every letter, digit and
 * shifted symbol typed and printed back; and PRINT MEM, the ROM's
 * own sizing of memory, against what XRoar prints, the trace diff having
 * held the two together through it (2026-11-14).
 *
 * Not 4 KB: BASIC 1.3 drives PIA0 PB6 low and reads the strap on PIA1 PB2,
 * which a 4K machine ties low, so it takes 64K mode, and 4K chips cannot
 * serve it. It hangs sizing memory, under XRoar too, which the trace diff
 * holds this core to instruction for instruction; 4K CoCos shipped with
 * BASIC 1.0 and 1.1. Skips without the ROMs, and a skip is not a pass.
 */

#include <string.h>

#include "guest.h"
#include "test_util.h"

static guest_t g;

typedef struct {
    unsigned    ram_k;
    bool        extended;
    const char *banner;    /* the screen row it starts */
    const char *mem;       /* PRINT MEM's answer, as XRoar 1.13 prints it */
} boot_t;

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: no Color BASIC 1.3 in %s (roms/README.md)\n", dir);
        return TEST_SKIP_CODE;
    }

    static const boot_t boots[] = {
        { 16, false, "COLOR BASIC 1.3",          " 14631" },
        { 32, false, "COLOR BASIC 1.3",          " 31015" },
        { 64, false, "COLOR BASIC 1.3",          " 31015" },
        { 16, true,  "EXTENDED COLOR BASIC 1.1", " 8487"  },
        { 32, true,  "EXTENDED COLOR BASIC 1.1", " 24871" },
        { 64, true,  "EXTENDED COLOR BASIC 1.1", " 24871" },
    };

    for (size_t i = 0; i < sizeof boots / sizeof boots[0]; i++) {
        const boot_t *b = &boots[i];
        if (b->extended && !guest_have_rom(ROM_COCO_EXTBAS11)) {
            printf("%u KB Extended: no extbas11.rom, left out\n", b->ram_k);
            continue;
        }
        int failures_before = test_failures;
        guest_build(&g, b->ram_k, b->extended);
        bool ok = guest_boot(&g);
        CHECK(ok, "%u KB %s: no OK in ten seconds", b->ram_k, b->banner);
        if (!ok) { guest_dump(&g.m, stderr); continue; }
        CHECK(guest_find_row(&g.m, b->banner, 0) >= 0, "%u KB: no \"%s\" on the screen",
              b->ram_k, b->banner);

        guest_type(&g, "PRINT 2+3\n");
        int r = guest_find_row(&g.m, "PRINT 2+3", 0);
        bool sum = r >= 0 && strcmp(guest_row(&g.m, r + 1), " 5") == 0 &&
                   strncmp(guest_row(&g.m, r + 2), "OK", 2) == 0;
        CHECK(sum, "%u KB %s: PRINT 2+3 not answered with 5", b->ram_k, b->banner);

        /* Every letter and digit, and the shifted symbols the matrix
         * gives, typed and printed back: a key in the wrong cell of the
         * table types the wrong character here. */
        static const char *const lines[] = {
            "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789", "@!#$%&'()*+,-./:;<=>?",
        };
        for (size_t k = 0; k < sizeof lines / sizeof lines[0]; k++) {
            char cmd[64];
            snprintf(cmd, sizeof cmd, "PRINT \"%s\"\n", lines[k]);
            guest_type(&g, cmd);
            char echo[64];
            snprintf(echo, sizeof echo, "PRINT \"%s\"", lines[k]);
            int e = guest_find_row(&g.m, echo, 0);
            CHECK(e >= 0 && strcmp(guest_row(&g.m, e + 1), lines[k]) == 0,
                  "%u KB %s: typed %s, the screen does not echo and print it", b->ram_k,
                  b->banner, cmd);
            guest_type(&g, "CLS\n");
        }

        guest_type(&g, "PRINT MEM\n");
        r = guest_find_row(&g.m, "PRINT MEM", 0);
        const char *mem = r >= 0 ? guest_row(&g.m, r + 1) : "";
        printf("%2u KB %-25s OK after %5.2f s; PRINT MEM %s\n", b->ram_k, b->banner,
               (double)g.ok_ticks / 14318180.0, mem);
        if (b->mem)
            CHECK(strcmp(mem, b->mem) == 0, "%u KB %s: PRINT MEM gave \"%s\", XRoar \"%s\"",
                  b->ram_k, b->banner, mem, b->mem);
        if (test_failures != failures_before) guest_dump(&g.m, stderr);
    }
    TEST_DONE();
}
