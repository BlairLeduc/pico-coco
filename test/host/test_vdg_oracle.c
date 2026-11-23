/* test_vdg_oracle.c — the picture held to XRoar's, pixel for pixel
 * (design.md §9, §15.2).
 *
 * For every SAM video mode, against every VDG mode in both colour sets,
 * random RAM and a random display offset (some placed to wrap past
 * $FFFF): the machine's snapshot through the SAM's counter
 * (coco_video_snapshot), drawn by mc6847_render_line with a palette whose
 * entries are their own indices, against XRoar 1.13's MC6847 fed by its
 * MC6883 (xroar_vdg.c), with the page bit P set at random, which the
 * VDG's address does not take. Each pixel's colour must equal both of XRoar's
 * half-pixels. The matched pairs are the datasheet's nine modes; the
 * mismatched ones include semigraphics 8, 12 and 24.
 *
 * Then the artefact colours (§9.5): XRoar's simple renderer's rule
 * applied to XRoar's VDG output (a pair of pixels each black or white
 * becomes black, blue, red or white, by phase), against this renderer
 * with artefacts on, in both phases. And XRoar's renderers themselves
 * (xroar_render.c), its 2-bit and its 5-bit, fed XRoar's VDG output a
 * line at a time, against this renderer's simple and full artefacts,
 * in every mode, both colour sets and both phases, with the dark
 * backgrounds drawn as such and as black.
 *
 * With "t1", the MC6847T1 (mc6847.h) against XRoar's MC6847T1, its
 * ROM ours as XRoar's: in every alphanumeric mode, GM2:GM0 all eight,
 * which the T1 reads and the MC6847 does not, and the graphics modes,
 * which it draws as the MC6847 does.
 *
 * Skips without the XRoar checkout; a skip is not a pass.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coco.h"
#include "mc6847.h"
#include "mc6847_font.h"
#include "test_util.h"
#include "xroar_render.h"
#include "xroar_vdg.h"

static coco_t m;
static coco_frame_t frame;
static mc6847_t vdg;
static uint8_t ram[COCO_ADDR_SPACE];
static uint8_t want[XV_LINES][XV_HALF_PIXELS];
static uint16_t got[COCO_PIC_W];
static uint16_t index_pal[VDG_COLOUR_COUNT];

static uint32_t rng_state = 0x6809u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* XRoar's simple renderer: black is 0, white 3; anything else is not a
 * candidate for an artefact. Its own test is the palette's luma and
 * chroma; of the VDG's colours only black and buff pass it. */
static int bw(uint8_t c) {
    return c == VDG_BLACK ? 0 : c == VDG_BUFF ? 1 : -1;
}

static unsigned compared_fields, diffs_reported;

