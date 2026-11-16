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
#include "test_util.h"

static coco_t m, m2;
static coco_frame_t f;
static uint8_t rom[0x2000];

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
}

/* The address each shown byte was fetched from, from two snapshots: one
 * of RAM holding each address's low byte, one its high. */
static uint16_t addr[COCO_PIC_H][COCO_VDG_LINE_BYTES];
static void addresses(void) {
    for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = (uint8_t)a;
    coco_video_snapshot(&m, &f);
    for (unsigned i = 0; i < COCO_SNAP_VRAM_MAX; i++) addr[i / 32u][i % 32u] = f.lines[i];
    for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = (uint8_t)(a >> 8);
    coco_video_snapshot(&m, &f);
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
        for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
            at_rest(modes[i].mode, modes[i].v, 0x0E00u);
            addresses();
            CHECK(f.mode == modes[i].mode && f.sam_v == modes[i].v && f.base == 0x0E00u,
                  "%s: the frame's modes", modes[i].name);
            if (check_layout(modes[i].name, 0x0E00u, modes[i].per, modes[i].stride,
                             modes[i].shown)) return 1;
        }

        /* The counter wraps past $FFFF: RG6 from $FE00 reaches $0000 at
         * line 16. */
        at_rest(0xF8, 6, 0xFE00u);
        addresses();
        CHECK(addr[15][31] == 0xFFFFu && addr[16][0] == 0x0000u,
              "RG6 from $FE00: line 16 from $%04X", addr[16][0]);

        /* A 16-byte mode's other 16 bytes are zero, not left over. */
        at_rest(0x90, 1, 0x0400u);
        for (unsigned a = 0; a < COCO_ADDR_SPACE; a++) m.ram[a] = 0xA5u;
        coco_video_snapshot(&m, &f);
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
            /* The control: a copy run only to where FS rises. */
            coco_copy(&m2, &m);
            uint64_t rise = m2.field_start + (uint64_t)COCO_FIELD_LINES * COCO_LINE_TICKS;
            (void)coco_run(&m2, (uint32_t)(rise - coco_ticks(&m2)));
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

    TEST_DONE();
}
