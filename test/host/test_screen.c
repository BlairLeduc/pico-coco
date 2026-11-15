/* test_screen.c — the real ROMs' screens through the snapshot
 * (design.md §9, §15.3, §18).
 *
 * Extended Color BASIC 1.1 on a 64 KB CoCo:
 *
 *  - the boot screen's snapshot: alphanumeric, SAM mode 0, the display
 *    at $0400, and each text row as guest_row reads it from RAM;
 *  - PMODE 0 to 4 with SCREEN 1,0 and 1,1, each run from a program: the
 *    VDG mode port B drives and the SAM mode BASIC chose are the
 *    datasheet's matched pair for that PMODE, CSS follows SCREEN's colour
 *    set, and page 1 is at $0600. This executes the ROM's own wiring of
 *    PIA1 PB3-PB7 to CSS, GM0-GM2 and A/G, which settles §18's row;
 *  - a few drawn screens (text, SET's semigraphics 4, PMODE 3's colours,
 *    PMODE 4 with and without artefacts), written as PPMs to
 *    out/screens/ to be looked at. Nothing compares them: they are for
 *    eyes (EL §5.7).
 *
 * Skips without bas13.rom and extbas11.rom; a skip is not a pass.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "frame_ppm.h"
#include "guest.h"
#include "mc6847_font.h"
#include "test_util.h"

static guest_t g;
static coco_frame_t f;
static mc6847_t v;
static uint8_t rgb[FRAME_RGB_BYTES];
static char shots[512];

static void shot(const char *name) {
    char path[sizeof shots + 64];
    snprintf(path, sizeof path, "%s/%s.ppm", shots, name);
    frame_rgb(&v, &f, rgb);
    if (frame_write_ppm(path, rgb)) printf("wrote %s\n", path);
}

/* A program typed a line at a time, then RUN, then fields for it to
 * draw. A line at a time, because the ROM is still storing a long line
 * when the next key comes and misses it: the hold and gap are M5's. */
static void run(const char *program, int fields) {
    guest_build(&g, 64, true);
    guest_boot(&g);
    char buf[256];
    while (*program) {
        const char *nl = strchr(program, '\n');
        size_t n = (size_t)(nl - program) + 1u;
        memcpy(buf, program, n);
        buf[n] = 0;
        guest_type(&g, buf);
        guest_fields(&g, 30);
        program += n;
    }
    guest_type(&g, "RUN\n");
    guest_fields(&g, fields);
    coco_video_snapshot(&g.m, &f);
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: needs bas13.rom and extbas11.rom in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    snprintf(shots, sizeof shots, "%s/out/screens", PICO_COCO_SOURCE_DIR);
    mkdir(PICO_COCO_SOURCE_DIR "/out", 0777);
    mkdir(shots, 0777);
    mc6847_init(&v);
    mc6847_set_font(&v, font_6847);

    /* ---- the boot screen -------------------------------------------- */
    {
        guest_build(&g, 64, true);
        CHECK(guest_boot(&g), "Extended Color BASIC should boot");
        coco_video_snapshot(&g.m, &f);
        CHECK(!(f.mode & VDG_AG) && f.sam_v == 0 && f.base == 0x0400u,
              "boot: VDG mode %02X, SAM mode %u, at $%04X", f.mode, f.sam_v, f.base);
        for (int r = 0; r < 16; r++) {
            char text[33];
            int n = 0;
            for (unsigned c = 0; c < 32u; c++) {
                uint8_t b = f.lines[(unsigned)r * 12u * 32u + c];
                uint8_t ch = b & 0x3Fu;
                text[n++] = (b & 0x80u) ? ' ' : (char)(ch < 0x20u ? ch + 0x40u : ch);
            }
            while (n > 0 && text[n - 1] == ' ') n--;
            text[n] = 0;
            CHECK(strcmp(text, guest_row(&g.m, r)) == 0, "row %d: snapshot \"%s\", RAM \"%s\"",
                  r, text, guest_row(&g.m, r));
        }
        shot("boot");
    }

    /* ---- PMODE and SCREEN: the wiring, by the ROM ------------------- */
    {
        static const unsigned gm[5] = { 3, 4, 5, 6, 7 };   /* RG2 CG3 RG3 CG6 RG6 */
        static const unsigned sam_v[5] = { 3, 4, 5, 6, 6 };
        for (unsigned p = 0; p < 5u; p++) {
            for (unsigned css = 0; css < 2u; css++) {
                char prog[96];
                snprintf(prog, sizeof prog, "10 PMODE %u,1:PCLS:SCREEN 1,%u\n20 GOTO 20\n", p, css);
                run(prog, 20);
                uint8_t want = (uint8_t)(VDG_AG | gm[p] << VDG_GM_SHIFT | (css ? VDG_CSS : 0u));
                CHECK(f.mode == want && f.sam_v == sam_v[p] && f.base == 0x0600u,
                      "PMODE %u,1 SCREEN 1,%u: VDG %02X (want %02X), SAM %u (want %u), at $%04X",
                      p, css, f.mode, want, f.sam_v, sam_v[p], f.base);
            }
        }
    }

    /* ---- screens to look at ----------------------------------------- */
    run("10 CLS 0:FOR C=0 TO 8:SET(C*7+2,4,C):SET(C*7+3,4,C):SET(C*7+2,5,C):NEXT\n"
        "20 PRINT@256,\"SEMIGRAPHICS 4 BY SET\";\n30 GOTO 30\n", 30);
    shot("sg4");
    run("10 PMODE 3,1:PCLS:SCREEN 1,0:FOR C=1 TO 4:COLOR C:LINE(C*40,20)-(C*40+30,170),PSET,BF:NEXT\n"
        "20 GOTO 20\n", 400);
    shot("pmode3-css0");
    run("10 PMODE 3,1:PCLS:SCREEN 1,1:FOR C=5 TO 8:COLOR C:LINE((C-4)*40,20)-((C-4)*40+30,170),PSET,BF:NEXT\n"
        "20 GOTO 20\n", 400);
    shot("pmode3-css1");
    run("10 PMODE 4,1:PCLS:SCREEN 1,1:CIRCLE(128,80),60:LINE(0,0)-(255,191),PSET\n"
        "20 FOR X=0 TO 254 STEP 2:LINE(X,150)-(X,170),PSET:NEXT\n"
        "30 FOR X=1 TO 255 STEP 2:LINE(X,172)-(X,191),PSET:NEXT\n40 GOTO 40\n", 400);
    shot("pmode4-plain");
    mc6847_set_artefact(&v, VDG_ARTEFACT_KBRW);
    shot("pmode4-kbrw");
    mc6847_set_artefact(&v, VDG_ARTEFACT_KRBW);
    shot("pmode4-krbw");
    mc6847_set_artefact(&v, VDG_ARTEFACT_OFF);

    TEST_DONE();
}