/* One field of both. `artefact` off compares indices as they stand. */
static int compare(uint16_t sam_reg, uint8_t pb, vdg_artefact_t artefact, const char *what) {
    xv_field(ram, sam_reg, pb, want);

    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    m.sam.reg = (uint16_t)((sam_reg & ~SAM_M_MASK) | (2u << 13));   /* 64K mode */
    m.pia1.b.ddr = 0xFFu;
    m.pia1.b.out = pb;
    memcpy(m.ram, ram, sizeof ram);
    coco_video_snapshot(&m, &frame);

    mc6847_set_artefact(&vdg, artefact);
    mc6847_set_mode(&vdg, frame.mode);
    CHECK(frame.mode == (pb & 0xF8u), "%s: the snapshot's mode %02X, port B %02X", what,
          frame.mode, pb);

    unsigned bad = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        mc6847_render_line(&vdg, &frame.lines[y * COCO_VDG_LINE_BYTES], y, got);
        for (unsigned x = 0; x < COCO_PIC_W; x++) {
            uint8_t a = want[y][2 * x], b = want[y][2 * x + 1];
            unsigned w;
            if (artefact == VDG_ARTEFACT_OFF || a != b) {
                w = a;
                if (a != b) w = 0xFFu;   /* a pixel split by XRoar: never ours */
            } else {
                /* The pair this pixel belongs to, as XRoar groups four
                 * half-pixels from the active line's start. */
                unsigned x0 = x & ~1u;
                uint8_t c0 = want[y][2 * x0], c1 = want[y][2 * x0 + 1];
                uint8_t c2 = want[y][2 * x0 + 2], c3 = want[y][2 * x0 + 3];
                w = a;
                if (c0 == c1 && c2 == c3 && bw(c0) >= 0 && bw(c2) >= 0) {
                    static const uint8_t kbrw[4] = { VDG_BLACK, VDG_ARTEFACT_BLUE,
                                                     VDG_ARTEFACT_RED, VDG_BUFF };
                    static const uint8_t krbw[4] = { VDG_BLACK, VDG_ARTEFACT_RED,
                                                     VDG_ARTEFACT_BLUE, VDG_BUFF };
                    unsigned k = (unsigned)(bw(c0) << 1 | bw(c2));
                    w = artefact == VDG_ARTEFACT_KBRW ? kbrw[k] : krbw[k];
                }
            }
            if (got[x] != w) {
                if (!bad && diffs_reported < 12) {
                    fprintf(stderr, "    %s: first difference at (%u,%u): ours %u, XRoar's %u %u\n",
                            what, x, y, got[x], a, b);
                    fprintf(stderr, "    line bytes:");
                    for (unsigned i = 0; i < 32u; i++)
                        fprintf(stderr, " %02X", frame.lines[y * COCO_VDG_LINE_BYTES + i]);
                    fprintf(stderr, "\n");
                    diffs_reported++;
                }
                bad++;
            }
        }
    }
    CHECK(bad == 0, "%s: %u pixel(s) differ from XRoar's", what, bad);
    compared_fields++;
    return 0;
}

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* One field through XRoar's VDG and its renderer `ccr`, against ours with
 * the matching artefacts. A colour drawn from XRoar's palette must be the
 * same index drawn by ours; an artefact, XRoar's table's colour, which
 * the simple renderer draws as the palette's black or buff where XRoar's
 * table gives black or white. Ours draws through `pal`, whose VDG colours
 * no table colour equals. */
