/* test_boot.c — the machines boot their real ROMs (design.md §17 M3,
 * §15.3).
 *
 * Color BASIC 1.3 alone, and with Extended Color BASIC 1.1, in 16, 32 and
 * 64 KB; Color BASIC 1.1 in 4 and 16 KB; and the Dragon 32 in 32 KB and
 * expanded to 64, the second wiring (§3.1). Each to its banner and "OK",
 * read from video RAM; then a sum typed through the matrix, the CoCo's
 * or the Dragon's, and answered on the screen; every letter, digit and
 * shifted symbol typed and printed back; a page of text written from
 * the screen (textpage.h) equal to what BASIC wrote; and PRINT MEM, the ROM's own
 * sizing of memory, against what XRoar prints, the trace diff having held
 * the two together through it (2026-11-14).
 *
 * Not BASIC 1.3 in 4 KB: it drives PIA0 PB6 low and reads the strap on
 * PIA1 PB2, which a 4K machine ties low, so it takes 64K mode, and 4K
 * chips cannot serve it. It hangs sizing memory, under XRoar too, which
 * the trace diff holds this core to instruction for instruction; 4K CoCos
 * shipped with BASIC 1.0 and 1.1, and 1.1 is here. A case whose ROM is
 * absent is left out, and with none present the test skips; a skip is not
 * a pass.
 */

#include <string.h>

#include "guest.h"
#include "test_util.h"
#include "textpage.h"

static guest_t g;
static coco_frame_t snap, page;

/* A page written from the screen's text (textpage.h) holds the bytes
 * Color BASIC wrote, line for line where the VDG fetches them: the page's
 * writing and the screen's reading agree with the ROM's, and a page is
 * laid out as the SAM's counter lays out the text screen. Semigraphics
 * cells (the cursor) read as spaces and are not compared. Returns the
 * cells that differ. */
static unsigned page_against_screen(const coco_t *m, unsigned *compared) {
    coco_video_snapshot(m, &snap);
    textpage_clear(&page);
    for (int r = 0; r < TEXT_ROWS; r++) textpage_line(&page, r, guest_row(m, r), false);
    unsigned bad = 0;
    *compared = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        for (unsigned c = 0; c < TEXT_COLS; c++) {
            uint8_t b = snap.lines[y * COCO_VDG_LINE_BYTES + c];
            if (b & VDG_BYTE_AS) continue;
            (*compared)++;
            if (page.lines[y * COCO_VDG_LINE_BYTES + c] != b) bad++;
        }
    }
    return bad;
}

typedef struct {
    coco_machine_t machine;
    rom_id_t    basic;
    unsigned    ram_k;
    bool        extended;
    const char *banner;    /* the screen row it starts */
    const char *mem;       /* PRINT MEM's answer, as XRoar 1.13 prints it */
} boot_t;

int main(void) {
    const char *dir;
    guest_find_roms(&dir);

    static const boot_t boots[] = {
        { COCO_MACHINE_COCO,     ROM_COCO_BAS11, 4,  false, "COLOR BASIC 1.1",          " 2343"  },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS11, 16, false, "COLOR BASIC 1.1",          " 14631" },
        /* The board's fallback without 1.3 (src/port/roms.c). */
        { COCO_MACHINE_COCO,     ROM_COCO_BAS11, 64, false, "COLOR BASIC 1.1",          NULL     },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 16, false, "COLOR BASIC 1.3",          " 14631" },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 32, false, "COLOR BASIC 1.3",          " 31015" },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 64, false, "COLOR BASIC 1.3",          " 31015" },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 16, true,  "EXTENDED COLOR BASIC 1.1", " 8487"  },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 32, true,  "EXTENDED COLOR BASIC 1.1", " 24871" },
        { COCO_MACHINE_COCO,     ROM_COCO_BAS13, 64, true,  "EXTENDED COLOR BASIC 1.1", " 24871" },
        { COCO_MACHINE_DRAGON32, ROM_DRAGON32,   32, false, "(C) 1982 DRAGON DATA LTD", " 24871" },
        { COCO_MACHINE_DRAGON32, ROM_DRAGON32,   64, false, "(C) 1982 DRAGON DATA LTD", " 24871" },
    };

    int ran = 0;
    for (size_t i = 0; i < sizeof boots / sizeof boots[0]; i++) {
        const boot_t *b = &boots[i];
        if (!guest_have_rom(b->basic) || (b->extended && !guest_have_rom(ROM_COCO_EXTBAS11))) {
            printf("%u KB %s: no %s, left out\n", b->ram_k, b->banner,
                   guest_have_rom(b->basic) ? "extbas11.rom" : romset_images[b->basic].file);
            continue;
        }
        ran++;
        int failures_before = test_failures;
        guest_build_as(&g, b->machine, b->ram_k, b->basic, b->extended);
        bool ok = guest_boot(&g);
        CHECK(ok, "%u KB %s: no OK in ten seconds", b->ram_k, b->banner);
        if (!ok) { guest_dump(&g.m, stderr); continue; }
        CHECK(guest_find_row(&g.m, b->banner, 0) >= 0, "%u KB: no \"%s\" on the screen",
              b->ram_k, b->banner);
        if (b->machine == COCO_MACHINE_COCO) {
            unsigned compared, bad = page_against_screen(&g.m, &compared);
            CHECK(bad == 0 && compared > 180u * TEXT_COLS,
                  "%u KB %s: a page of the screen's text differs in %u of %u line bytes",
                  b->ram_k, b->banner, bad, compared);
        }

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
    if (!ran) {
        printf("skipped: no BASIC ROM in %s (roms/README.md)\n", dir);
        return TEST_SKIP_CODE;
    }
    TEST_DONE();
}
