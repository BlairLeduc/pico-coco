/* test_video.c — the snapshot through the machine (design.md §9, §13.1).
 *
 * Without the real ROMs, so it runs in CI:
 *
 *  - the SAM's counter, read back through coco_video_snapshot: the
 *    address each line starts at, and that its bytes lie in order, in
 *    each of the datasheet's nine modes at its matched SAM mode, in the
 *    semigraphics 8, 12 and 24 pairs, and wrapping past $FFFF;
 *  - §9.4's count of fields whose video bits changed during active
 *    lines, with a program that writes the same bits as the control;
 *    and the changes themselves: a program that goes from text to RG6
 *    part-way down every field has a snapshot whose lines are fetched
 *    as text above the change and as RG6 below it, the counter carried
 *    across, and one that sets CSS for part of a line has that line
 *    drawn in the other colour set between the two pixels;
 *  - EL §5.6's field shape: a guest loop that waits for FS to fall
 *    escapes once a field, and a redraw it starts then is complete in
 *    every snapshot taken where the next active line begins, while
 *    snapshots taken where FS rises, the control, catch it half done.
 *
 * test_vdg_oracle holds the counter to XRoar's for random bytes; this
 * says what it does.
 */

#include <string.h>

#include "coco.h"
#include "present.h"
#include "test_util.h"

static coco_t m, m2;
static coco_frame_t f;
static uint8_t rom[0x2000];

/* present_render_line's renderer: one table, set to the mode. */
static const mc6847_t *vdg_of(void *ctx, uint8_t mode) {
    mc6847_set_mode(ctx, mode);
    return ctx;
}

static void machine(void) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    coco_load_rom(&m, 0xA000, rom, sizeof rom);
    coco_power_on(&m);
}

/* A machine at rest in the SAM's 64K mode with the VDG and SAM in the
 * given modes and the display at `base`. */
static void at_rest(uint8_t vdg_mode, unsigned v, uint16_t base) {
    memset(rom, 0x12, sizeof rom);
    rom[0] = 0x20; rom[1] = 0xFE;                 /* $A000 BRA * */
    rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
    machine();
    m.sam.reg = (uint16_t)(v | (base >> 9) << 3 | 2u << 13);
    m.pia1.b.ddr = 0xF8u;
    m.pia1.b.out = vdg_mode;
    /* Set behind the machine's back: the picture's mode with it, and a
     * field run, since the counter loaded F as the first began. */
    m.vid_shown = coco_vdg_mode(&m);
    coco_run_field(&m);
}

/* The address each shown byte was fetched from, from two fields'
 * snapshots: one of RAM holding each address's low byte, one its high.
 * Each field's lines are fetched as it runs (§9.3); with `walk`, the
 * snapshot is taken with no field run, from RAM as it is, as after a
 * power-on or a restored state. */
static uint16_t addr[COCO_PIC_H][COCO_VDG_LINE_BYTES];
static bool s_walk;
static void snap_now(void) {
    if (s_walk) m.vid_whole = false;
    else coco_run_field(&m);
    coco_video_snapshot(&m, &f);
}
static void addresses(void) {
    for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = (uint8_t)a;
    snap_now();
    for (unsigned i = 0; i < COCO_SNAP_VRAM_MAX; i++) addr[i / 32u][i % 32u] = f.lines[i];
    for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = (uint8_t)(a >> 8);
    snap_now();
    for (unsigned i = 0; i < COCO_SNAP_VRAM_MAX; i++)
        addr[i / 32u][i % 32u] = (uint16_t)(addr[i / 32u][i % 32u] | f.lines[i] << 8);
}

/* Every line starts at base + (y / per) * stride and runs in order for
 * `shown` bytes. */
static int check_layout(const char *what, uint16_t base, unsigned per, unsigned stride,
                        unsigned shown) {
    unsigned bad = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        for (unsigned c = 0; c < shown; c++) {
            uint16_t want = (uint16_t)(base + (y / per) * stride + c);
            if (addr[y][c] != want) {
                if (!bad)
                    fprintf(stderr, "    %s: line %u byte %u from $%04X, not $%04X\n", what, y,
                            c, addr[y][c], want);
                bad++;
            }
        }
    }
    CHECK(bad == 0, "%s: %u byte(s) from the wrong address", what, bad);
    return 0;
}

