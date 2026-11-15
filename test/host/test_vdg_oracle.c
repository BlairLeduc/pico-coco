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
 * with artefacts on, in both phases.
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

int main(void) {
    for (unsigned i = 0; i < VDG_COLOUR_COUNT; i++) index_pal[i] = (uint16_t)i;
    mc6847_init(&vdg);
    mc6847_set_font(&vdg, font_6847);
    mc6847_set_palette(&vdg, index_pal);
    mc6847_set_dark_bg(&vdg, true);   /* the dark colours as their own indices */

    /* The VDG's modes: alpha with INT/EXT (GM0) low and high, the other GM
     * bits set at random, which alpha ignores; and the eight graphics
     * modes. */
    uint8_t vdg_modes[10];
    unsigned n = 0;
    vdg_modes[n++] = 0x00u;
    vdg_modes[n++] = 0x10u;
    for (unsigned gm = 0; gm < 8u; gm++) vdg_modes[n++] = (uint8_t)(0x80u | gm << 4);

    for (unsigned v = 0; v < 8u; v++) {
        for (unsigned i = 0; i < n; i++) {
            for (unsigned css = 0; css < 2u; css++) {
                for (unsigned k = 0; k < sizeof ram; k++) ram[k] = (uint8_t)rng();
                /* F at random, and on alternate trials near the top, so the
                 * counter wraps past $FFFF. */
                unsigned f = (v + i + css) & 1u ? 0x78u + (rng() % 8u) : rng() % 128u;
                uint8_t pb = (uint8_t)(vdg_modes[i] | (css ? 0x08u : 0u));
                if (!(pb & 0x80u)) pb = (uint8_t)(pb | (rng() & 0x60u));
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

    printf("%u fields compared with XRoar's\n", compared_fields);
    TEST_DONE();
}