static int compare_ccr(uint16_t sam_reg, uint8_t pb, xr_ccr_t ccr, int krbw, bool dark,
                       const uint16_t *pal, const char *what) {
    xv_field(ram, sam_reg, pb, want);

    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    m.sam.reg = (uint16_t)((sam_reg & ~SAM_M_MASK) | (2u << 13));   /* 64K mode */
    m.pia1.b.ddr = 0xFFu;
    m.pia1.b.out = pb;
    memcpy(m.ram, ram, sizeof ram);
    coco_video_snapshot(&m, &frame);

    static mc6847_t v;
    static bool init;
    if (!init) {
        mc6847_init(&v);
        mc6847_set_font(&v, font_6847);
        init = true;
    }
    mc6847_set_t1(&v, mc6847_is_t1(&vdg) ? font_6847t1 : NULL);
    mc6847_set_palette(&v, pal);
    mc6847_set_dark_bg(&v, dark);
    mc6847_set_artefact(&v, ccr == XR_CCR_5BIT
                                ? (krbw ? VDG_ARTEFACT_FULL_KRBW : VDG_ARTEFACT_FULL_KBRW)
                                : (krbw ? VDG_ARTEFACT_KRBW : VDG_ARTEFACT_KBRW));
    mc6847_set_mode(&v, frame.mode);
    uint8_t border = (uint8_t)mc6847_border(frame.mode, mc6847_is_t1(&v));

    static xr_pixel_t xr[256];
    unsigned bad = 0, split = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        mc6847_render_line(&v, &frame.lines[y * COCO_VDG_LINE_BYTES], y, got);
        mc6847_artefact_line(&v, got, v.col[border], v.col[border]);
        xr_render_line(ccr, krbw, want[y], border, xr);
        for (unsigned x = 0; x < COCO_PIC_W; x++) {
            /* A pixel XRoar split, and those whose colours it reaches. */
            bool skip = false;
            for (int d = -2; d <= 2; d++) {
                int i = (int)x + d;
                if (i >= 0 && i < (int)COCO_PIC_W && want[y][2 * i] != want[y][2 * i + 1])
                    skip = true;
            }
            if (skip || xr[x].from_palette == 2) { split++; continue; }
            uint16_t w;
            if (xr[x].from_palette) {
                w = v.col[xr[x].index];
            } else if (ccr == XR_CCR_SIMPLE && xr[x].r == 0 && xr[x].g == 0 && xr[x].b == 0) {
                w = v.col[VDG_BLACK];
            } else if (ccr == XR_CCR_SIMPLE && xr[x].r == 0xFF && xr[x].g == 0xFF &&
                       xr[x].b == 0xFF) {
                w = v.col[VDG_BUFF];
            } else {
                w = RGB565(xr[x].r, xr[x].g, xr[x].b);
            }
            if (got[x] != w) {
                if (!bad && diffs_reported < 12) {
                    fprintf(stderr, "    %s: first difference at (%u,%u): ours %04X, XRoar's %04X"
                            " (%s %u, %02X%02X%02X)\n", what, x, y, got[x], w,
                            xr[x].from_palette ? "index" : "table", xr[x].index, xr[x].r,
                            xr[x].g, xr[x].b);
                    diffs_reported++;
                }
                bad++;
            }
        }
    }
    CHECK(bad == 0, "%s: %u pixel(s) differ from XRoar's renderer", what, bad);
    CHECK(split == 0, "%s: %u pixel(s) split or beside one, not compared", what, split);
    compared_fields++;
    return 0;
}