int main(void) {
    /* ---- the matched modes ------------------------------------------ */
    {
        static const struct {
            uint8_t mode; unsigned v, per, stride, shown; const char *name;
        } modes[] = {
            { 0x00, 0, 12, 32, 32, "alpha" },
            { 0x80, 1,  3, 16, 16, "CG1" },
            { 0x90, 1,  3, 16, 16, "RG1" },
            { 0xA0, 2,  3, 32, 32, "CG2" },
            { 0xB0, 3,  2, 16, 16, "RG2" },
            { 0xC0, 4,  2, 32, 32, "CG3" },
            { 0xD0, 5,  1, 16, 16, "RG3" },
            { 0xE0, 6,  1, 32, 32, "CG6" },
            { 0xF0, 6,  1, 32, 32, "RG6" },
            /* Semigraphics 8, 12 and 24: a text-mode VDG fed a new row
             * every 3, 2 and 1 lines (§9.2). */
            { 0x00, 2,  3, 32, 32, "SG8" },
            { 0x00, 4,  2, 32, 32, "SG12" },
            { 0x00, 6,  1, 32, 32, "SG24" },
        };
        /* Fetched as each field runs, then walked from RAM as it is. */
        for (unsigned walk = 0; walk < 2u; walk++) {
            s_walk = walk != 0;
            for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
                at_rest(modes[i].mode, modes[i].v, 0x0E00u);
                addresses();
                CHECK(f.mode == modes[i].mode && f.sam_v == modes[i].v && f.base == 0x0E00u,
                      "%s%s: the frame's modes", modes[i].name, walk ? ", walked" : "");
                if (check_layout(modes[i].name, 0x0E00u, modes[i].per, modes[i].stride,
                                 modes[i].shown)) return 1;
            }
        }
        s_walk = false;

        /* The counter wraps past $FFFF: RG6 from $FE00 reaches $0000 at
         * line 16. */
        at_rest(0xF8, 6, 0xFE00u);
        addresses();
        CHECK(addr[15][31] == 0xFFFFu && addr[16][0] == 0x0000u,
              "RG6 from $FE00: line 16 from $%04X", addr[16][0]);

        /* A 16-byte mode's other 16 bytes are zero, not left over. */
        at_rest(0x90, 1, 0x0400u);
        for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = 0xA5u;
        snap_now();
        CHECK(f.lines[16] == 0 && f.lines[31] == 0 && f.lines[15] == 0xA5u,
              "RG1's line: 16 bytes shown, the rest zero");
    }

    /* ---- §9.4: fields with more than one mode ----------------------- */
    {
        /* PIA1 port B made outputs, then a loop writing $FF22 forever:
         * alternately $00 and $08 (CSS), or $00 twice, the control. */
        static const uint8_t code[] = {
            0x86, 0xF8,           /* $A000 LDA #$F8                       */
            0xB7, 0xFF, 0x22,     /* $A002 STA $FF22  DDR B (CRB is 0)    */
            0x86, 0x04,           /* $A005 LDA #$04                       */
            0xB7, 0xFF, 0x23,     /* $A007 STA $FF23  data B selected     */
            0x86, 0x00,           /* $A00A LDA #$00                       */
            0xB7, 0xFF, 0x22,     /* $A00C STA $FF22                      */
            0x86, 0x08,           /* $A00F LDA #$08   (patched to $00)    */
            0xB7, 0xFF, 0x22,     /* $A011 STA $FF22                      */
            0x20, 0xF4,           /* $A014 BRA $A00A                      */
        };
        for (unsigned control = 0; control < 2u; control++) {
            memset(rom, 0x12, sizeof rom);
            memcpy(rom, code, sizeof code);
            if (control) rom[0x10] = 0x00;
            rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
            machine();
            for (int i = 0; i < 10; i++) coco_run_field(&m);
            if (control)
                CHECK(m.split_fields == 0, "the same bits written: %u split fields",
                      (unsigned)m.split_fields);
            else
                CHECK(m.split_fields >= 9u, "CSS toggled throughout: %u of 10 fields split",
                      (unsigned)m.split_fields);
        }
    }

    /* ---- §9.4: the regions ------------------------------------------ */
    {
        /* Wait for FS's fall; in the flyback, the SAM's V to 0 and the
         * VDG to text; a delay of 1,211 loops of 8 cycles, which ends
         * about 100 lines down the next field's picture; then RG6 with
         * CSS and V to 6. The control writes the text's bits again. */
        static const uint8_t code[] = {
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A000 LDA #$F8, STA $FF22   */
            0x86, 0x04, 0xB7, 0xFF, 0x23,   /* $A005 LDA #$04, STA $FF23   */
            0xB7, 0xFF, 0x03,               /* $A00A STA $FF03             */
            0xB6, 0xFF, 0x03, 0x2A, 0xFB,   /* $A00D LDA $FF03, BPL $A00D  */
            0xB6, 0xFF, 0x02,               /* $A012 LDA $FF02             */
            0xB7, 0xFF, 0xC2,               /* $A015 STA $FFC2  V1 clear   */
            0xB7, 0xFF, 0xC4,               /* $A018 STA $FFC4  V2 clear   */
            0x7F, 0xFF, 0x22,               /* $A01B CLR $FF22  text       */
            0x8E, 0x04, 0xBB,               /* $A01E LDX #1211             */
            0x30, 0x1F, 0x26, 0xFC,         /* $A021 LEAX -1,X, BNE $A021  */
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A025 LDA #$F8, STA $FF22   */
            0xB7, 0xFF, 0xC3,               /* $A02A STA $FFC3  V1 set     */
            0xB7, 0xFF, 0xC5,               /* $A02D STA $FFC5  V2 set     */
            0x20, 0xDB,                     /* $A030 BRA $A00D             */
        };
        for (unsigned control = 0; control < 2u; control++) {
            memset(rom, 0x12, sizeof rom);
            memcpy(rom, code, sizeof code);
            if (control) {
                rom[0x26] = 0x00;
                rom[0x2C] = 0xC2;
                rom[0x2F] = 0xC4;
            }
            rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
            machine();
            m.sam.reg |= 2u << 13;   /* 64K chips, as at_rest has them */
            for (int i = 0; i < 6; i++) coco_run_field(&m);
            addresses();
            if (control) {
                unsigned plain = 0;
                for (unsigned y = 0; y < COCO_PIC_H; y++)
                    plain += f.how[y].mode == 0 && !f.how[y].x[0];
                CHECK(f.splits == 0 && f.mode == 0 && plain == COCO_PIC_H,
                      "the control: %u changes, mode %02X, %u plain lines", f.splits, f.mode,
                      plain);
                check_layout("the control, text", 0, 12, 32, 32);
                continue;
            }
            /* The first line that begins in RG6, and the one before it,
             * where the three writes fell: 13 cycles end to end, so the
             * line shows up to three changes along it, or none if they
             * fell in its border. */
            unsigned at = 0;
            while (at < COCO_PIC_H && f.how[at].mode == 0) at++;
            CHECK(f.mode == 0 && f.sam_v == 0 && f.splits >= 1 && f.splits <= 3 &&
                      at >= 99u && at <= 102u,
                  "text above: mode %02X, V %u, %u changes, RG6 from line %u", f.mode, f.sam_v,
                  f.splits, at);
            unsigned bad = 0, wrong = 0;
            for (unsigned y = 0; y < COCO_PIC_H; y++) {
                /* Text: a row of 32 every 12 lines. RG6: one every line,
                 * from the row the text had reached. */
                unsigned row = y < at ? y / 12u : at / 12u + (y - at);
                for (unsigned c = 0; c < 32u; c++) bad += addr[y][c] != row * 32u + c;
                wrong += f.how[y].mode != (y < at ? 0u : 0xF8u);
                if (y != at - 1u) wrong += f.how[y].x[0] != 0;
            }
            CHECK(bad == 0, "%u byte(s) of the two regions from the wrong address", bad);
            CHECK(wrong == 0 && coco_frame_border_mode(&f) == 0,
                  "%u line(s) in the wrong mode; the border the text's, which has more lines",
                  wrong);
            /* The same next field: the split is steady. */
            coco_run_field(&m);
            coco_video_snapshot(&m, &f);
            if (at == 0 || at >= COCO_PIC_H) continue;   /* failed above */
            CHECK(f.how[at].mode == 0xF8u && f.how[at - 1u].mode == 0,
                  "the next field's RG6 not from line %u", at);
            printf("regions: text to line %u, RG6 from %u, %u change(s) along line %u\n",
                   at - 1u, at, (unsigned)(f.how[at - 1u].x[0] != 0) + (f.how[at - 1u].x[1] != 0),
                   at - 1u);
        }
    }

    /* ---- §9.4: a change along a line -------------------------------- */
    {
        /* Dark Star's (docs/milestones.md, M12): in RG6, wait for FS's
         * fall, delay to a line of the next picture, set CSS, wait and
         * clear it, 29 cycles later here, 464 ticks, 232 pixels, so that
         * both fall on one line (Dark Star's are 30 cycles apart). */
        static const uint8_t code[] = {
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A000 LDA #$F8, STA $FF22   */
            0x86, 0x04, 0xB7, 0xFF, 0x23,   /* $A005 LDA #$04, STA $FF23   */
            0xB7, 0xFF, 0x03,               /* $A00A STA $FF03             */
            0xB7, 0xFF, 0xC3,               /* $A00D STA $FFC3  V1 set     */
            0xB7, 0xFF, 0xC5,               /* $A010 STA $FFC5  V2 set     */
            0xC6, 0xF0, 0xF7, 0xFF, 0x22,   /* $A013 LDB #$F0, STB $FF22   */
            0xB6, 0xFF, 0x03, 0x2A, 0xFB,   /* $A018 LDA $FF03, BPL $A018  */
            0xB6, 0xFF, 0x02,               /* $A01D LDA $FF02             */
            0x8E, 0x04, 0xC1,               /* $A020 LDX #1217             */
            0x30, 0x1F, 0x26, 0xFC,         /* $A023 LEAX -1,X, BNE $A023  */
            0x21, 0x00, 0x21, 0x00,         /* $A027 BRN, BRN: 6 cycles,
                                             * which put the change at a
                                             * line's eighth pixel, the
                                             * PIA's 7 included
                                             * (COCO_PIA_VIDEO_TICKS)      */
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A02B LDA #$F8, STA $FF22   */
            0x8E, 0x00, 0x00,               /* $A030 LDX #0        3       */
            0x30, 0x1F, 0x30, 0x1F,         /* $A033 LEAX -1,X x 3 15      */
            0x30, 0x1F,
            0x21, 0x00,                     /* $A039 BRN           3       */
            0x3A,                           /* $A03B ABX           3       */
            0xF7, 0xFF, 0x22,               /* $A03C STB $FF22     5       */
            0x20, 0xD7,                     /* $A03F BRA $A018             */
        };
        /* A loop more, 64 pixels on: the second change falls after the
         * line's pixels, and is the next line's from its first. */
        memset(rom, 0x12, sizeof rom);
        memcpy(rom, code, sizeof code);
        rom[0x22] = 0xC2;
        rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
        machine();
        for (int i = 0; i < 6; i++) coco_run_field(&m);
        coco_video_snapshot(&m, &f);
        {
            unsigned at = 0;
            while (at < COCO_PIC_H - 2u && !f.how[at].x[0]) at++;
            const coco_line_t *h = &f.how[at];
            CHECK(f.splits == 2 && h->mode == 0xF0u && h->x[0] == 72 && h->m[0] == 0xF8u &&
                      !h->x[1] && h[1].mode == 0xF0u && !h[1].x[0] && h[-1].mode == 0xF0u,
                  "a change past the pixels: line %u %02X, %02X from %u, then %u; the next "
                  "line %02X, then %u", at, h->mode, h->m[0], h->x[0], h->x[1], h[1].mode,
                  h[1].x[0]);
        }

        memset(rom, 0x12, sizeof rom);
        memcpy(rom, code, sizeof code);
        rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
        machine();
        for (int i = 0; i < 6; i++) coco_run_field(&m);
        coco_video_snapshot(&m, &f);
        /* One line with two changes, 232 pixels apart: the first to CSS,
         * the second back. */
        unsigned n = 0, at = 0;
        for (unsigned y = 0; y < COCO_PIC_H; y++)
            if (f.how[y].x[0]) { n++; at = y; }
        const coco_line_t *how = &f.how[at];
        CHECK(f.mode == 0xF0u && f.splits == 2 && n >= 1,
              "mode %02X, %u changes, on %u line(s)", f.mode, f.splits, n);
        if (n == 1 && how->x[1]) {
            CHECK(how->mode == 0xF0u && how->m[0] == 0xF8u && how->m[1] == 0xF0u &&
                      how->x[1] - how->x[0] == 232,
                  "line %u: %02X, %02X from pixel %u, %02X from %u", at, how->mode, how->m[0],
                  how->x[0], how->m[1], how->x[1]);
            /* Drawn: buff where CSS was set and the byte's bits are, the
             * other set's green elsewhere. */
            static mc6847_t vdg;
            mc6847_init(&vdg);
            mc6847_set_artefact(&vdg, VDG_ARTEFACT_OFF);
            memset(f.lines, 0xFF, sizeof f.lines);
            uint16_t px[COCO_PIC_W], tmp[COCO_PIC_W];
            present_render_line(&f, at, vdg_of, &vdg, px, tmp);
            unsigned bad = 0;
            for (unsigned x = 0; x < COCO_PIC_W; x++) {
                bool css = x >= how->x[0] && x < how->x[1];
                bad += px[x] != mc6847_palette[css ? VDG_BUFF : VDG_GREEN];
            }
            CHECK(bad == 0, "%u pixel(s) of line %u in the wrong colour set", bad, at);
            present_render_line(&f, at + 1u, vdg_of, &vdg, px, tmp);
            CHECK(px[0] == mc6847_palette[VDG_GREEN] && px[128] == px[0],
                  "the line after it is in the first colour set throughout");
            printf("along a line: line %u, CSS from pixel %u to %u\n", at, how->x[0], how->x[1]);
        } else {
            CHECK(false, "the two changes fell on %u line(s), the last line %u: %02X, %02X "
                  "from %u, %02X from %u, and line %u begins %02X: move the test's delay", n,
                  at, how->mode, how->m[0], how->x[0], how->m[1], how->x[1], at + 1u,
                  f.how[at + 1u].mode);
        }
    }

    /* ---- §9.4: six changes along a line ----------------------------- */
    {
        /* Dragonfire's moat (docs/milestones.md, M13): CSS set and
         * cleared three times along one line, as the game does on the
         * moat's lower edge. Six stores, 5 cycles, 40 pixels apart. */
        static const uint8_t code[] = {
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A000 LDA #$F8, STA $FF22   */
            0x86, 0x04, 0xB7, 0xFF, 0x23,   /* $A005 LDA #$04, STA $FF23   */
            0xB7, 0xFF, 0x03,               /* $A00A STA $FF03             */
            0xB7, 0xFF, 0xC3,               /* $A00D STA $FFC3  V1 set     */
            0xB7, 0xFF, 0xC5,               /* $A010 STA $FFC5  V2 set     */
            0xC6, 0xF0, 0xF7, 0xFF, 0x22,   /* $A013 LDB #$F0, STB $FF22   */
            0xB6, 0xFF, 0x03, 0x2A, 0xFB,   /* $A018 LDA $FF03, BPL $A018  */
            0xB6, 0xFF, 0x02,               /* $A01D LDA $FF02             */
            0x8E, 0x04, 0xC1,               /* $A020 LDX #1217             */
            0x30, 0x1F, 0x26, 0xFC,         /* $A023 LEAX -1,X, BNE $A023  */
            0x20, 0x01, 0x12, 0x12,         /* $A027 BRA over a NOP, NOP   */
            0x86, 0xF8, 0xB7, 0xFF, 0x22,   /* $A02B LDA #$F8, STA $FF22   */
            0xF7, 0xFF, 0x22,               /* $A030 STB $FF22             */
            0xB7, 0xFF, 0x22,               /* $A033 STA $FF22             */
            0xF7, 0xFF, 0x22,               /* $A036 STB $FF22             */
            0xB7, 0xFF, 0x22,               /* $A039 STA $FF22             */
            0xF7, 0xFF, 0x22,               /* $A03C STB $FF22             */
            0x20, 0xD7,                     /* $A03F BRA $A018             */
        };
        memset(rom, 0x12, sizeof rom);
        memcpy(rom, code, sizeof code);
        rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
        machine();
        for (int i = 0; i < 6; i++) coco_run_field(&m);
        coco_video_snapshot(&m, &f);
        unsigned at = 0;
        while (at < COCO_PIC_H - 1u && !f.how[at].x[0]) at++;
        const coco_line_t *how = &f.how[at];
        unsigned k = 0;
        bool apart = true;
        while (k < COCO_LINE_SPLITS && how->x[k]) {
            if (k && (how->x[k] - how->x[k - 1u] != 40 ||
                      how->m[k] != ((k & 1u) ? 0xF0u : 0xF8u)))
                apart = false;
            k++;
        }
        CHECK(f.splits == 6 && k == 6 && apart && how->mode == 0xF0u,
              "six changes on line %u: %u kept of %u, 40 pixels apart in turn %d", at, k,
              f.splits, apart);
        printf("six along a line: line %u, changes at", at);
        for (unsigned i = 0; i < k; i++) printf(" %u", how->x[i]);
        printf("\n");
    }

    /* ---- EL §5.6: the field's shape --------------------------------- */
    {
        /* SAM F1 set, so the screen is at $0400. PIA0's CRB selects its
         * data register, CB1 on a fall (FS), no interrupt. Then, for
         * ever: wait for CB1's flag, clear it, count in B, and write B to
         * the first 256 bytes of the screen, 13 cycles a byte: 58 lines,
         * longer than the 32 FS is low and shorter than the 70 lines from
         * its fall to the next active line. */
        static const uint8_t code[] = {
            0xB7, 0xFF, 0xC9,           /* $A000 STA $FFC9  F1: $0400     */
            0x86, 0x04,                 /* $A003 LDA #$04                 */
            0xB7, 0xFF, 0x03,           /* $A005 STA $FF03                */
            0x5F,                       /* $A008 CLRB                     */
            0xB6, 0xFF, 0x03,           /* $A009 LDA $FF03                */
            0x2A, 0xFB,                 /* $A00C BPL $A009                */
            0xB6, 0xFF, 0x02,           /* $A00E LDA $FF02  clear the flag */
            0x5C,                       /* $A011 INCB                     */
            0x8E, 0x04, 0x00,           /* $A012 LDX #$0400               */
            0xE7, 0x80,                 /* $A015 STB ,X+                  */
            0x8C, 0x05, 0x00,           /* $A017 CMPX #$0500              */
            0x26, 0xF9,                 /* $A01A BNE $A015                */
            0x20, 0xEB,                 /* $A01C BRA $A009                */
        };
        memset(rom, 0x12, sizeof rom);
        memcpy(rom, code, sizeof code);
        rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
        machine();
        coco_run_field(&m);
        coco_run_field(&m);

        unsigned torn = 0, torn_control = 0, steps = 0;
        uint8_t last = 0;
        for (int field = 0; field < 120; field++) {
            /* The control: a copy run only to where FS rises, and RAM
             * walked there, as the snapshot was taken before each line
             * was fetched as it ran (§9.3). */
            coco_copy(&m2, &m);
            uint64_t rise = m2.field_start + (uint64_t)COCO_FIELD_LINES * COCO_LINE_TICKS;
            (void)coco_run(&m2, (uint32_t)(rise - coco_ticks(&m2)));
            m2.vid_whole = false;
            coco_video_snapshot(&m2, &f);
            for (unsigned i = 1; i < 256u; i++)
                if (f.lines[(i / 32u) * 12u * 32u + i % 32u] != f.lines[0]) { torn_control++; break; }

            coco_run_field(&m);
            coco_video_snapshot(&m, &f);
            /* The 256 bytes are rows 0-7: lines 0-95, 12 to a row. */
            uint8_t b = f.lines[0];
            bool whole = true;
            for (unsigned y = 0; y < 96u; y++)
                for (unsigned c = 0; c < 32u; c++)
                    if (f.lines[y * 32u + c] != b) whole = false;
            if (!whole) torn++;
            if (field > 0 && b == (uint8_t)(last + 1u)) steps++;
            last = b;
        }
        CHECK(torn == 0, "%u of 120 snapshots caught the redraw half done", torn);
        CHECK(steps == 119u, "the loop escaped once a field in %u of 119", steps);
        CHECK(torn_control >= 119u,
              "the control, snapshots where FS rises, should catch the redraw: %u of 120",
              torn_control);
    }

    /* ---- §9.3: a byte drawn in step with the beam ------------------- */
    {
        /* Dragonfire's room (docs/milestones.md, M13): the screen at
         * $0400 in text, 12 lines a row. Wait for FS's fall, line 230;
         * 7,408 cycles on, line 60 of the next field, write $FF to row
         * 9's first byte, which lines 108-119 show; 5,128 cycles on, line
         * 150, after the beam has passed it, write $00 back. Each line
         * fetched as it is shown has the $FF; RAM where FS falls, the
         * control, has $00. */
        static const uint8_t code[] = {
            0xB7, 0xFF, 0xC9,           /* $A000 STA $FFC9  F1: $0400     */
            0x86, 0x04,                 /* $A003 LDA #$04                 */
            0xB7, 0xFF, 0x03,           /* $A005 STA $FF03                */
            0xB6, 0xFF, 0x03,           /* $A008 LDA $FF03                */
            0x2A, 0xFB,                 /* $A00B BPL $A008                */
            0xB6, 0xFF, 0x02,           /* $A00D LDA $FF02  clear the flag */
            0x8E, 0x03, 0x9E,           /* $A010 LDX #926                 */
            0x30, 0x1F, 0x26, 0xFC,     /* $A013 LEAX -1,X, BNE $A013     */
            0x86, 0xFF, 0xB7, 0x05, 0x20, /* $A017 LDA #$FF, STA $0520    */
            0x8E, 0x02, 0x81,           /* $A01C LDX #641                 */
            0x30, 0x1F, 0x26, 0xFC,     /* $A01F LEAX -1,X, BNE $A01F     */
            0x7F, 0x05, 0x20,           /* $A023 CLR $0520                */
            0x20, 0xE0,                 /* $A026 BRA $A008                */
        };
        /* Twice: with the SAM's memory size left at 4K, where the CPU's
         * pages lie out of order in the chips and every write takes the
         * slow path; and set to 64K first ($FFDD, the same code 3 bytes
         * on), where the screen's pages lie in order and their writes
         * take the short path of a page the VDG may fetch (coco.c). */
        for (unsigned k64 = 0; k64 < 2u; k64++) {
            memset(rom, 0x12, sizeof rom);
            if (k64) {
                rom[0] = 0xB7; rom[1] = 0xFF; rom[2] = 0xDD;   /* STA $FFDD */
            }
            memcpy(rom + 3u * k64, code, sizeof code);
            rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
            machine();
            for (int i = 0; i < 4; i++) coco_run_field(&m);
            const char *how_written = k64 ? "64K, in order" : "4K, out of order";
            int32_t i520 = coco_ram_index(&m, 0x0520u);
            CHECK(k64 ? (m.page[0x05].read && !m.page[0x05].write) : !m.page[0x05].read,
                  "%s: page $05 not as expected", how_written);
            coco_video_snapshot(&m, &f);
            unsigned shown = 0, elsewhere = 0;
            for (unsigned y = 0; y < COCO_PIC_H; y++) {
                bool row9 = y >= 108u && y < 120u;
                if (row9) shown += f.lines[y * 32u] == 0xFFu;
                else elsewhere += f.lines[y * 32u] == 0xFFu;
            }
            CHECK(shown == 12u && elsewhere == 0 && i520 >= 0 && m.ram[i520] == 0,
                  "%s: the byte drawn ahead of the beam: on %u of row 9's 12 lines, %u "
                  "elsewhere", how_written, shown, elsewhere);
            m.vid_whole = false;
            coco_video_snapshot(&m, &f);
            unsigned walked = 0;
            for (unsigned y = 108; y < 120u; y++) walked += f.lines[y * 32u] == 0xFFu;
            CHECK(walked == 0, "%s: the control, RAM walked where FS falls: on %u of 12 lines",
                  how_written, walked);
            printf("in step with the beam, %s: shown on row 9's %u lines, none when RAM is "
                   "walked\n", how_written, shown);
        }
    }

    /* ---- §9.3: a reset in the middle of a field ---------------------- */
    {
        /* Running in 64K with the screen at $0400, the machine is reset at
         * picture line 50: the SAM goes to 4K, which reorganises the chips,
         * while the field keeps the base FS's rise loaded. The code the
         * reset runs ($A100) waits until line 135, after the beam has
         * passed row 9 (lines 108-119), and writes $FF to its first byte.
         * Row 9's lines show what was there, $00, only if the pages the
         * VDG may fetch were worked out again for 4K at the reset (PR #16's
         * review); RAM walked where FS falls, the control, has the $FF. */
        memset(rom, 0x12, sizeof rom);
        static const uint8_t run64[] = {
            0xB7, 0xFF, 0xDD,           /* $A000 STA $FFDD  64K           */
            0xB7, 0xFF, 0xC9,           /* $A003 STA $FFC9  F1: $0400     */
            0x20, 0xFE,                 /* $A006 BRA *                    */
        };
        static const uint8_t after[] = {
            0x8E, 0x02, 0x58,           /* $A100 LDX #600                 */
            0x30, 0x1F, 0x26, 0xFC,     /* $A103 LEAX -1,X, BNE $A103     */
            0x86, 0xFF, 0xB7, 0x05, 0x20, /* $A107 LDA #$FF, STA $0520    */
            0x20, 0xFE,                 /* $A10C BRA *                    */
        };
        memcpy(rom, run64, sizeof run64);
        memcpy(rom + 0x100, after, sizeof after);
        rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;
        machine();
        for (int i = 0; i < 3; i++) coco_run_field(&m);
        CHECK((m.sam.reg & SAM_M_MASK) == 2u << 13, "64K before the reset");
        coco_run_blanking(&m);
        uint64_t line50 = m.field_start + (uint64_t)(COCO_ACTIVE_LINE + 50u) * COCO_LINE_TICKS;
        (void)coco_run(&m, (uint32_t)(line50 - coco_ticks(&m)));
        /* The reset's vector to $A100, as the ROM the machine holds. */
        m.rom[0x3FFE] = 0xA1; m.rom[0x3FFF] = 0x00;
        coco_reset(&m);
        coco_run_field(&m);
        int32_t i520 = coco_ram_index(&m, 0x0520u);
        CHECK((m.sam.reg & SAM_M_MASK) == 0 && i520 >= 0 && m.ram[i520] == 0xFFu,
              "4K after the reset, $0520 written: M %u, ram %02X", (m.sam.reg & SAM_M_MASK) >> 13,
              i520 >= 0 ? m.ram[i520] : 0u);
        coco_video_snapshot(&m, &f);
        unsigned shown = 0;
        for (unsigned y = 108; y < 120u; y++) shown += f.lines[y * 32u] == 0xFFu;
        CHECK(shown == 0, "reset mid-field: the byte written after the beam passed is on %u of "
              "row 9's 12 lines", shown);
        m.vid_whole = false;
        m.sam.reg |= (0x0400u >> 9) << 3;   /* the walk from F, which the reset cleared */
        coco_video_snapshot(&m, &f);
        unsigned walked = 0;
        for (unsigned y = 108; y < 120u; y++) walked += f.lines[y * 32u] == 0xFFu;
        CHECK(walked == 12u, "reset mid-field, the control: RAM walked has it on %u of 12 lines",
              walked);
    }

    TEST_DONE();
}
