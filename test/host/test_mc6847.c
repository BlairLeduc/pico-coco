/* test_mc6847.c — the VDG's modes, its tables and a line's pixels
 * (design.md §9, §15.1), adapted from pico-atom's.
 *
 * Each mode's decode, asserted by rendering a line and looking at the
 * pixels: the CoCo's wiring (§3.4), SG4 and SG6 by GM0, INV on bit 6, the
 * external-ROM case, the colour sets, artefacts, the mono palette, the
 * character ROM's layout against its data, the dirty-band span, and the
 * MC6847T1's lower case, inverse and border (mc6847.h), and the
 * datasheet palette derived again from the chip's voltages (§9.5).
 * test_vdg_oracle holds the same decode to XRoar's for random bytes;
 * this test says what each part of it is, so a failure there can be
 * read here.
 */

#include <math.h>
#include <string.h>

#include "config.h"
#include "mc6847.h"
#include "mc6847_font.h"
#include "test_util.h"

static mc6847_t v;
static uint8_t  line[COCO_VDG_LINE_BYTES];
static uint16_t px[COCO_PIC_W];
static uint8_t  lines[COCO_SNAP_VRAM_MAX], shadow[COCO_SNAP_VRAM_MAX];

static uint8_t gfx(unsigned gm, bool css) {
    return (uint8_t)(VDG_AG | gm << VDG_GM_SHIFT | (css ? VDG_CSS : 0u));
}

#define P(c) mc6847_palette[c]

/* Does cell 0 of row `row` show glyph g of the T1's ROM, inverted or not,
 * in ink on its background? */
static bool t1_shows(unsigned g, unsigned row, bool inv, vdg_colour_t ink, vdg_colour_t bg) {
    uint8_t bits = font_6847t1[g * MC6847_FONT_ROWS + row];
    if (inv) bits = (uint8_t)~bits;
    for (unsigned x = 0; x < 8u; x++)
        if (px[x] != P(((bits >> (7u - x)) & 1u) ? ink : bg)) return false;
    return true;
}

/* A colour from the MC6847 datasheet's typical Y, φB and φA (volts,
 * the chroma about 1.5 V), as §9.5 derives it: the MC1372's gain of 0.6
 * with φA `lag` degrees from φB, demodulated on axes 90° apart, black at
 * 0.72 V and white at 0.42 V, Y'PbPr to R'G'B' and clipped. */
static uint16_t from_volts(double y, double phb, double pha, double lag) {
    double l = (0.72 - y) / (0.72 - 0.42);
    double pb = 0.6 * (phb - 1.5), pr = 0.6 * (pha - 1.5);
    double th = lag * M_PI / 180.0;
    double u = pb + pr * cos(th), w = pr * sin(th);
    double c[3] = { l + 1.402 * w, l - 0.344136 * u - 0.714136 * w, l + 1.772 * u };
    unsigned b[3];
    for (unsigned i = 0; i < 3u; i++) {
        double x = c[i] < 0.0 ? 0.0 : c[i] > 1.0 ? 1.0 : c[i];
        b[i] = (unsigned)lround(x * 255.0);
    }
    return (uint16_t)(((b[0] & 0xF8u) << 8) | ((b[1] & 0xFCu) << 3) | (b[2] >> 3));
}

/* The datasheet's figure 10, typical values, by colour. */
static const struct { vdg_colour_t c; double y, phb, pha; } k_volts[] = {
    { VDG_GREEN,   0.54, 1.0, 1.0 }, { VDG_YELLOW,  0.42, 1.0, 1.5 },
    { VDG_BLUE,    0.65, 2.0, 1.5 }, { VDG_RED,     0.65, 1.5, 2.0 },
    { VDG_BUFF,    0.42, 1.5, 1.5 }, { VDG_CYAN,    0.54, 1.5, 1.0 },
    { VDG_MAGENTA, 0.54, 2.0, 2.0 }, { VDG_ORANGE,  0.54, 1.0, 2.0 },
    { VDG_BLACK,   0.72, 1.5, 1.5 }, { VDG_BRIGHT_ORANGE, 0.54, 1.0, 2.0 },
};