int main(int argc, char **argv) {
    bool t1 = argc > 1 && strcmp(argv[1], "t1") == 0;
    for (unsigned i = 0; i < VDG_COLOUR_COUNT; i++) index_pal[i] = (uint16_t)i;
    mc6847_init(&vdg);
    mc6847_set_font(&vdg, font_6847);
    if (t1) {
        mc6847_set_t1(&vdg, font_6847t1);
        xv_use_t1();
    }
    mc6847_set_palette(&vdg, index_pal);
    mc6847_set_dark_bg(&vdg, true);   /* the dark colours as their own indices */

    /* The VDG's modes: alpha with INT/EXT (GM0) low and high, the other GM
     * bits set at random, which alpha ignores; and the eight graphics
     * modes. */
    uint8_t vdg_modes[16];
    unsigned n = 0;
    for (unsigned gm = 0; gm < (t1 ? 8u : 2u); gm++) vdg_modes[n++] = (uint8_t)(gm << 4);
    for (unsigned gm = 0; gm < 8u; gm++) vdg_modes[n++] = (uint8_t)(0x80u | gm << 4);

    for (unsigned v = 0; v < 8u; v++) {
        for (unsigned i = 0; i < n; i++) {
            for (unsigned css = 0; css < 2u; css++) {
                for (unsigned k = 0; k < sizeof ram; k++) ram[k] = (uint8_t)rng();
                /* F at random, and on alternate trials near the top, so the
                 * counter wraps past $FFFF. */
                unsigned f = (v + i + css) & 1u ? 0x78u + (rng() % 8u) : rng() % 128u;
                uint8_t pb = (uint8_t)(vdg_modes[i] | (css ? 0x08u : 0u));
                if (!(pb & 0x80u) && !t1) pb = (uint8_t)(pb | (rng() & 0x60u));
                pb = (uint8_t)(pb | (rng() & 0x07u));   /* the low bits are not the VDG's */
                /* P at random: the VDG's address takes no page bit. */
                uint16_t reg = (uint16_t)(v | f << 3 | (rng() & 1u) << 10);
                char what[96];
                snprintf(what, sizeof what, "SAM V=%u F=$%02X, VDG %s (port B $%02X)", v, f,
                         mc6847_mode_info(pb)->name, pb);
                if (compare(reg, pb, VDG_ARTEFACT_OFF, what)) return 1;
            }
        }
    }

    /* Artefacts: RG6 with CSS high is where they show; the other modes
     * with CSS high must be untouched by them. Both phases. */
    for (unsigned t = 0; t < 2u; t++) {
        vdg_artefact_t a = t ? VDG_ARTEFACT_KRBW : VDG_ARTEFACT_KBRW;
        for (unsigned i = 0; i < n; i++) {
            for (unsigned k = 0; k < sizeof ram; k++) ram[k] = (uint8_t)rng();
            uint8_t pb = (uint8_t)(vdg_modes[i] | 0x08u);
            unsigned v = mc6847_mode_info(pb)->sam_v;
            char what[96];
            snprintf(what, sizeof what, "artefacts %s, VDG %s", t ? "KRBW" : "KBRW",
                     mc6847_mode_info(pb)->name);
            if (compare((uint16_t)(v | (rng() % 128u) << 3), pb, a, what)) return 1;
        }
    }

    /* XRoar's renderers. Its palette's verdict first: of the VDG's
     * colours, only black is black and only buff white to it. */
    for (unsigned c = 0; c < 12u; c++)
        CHECK(xr_black_or_white((uint8_t)c) == (c == VDG_BLACK ? 2 : c == VDG_BUFF ? 3 : 0),
              "XRoar's verdict on colour %u: %d", c, xr_black_or_white((uint8_t)c));
    /* Ours draws the VDG's colours as no table colour: $F800 and up. */
    static uint16_t mark_pal[VDG_COLOUR_COUNT];
    for (unsigned c = 0; c < VDG_COLOUR_COUNT; c++) mark_pal[c] = (uint16_t)(0xF800u | c);
    mark_pal[VDG_ARTEFACT_BLUE] = mc6847_palette[VDG_ARTEFACT_BLUE];
    mark_pal[VDG_ARTEFACT_RED] = mc6847_palette[VDG_ARTEFACT_RED];
    unsigned ccr_fields = 0;
    for (unsigned c = 0; c < 2u; c++) {
        xr_ccr_t ccr = c ? XR_CCR_5BIT : XR_CCR_SIMPLE;
        for (unsigned i = 0; i < n; i++) {
            for (unsigned css = 0; css < 2u; css++) {
                for (unsigned t = 0; t < 2u; t++) {
                    for (unsigned d = 0; d < 2u; d++) {
                        for (unsigned k = 0; k < sizeof ram; k++) ram[k] = (uint8_t)rng();
                        /* Runs and single pixels, as well as noise: a
                         * byte at random, $00, $FF, $55 or $AA. */
                        for (unsigned k = 0; k < sizeof ram; k++) {
                            static const uint8_t pick[4] = { 0x00u, 0xFFu, 0x55u, 0xAAu };
                            if (ram[k] & 0x80u) ram[k] = pick[ram[k] & 3u];
                        }
                        uint8_t pb = (uint8_t)(vdg_modes[i] | (css ? 0x08u : 0u));
                        unsigned v = mc6847_mode_info(pb)->sam_v;
                        char what[96];
                        snprintf(what, sizeof what, "XRoar's %s, %s, VDG %s CSS %u, dark %s",
                                 c ? "5-bit" : "2-bit", t ? "KRBW" : "KBRW",
                                 mc6847_mode_info(pb)->name, css, d ? "drawn" : "black");
                        if (compare_ccr((uint16_t)(v | (rng() % 128u) << 3), pb, ccr, (int)t,
                                        d != 0, mark_pal, what))
                            return 1;
                        ccr_fields++;
                    }
                }
            }
        }
    }
    printf("%u fields through XRoar's renderers\n", ccr_fields);

    printf("%u fields compared with XRoar's %s\n", compared_fields, t1 ? "MC6847T1" : "MC6847");
    TEST_DONE();
}