int main(void) {
    /* ---- the mode table --------------------------------------------- */
    {
        const char *want[8] = { "CG1", "RG1", "CG2", "RG2", "CG3", "RG3", "CG6", "RG6" };
        const unsigned sam_v[8] = { 1, 1, 2, 3, 4, 5, 6, 6 };
        for (unsigned gm = 0; gm < 8u; gm++) {
            const mc6847_mode_info_t *info = mc6847_mode_info(gfx(gm, false));
            CHECK(strcmp(info->name, want[gm]) == 0, "GM %u should be %s, is %s", gm, want[gm],
                  info->name);
            CHECK(info->bytes_per_line * info->px_per_byte == COCO_PIC_W,
                  "%s: %u bytes of %u pixels do not span 256", info->name, info->bytes_per_line,
                  info->px_per_byte);
            CHECK(info->px_per_byte <= COCO_LUT_PX_MAX, "%s: a wider entry than config.h allows",
                  info->name);
            CHECK(info->sam_v == sam_v[gm], "%s: matched by SAM mode %u, not %u", info->name,
                  info->sam_v, sam_v[gm]);
            CHECK(info->vram_bytes <= COCO_SNAP_VRAM_MAX, "%s: %u bytes", info->name,
                  info->vram_bytes);
            CHECK(mc6847_mode_info((uint8_t)(gm << VDG_GM_SHIFT))->kind == VDG_ALPHA,
                  "A/G low is alpha whatever GM %u says", gm);
        }
        CHECK(mc6847_mode_info(0)->bytes_per_line == 32u, "alpha fetches 32 bytes a line");
        /* The packed mode is port B's top five bits as they stand. */
        CHECK(mc6847_mode_from_pb(0xFFu) == 0xF8u, "port B's low three bits are not the VDG's");
        CHECK(mc6847_mode_from_pb(0x08u) == VDG_CSS, "PB3 is CSS");
        CHECK(mc6847_mode_from_pb(0x10u) == VDG_GM0, "PB4 is GM0");
        CHECK(mc6847_mode_from_pb(0x80u) == VDG_AG, "PB7 is A/G");
    }

    mc6847_init(&v);
    mc6847_set_font(&v, font_6847);

    /* ---- RG6: one bit a pixel, bit 7 leftmost ----------------------- */
    {
        memset(line, 0, sizeof line);
        line[0] = 0x81u;
        mc6847_set_mode(&v, gfx(7, true));
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_BUFF) && px[7] == P(VDG_BUFF), "RG6 bits 7 and 0 at pixels 0 and 7");
        for (unsigned x = 1; x < 7u; x++) CHECK(px[x] == P(VDG_BLACK), "RG6 pixel %u is black", x);
        /* With CSS low the background is dark green, drawn black unless
         * the dark background is chosen. */
        mc6847_set_mode(&v, gfx(7, false));
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_GREEN) && px[1] == P(VDG_BLACK), "RG6, CSS low: green on black");
        mc6847_set_dark_bg(&v, true);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[1] == P(VDG_DARK_GREEN), "RG6, CSS low, dark background: dark green");
        mc6847_set_dark_bg(&v, false);
    }

    /* ---- the 16-byte modes: a byte is 16 pixels --------------------- */
    {
        memset(line, 0, sizeof line);
        line[0] = 0x1Bu;   /* 00 01 10 11 */
        line[15] = 0xFFu;
        mc6847_set_mode(&v, gfx(0, false));   /* CG1 */
        mc6847_render_line(&v, line, 0, px);
        const vdg_colour_t want[4] = { VDG_GREEN, VDG_YELLOW, VDG_BLUE, VDG_RED };
        for (unsigned p = 0; p < 4u; p++)
            for (unsigned s = 0; s < 4u; s++)
                CHECK(px[p * 4u + s] == P(want[p]), "CG1 pixel %u, repeat %u", p, s);
        CHECK(px[255] == P(VDG_RED), "CG1's sixteenth byte ends the line");
        mc6847_set_mode(&v, gfx(0, true));
        mc6847_render_line(&v, line, 0, px);
        const vdg_colour_t want_css[4] = { VDG_BUFF, VDG_CYAN, VDG_MAGENTA, VDG_ORANGE };
        for (unsigned p = 0; p < 4u; p++) CHECK(px[p * 4u] == P(want_css[p]), "CG1, CSS: %u", p);

        line[0] = 0x80u;
        mc6847_set_mode(&v, gfx(5, true));    /* RG3 */
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_BUFF) && px[1] == P(VDG_BUFF) && px[2] == P(VDG_BLACK),
              "RG3: a bit is two pixels");
    }

    /* ---- semigraphics 4 and 6 (§3.4) -------------------------------- */
    {
        mc6847_set_mode(&v, 0);   /* alpha, GM0 low: SG4 */
        /* Bit 7 makes a semigraphics cell; bits 6-4 its colour, here
         * 5, cyan; bits 3-0 the elements, top-left 3 only. */
        line[0] = (uint8_t)(VDG_BYTE_AS | 5u << 4 | 0x08u);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_CYAN) && px[3] == P(VDG_CYAN), "SG4 top-left lit, 4 pixels wide");
        CHECK(px[4] == P(VDG_BLACK), "SG4 top-right dark");
        mc6847_render_line(&v, line, 5, px);
        CHECK(px[0] == P(VDG_CYAN), "SG4's top half is six lines");
        mc6847_render_line(&v, line, 6, px);
        CHECK(px[0] == P(VDG_BLACK), "SG4's bottom half starts at line 6");
        line[0] = (uint8_t)(VDG_BYTE_AS | 5u << 4 | 0x01u);
        mc6847_render_line(&v, line, 11, px);
        CHECK(px[4] == P(VDG_CYAN) && px[0] == P(VDG_BLACK), "SG4 bit 0 is bottom-right");

        mc6847_set_mode(&v, VDG_GM0);   /* alpha, GM0 high: INT/EXT, SG6 */
        line[0] = (uint8_t)(VDG_BYTE_AS | 0x20u);   /* C1:C0 = 10, top-left */
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_BLUE), "SG6 with bit 7 set: blue");
        mc6847_render_line(&v, line, 3, px);
        CHECK(px[0] == P(VDG_BLUE), "SG6's rows are four lines");
        mc6847_render_line(&v, line, 4, px);
        CHECK(px[0] == P(VDG_BLACK), "SG6's middle row starts at line 4");
        line[0] = (uint8_t)(VDG_BYTE_AS | VDG_BYTE_INV | 0x01u);   /* C1:C0 = 11 */
        mc6847_render_line(&v, line, 8, px);
        CHECK(px[4] == P(VDG_RED), "SG6 bit 0 bottom-right, red");
        mc6847_set_mode(&v, VDG_GM0 | VDG_CSS);
        mc6847_render_line(&v, line, 8, px);
        CHECK(px[4] == P(VDG_ORANGE), "SG6, CSS: orange");
    }

    /* ---- text: the glyph, INV, CSS, the external case --------------- */
    {
        mc6847_set_mode(&v, 0);
        /* 'A' is glyph 1. Bit 6 clear: green ink on its dark ground. */
        line[0] = 0x01u;
        unsigned row = 4;   /* a row of the glyph with ink in it */
        uint8_t g = font_6847[1u * MC6847_FONT_ROWS + row];
        mc6847_render_line(&v, line, row, px);
        for (unsigned x = 0; x < 8u; x++)
            CHECK(px[x] == P((g >> (7u - x)) & 1u ? VDG_GREEN : VDG_BLACK), "'A' pixel %u", x);
        /* Bit 6 inverts: dark on green, which is how BASIC prints. */
        line[0] = (uint8_t)(0x01u | VDG_BYTE_INV);
        mc6847_render_line(&v, line, row, px);
        for (unsigned x = 0; x < 8u; x++)
            CHECK(px[x] == P((g >> (7u - x)) & 1u ? VDG_BLACK : VDG_GREEN), "inverse 'A' %u", x);
        CHECK(px[0] == P(VDG_GREEN), "INV fills the spacing columns");
        mc6847_set_mode(&v, VDG_CSS);
        mc6847_render_line(&v, line, row, px);
        CHECK(px[0] == P(VDG_BRIGHT_ORANGE), "CSS: bright orange");
        /* INT/EXT high: no external ROM, so the byte is the row. */
        mc6847_set_mode(&v, VDG_GM0);
        line[0] = 0x05u;
        mc6847_render_line(&v, line, row, px);
        CHECK(px[5] == P(VDG_GREEN) && px[7] == P(VDG_GREEN) && px[6] == P(VDG_BLACK),
              "INT/EXT high: the byte's own bits");
    }

    /* ---- the character ROM against its layout (EL §5.5) -------------- */
    {
        unsigned used = 0;
        for (unsigned i = 0; i < MC6847_FONT_BYTES; i++) used |= font_6847[i];
        CHECK((used & 0xC1u) == 0, "the ROM uses bits outside 5..1: union %02X", used);
        for (unsigned g = 0; g < MC6847_FONT_GLYPHS; g++) {
            for (unsigned r = 0; r < 3u; r++)
                CHECK(font_6847[g * MC6847_FONT_ROWS + r] == 0, "glyph %u row %u is spacing", g, r);
            for (unsigned r = 10u; r < MC6847_FONT_ROWS; r++)
                CHECK(font_6847[g * MC6847_FONT_ROWS + r] == 0, "glyph %u row %u is spacing", g, r);
        }
        unsigned ink_space = 0, ink_a = 0;
        for (unsigned r = 0; r < MC6847_FONT_ROWS; r++) {
            ink_space |= font_6847[MC6847_GLYPH_SPACE * MC6847_FONT_ROWS + r];
            ink_a |= font_6847[1u * MC6847_FONT_ROWS + r];
        }
        CHECK(ink_space == 0 && ink_a != 0, "glyph 32 is the space and glyph 1 'A'");
        CHECK(mc6847_glyph_ascii(0) == '@' && mc6847_glyph_ascii(26) == 'Z' &&
              mc6847_glyph_ascii(32) == ' ' && mc6847_glyph_ascii(63) == '?',
              "the MC6847's glyph order");
        /* Zeroed RAM is a screen of '@' (EL §5.5), not blanks. */
        memset(line, 0, sizeof line);
        mc6847_set_mode(&v, 0);
        unsigned ink = 0;
        for (unsigned y = 0; y < 12u; y++) {
            mc6847_render_line(&v, line, y, px);
            for (unsigned x = 0; x < COCO_PIC_W; x++) ink += px[x] != P(VDG_BLACK);
        }
        CHECK(ink > 0, "zeroed RAM should show '@'");
    }

    /* ---- artefacts (§9.5) ------------------------------------------- */
    {
        memset(line, 0, sizeof line);
        line[0] = 0x1Bu;   /* pairs 00 01 10 11 */
        mc6847_set_artefact(&v, VDG_ARTEFACT_KBRW);
        mc6847_set_mode(&v, gfx(7, true));
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == P(VDG_BLACK) && px[2] == P(VDG_ARTEFACT_BLUE) &&
              px[4] == P(VDG_ARTEFACT_RED) && px[6] == P(VDG_BUFF) && px[7] == P(VDG_BUFF),
              "KBRW: black, blue, red, white");
        mc6847_set_artefact(&v, VDG_ARTEFACT_KRBW);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[2] == P(VDG_ARTEFACT_RED) && px[4] == P(VDG_ARTEFACT_BLUE), "KRBW swaps them");
        /* Only RG6 with CSS high shows them. */
        mc6847_set_mode(&v, gfx(7, false));
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[3] == P(VDG_GREEN), "RG6 with CSS low has none");
        mc6847_set_mode(&v, gfx(7, true));
        mc6847_set_mono(&v, true);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[3] == mc6847_palette_mono[VDG_BUFF], "a monochrome set has none");
        mc6847_set_mono(&v, false);
        mc6847_set_artefact(&v, VDG_ARTEFACT_OFF);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[3] == P(VDG_BUFF) && px[2] == P(VDG_BLACK), "off: the pixels as they are");
    }

    /* ---- mono: the datasheet's luminance ---------------------------- */
    {
        memset(line, 0, sizeof line);
        line[0] = 0x1Bu;
        mc6847_set_mode(&v, gfx(6, false));   /* CG6: green yellow blue red */
        mc6847_set_mono(&v, true);
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[4] == px[6], "blue and red are one level");
        CHECK(px[2] == 0xFFFFu, "yellow, at 0.42 V, is white");
        CHECK(px[0] != px[4], "green and blue differ");
        mc6847_set_mono(&v, false);
    }

    /* ---- every mode fills the whole line ---------------------------- */
    {
        for (unsigned i = 0; i < 18u; i++) {
            uint8_t mode = i < 2u ? (uint8_t)(i ? VDG_GM0 : 0u) : gfx((i - 2u) % 8u, i >= 10u);
            mc6847_set_mode(&v, mode);
            for (unsigned b = 0; b < sizeof line; b++) line[b] = (uint8_t)(b * 37u + i);
            px[COCO_PIC_W - 1u] = 0xDEADu;
            mc6847_render_line(&v, line, 7, px);
            CHECK(px[COCO_PIC_W - 1u] != 0xDEADu, "%s did not fill the line",
                  mc6847_mode_info(mode)->name);
        }
    }

    /* ---- the dirty band's span -------------------------------------- */
    {
        memset(lines, 0, sizeof lines);
        memset(shadow, 0, sizeof shadow);
        uint16_t x0, x1;
        uint8_t rg6 = gfx(7, true), cg1 = gfx(0, false);
        CHECK(!mc6847_band_span(rg6, lines, shadow, 0, &x0, &x1), "nothing changed");
        lines[9 * COCO_VDG_LINE_BYTES + 5] = 1;   /* band 1, byte 5 */
        CHECK(!mc6847_band_span(rg6, lines, shadow, 0, &x0, &x1), "band 0 untouched");
        CHECK(mc6847_band_span(rg6, lines, shadow, 1, &x0, &x1) && x0 == 40 && x1 == 47,
              "RG6 byte 5 is pixels 40-47: %u-%u", x0, x1);
        CHECK(mc6847_band_span(cg1, lines, shadow, 1, &x0, &x1) && x0 == 80 && x1 == 95,
              "CG1 byte 5 is pixels 80-95: %u-%u", x0, x1);
        lines[9 * COCO_VDG_LINE_BYTES + 20] = 1;   /* past a 16-byte mode's bytes */
        CHECK(mc6847_band_span(cg1, lines, shadow, 1, &x0, &x1) && x1 == 95,
              "a 16-byte mode ignores bytes 16-31: to %u", x1);
    }

    /* ---- the MC6847T1 (mc6847.h, §9.2) ------------------------------- */
    {
        mc6847_init(&v);
        mc6847_set_font(&v, font_6847);
        memset(line, 0, sizeof line);
        /* The control first: the MC6847 with GM0 high draws a text byte
         * as itself, INT/EXT with no external ROM to answer. */
        line[0] = 0x01u;
        mc6847_set_mode(&v, VDG_GM0);
        mc6847_render_line(&v, line, 4, px);
        CHECK(!t1_shows(0x01u, 4, true, VDG_GREEN, VDG_BLACK),
              "the MC6847 has no lower case: $01 with GM0 is not 'a'");

        mc6847_set_t1(&v, font_6847t1);
        CHECK(mc6847_is_t1(&v), "a T1 once its ROM is given");
        /* Its ROM: 'a' at $01 and 'A' at $41, upright; '!' at $21 stored
         * inverted and at $61 upright. */
        CHECK(font_6847t1[0x01u * 12u + 3u] == 0x1Cu && font_6847t1[0x41u * 12u + 5u] == 0x3Eu,
              "the T1's ROM: 'a' at $01, 'A' at $41");
        CHECK(font_6847t1[0x21u * 12u] == 0xFFu && font_6847t1[0x61u * 12u] == 0x00u,
              "the T1's ROM: $20-$3F stored inverted, $60-$7F upright");

        /* GM0 low: $40-$7F by six bits, D6 as INV, as the MC6847. */
        mc6847_set_mode(&v, 0);
        for (unsigned row = 0; row < 12u; row++) {
            line[0] = 0x41u;
            mc6847_render_line(&v, line, row, px);
            CHECK(t1_shows(0x41u, row, true, VDG_GREEN, VDG_BLACK), "GM0 low: $41, 'A', row %u",
                  row);
            line[0] = 0x01u;
            mc6847_render_line(&v, line, row, px);
            CHECK(t1_shows(0x41u, row, false, VDG_GREEN, VDG_BLACK),
                  "GM0 low: $01 is 'A' inverse, row %u", row);
        }
        /* GM0 high: seven bits pick the glyph, every cell inverted, so
         * lower and upper case are both dark on green. */
        mc6847_set_mode(&v, VDG_GM0);
        for (unsigned row = 0; row < 12u; row++) {
            line[0] = 0x01u;
            mc6847_render_line(&v, line, row, px);
            CHECK(t1_shows(0x01u, row, true, VDG_GREEN, VDG_BLACK), "GM0 high: $01, 'a', row %u",
                  row);
            line[0] = 0x41u;
            mc6847_render_line(&v, line, row, px);
            CHECK(t1_shows(0x41u, row, true, VDG_GREEN, VDG_BLACK), "GM0 high: $41, 'A', row %u",
                  row);
        }
        /* GM1 inverts every cell; CSS's ink is orange. */
        line[0] = 0x01u;
        mc6847_set_mode(&v, VDG_GM0 | 0x20u);
        mc6847_render_line(&v, line, 4, px);
        CHECK(t1_shows(0x01u, 4, false, VDG_GREEN, VDG_BLACK), "GM1: 'a' green on black");
        mc6847_set_mode(&v, VDG_GM0 | VDG_CSS);
        mc6847_render_line(&v, line, 4, px);
        CHECK(t1_shows(0x01u, 4, true, VDG_ORANGE, VDG_BLACK), "CSS: 'a' in orange");
        /* No SG6: GM0 high leaves a semigraphics cell SG4, colour by
         * bits 6-4. */
        line[0] = (uint8_t)(VDG_BYTE_AS | 5u << 4 | 0x08u);
        mc6847_set_mode(&v, VDG_GM0);
        mc6847_render_line(&v, line, 5, px);
        CHECK(px[0] == P(VDG_CYAN) && px[4] == P(VDG_BLACK), "GM0 high: SG4 still, cyan");

        /* The border: the ink's with GM2 high and GM1 low, in alpha. */
        CHECK(mc6847_border(0x40u, true) == VDG_GREEN, "T1, GM2: a green border");
        CHECK(mc6847_border(0x48u, true) == VDG_ORANGE, "T1, GM2, CSS: an orange border");
        CHECK(mc6847_border(0x60u, true) == VDG_BLACK, "T1, GM2 and GM1: black");
        CHECK(mc6847_border(0x10u, true) == VDG_BLACK, "T1, GM0 alone: black");
        CHECK(mc6847_border(0x40u, false) == VDG_BLACK, "MC6847, GM2: black");
        for (unsigned m = 0x80u; m < 0x100u; m += 8u)
            CHECK(mc6847_border((uint8_t)m, true) == mc6847_border((uint8_t)m, false),
                  "graphics mode %02X: the T1's border is the MC6847's", m);
        mc6847_set_t1(&v, NULL);
        CHECK(!mc6847_is_t1(&v), "the MC6847 again");
    }

    /* ---- the datasheet palette (§9.5) --------------------------------- */
    {
        const uint16_t *d = mc6847_palette_datasheet;
        for (unsigned i = 0; i < sizeof k_volts / sizeof k_volts[0]; i++) {
            vdg_colour_t c = k_volts[i].c;
            uint16_t want = from_volts(k_volts[i].y, k_volts[i].phb, k_volts[i].pha, 100.0);
            CHECK(d[c] == want, "colour %u from the datasheet's voltages: %04X, not %04X",
                  (unsigned)c, d[c], want);
        }
        /* The control: decoded as if φA were 90° from φB, cyan and red
         * are other colours, so the MC1372's 100° is what the table says. */
        CHECK(from_volts(0.54, 1.5, 1.0, 90.0) != d[VDG_CYAN] &&
              from_volts(0.65, 1.5, 2.0, 90.0) != d[VDG_RED], "a 90° decode differs");
        /* The four of white medium have one luminance, as figure 10 says;
         * the primaries' cyan is brighter than their green. */
        CHECK(mc6847_palette[VDG_CYAN] != d[VDG_CYAN], "the primaries' cyan is not the chip's");
        /* Drawn: CG2 with CSS high, buff cyan magenta orange. */
        mc6847_init(&v);
        mc6847_set_font(&v, font_6847);
        mc6847_set_palette(&v, d);
        memset(line, 0, sizeof line);
        line[0] = 0x1Bu;                      /* 00 01 10 11 */
        mc6847_set_mode(&v, gfx(2, true));
        mc6847_render_line(&v, line, 0, px);
        CHECK(px[0] == d[VDG_BUFF] && px[2] == d[VDG_CYAN] && px[4] == d[VDG_MAGENTA] &&
              px[6] == d[VDG_ORANGE], "CG2, CSS: drawn from the datasheet palette");
        mc6847_set_palette(&v, mc6847_palette);
    }

    TEST_DONE();
}
