/* vdg_scenes.c — see vdg_scenes.h. */

#include "vdg_scenes.h"

#include <string.h>

/* ---- graphics: frame, diagonal, bars --------------------------------- */

/* Pixel (x, y) of a graphics mode laid out as its matched SAM mode reads
 * it: bytes_per_line bytes a row, MSB first. */
static void plot(uint8_t *ram, uint8_t mode, unsigned x, unsigned y, unsigned c) {
    const mc6847_mode_info_t *info = mc6847_mode_info(mode);
    unsigned bpp = info->kind == VDG_CG ? 2u : 1u;
    unsigned per = 8u / bpp;
    uint8_t *b = &ram[VDG_SCENE_BASE + y * info->bytes_per_line + x / per];
    unsigned shift = (per - 1u - x % per) * bpp;
    unsigned mask = ((1u << bpp) - 1u) << shift;
    *b = (uint8_t)((*b & ~mask) | ((c << shift) & mask));
}

static void fill_graphics(uint8_t *ram, uint8_t mode) {
    const mc6847_mode_info_t *info = mc6847_mode_info(mode);
    unsigned per = info->kind == VDG_CG ? 4u : 8u;
    unsigned w = info->bytes_per_line * per;
    unsigned h = info->vram_bytes / info->bytes_per_line;
    unsigned ink = info->kind == VDG_CG ? 3u : 1u;
    for (unsigned x = 0; x < w; x++) { plot(ram, mode, x, 0, ink); plot(ram, mode, x, h - 1u, ink); }
    for (unsigned y = 0; y < h; y++) { plot(ram, mode, 0, y, ink); plot(ram, mode, w - 1u, y, ink); }
    for (unsigned x = 0; x < w; x++) plot(ram, mode, x, x * h / w, info->kind == VDG_CG ? 2u : 1u);
    /* Bars across the middle third: the four colours, or for two colours
     * stripes one, two and four pixels wide. */
    for (unsigned y = h / 3u; y < 2u * h / 3u; y++) {
        for (unsigned x = w / 8u; x < 7u * w / 8u; x++) {
            unsigned c;
            if (info->kind == VDG_CG) c = (x - w / 8u) * 4u / (3u * w / 4u);
            else {
                unsigned band = (y - h / 3u) * 3u / (h / 3u + 1u);
                c = (x >> band) & 1u;
            }
            plot(ram, mode, x, y, c);
        }
    }
}

/* ---- text ------------------------------------------------------------ */

static void put_text(uint8_t *row, const char *s, bool inverse) {
    for (; *s; s++, row++) {
        uint8_t c = (uint8_t)*s;
        *row = (uint8_t)((c & 0x3Fu) | (inverse ? VDG_BYTE_INV : 0u));
    }
}

/* Rows 0-1: the 64 glyphs; rows 2-3: inverse; rows 5-8: the 128
 * semigraphics bytes; then a line of each. */
static void fill_text(uint8_t *ram, uint8_t mode) {
    (void)mode;
    uint8_t *s = &ram[VDG_SCENE_BASE];
    for (unsigned i = 0; i < 64u; i++) {
        s[i] = (uint8_t)i;
        s[64u + i] = (uint8_t)(i | VDG_BYTE_INV);
    }
    for (unsigned i = 0; i < 128u; i++) s[5u * 32u + i] = (uint8_t)(0x80u | i);
    put_text(&s[10u * 32u], "PLAIN TEXT, GLYPHS 0-63", false);
    put_text(&s[11u * 32u], "INVERSE, AS BASIC PRINTS", true);
    for (unsigned i = 0; i < 32u; i++) s[13u * 32u + i] = (uint8_t)(0x80u | (i & 7u) << 4 | 0x0Fu);
    for (unsigned i = 0; i < 32u; i++) s[14u * 32u + i] = (uint8_t)(0x80u | (i & 7u) << 4 | (i & 0x0Fu));
    for (unsigned i = 0; i < 32u; i++) s[15u * 32u + i] = (uint8_t)(0xC0u | (i & 0x3Fu));
}

/* Semigraphics 8, 12 and 24 (§9.2): a text-mode VDG with the SAM
 * advancing a row every 3, 2 or 1 lines, so each cell of 12 lines is
 * built of 4, 6 or 12 bytes. Each row of bytes a colour by its column,
 * with alternate elements lit, and every fourth row clear: a checker in
 * rows of the cell's subdivisions. */
static void fill_sg(uint8_t *ram, uint8_t mode) {
    (void)mode;
    for (unsigned r = 0; r < 192u; r++)
        for (unsigned c = 0; c < 32u; c++)
            ram[VDG_SCENE_BASE + r * 32u + c] =
                (uint8_t)(r % 4u == 3u ? 0x80u
                                       : 0x80u | (c & 7u) << 4 | ((r + c) & 1u ? 0x0Au : 0x05u));
}

/* ---- artefacts ------------------------------------------------------- */

/* RG6 in bands of one byte each: every pair pattern, and stripes. */
static void fill_artefact(uint8_t *ram, uint8_t mode) {
    fill_graphics(ram, mode);
    static const uint8_t bands[8] = { 0x00, 0x55, 0xAA, 0xFF, 0x33, 0xCC, 0x0F, 0x1B };
    for (unsigned y = 132; y < 192u; y++)
        for (unsigned c = 0; c < 32u; c++)
            ram[VDG_SCENE_BASE + y * 32u + c] = bands[c / 4u];
}

/* RG6 in bands of 12 lines: lit runs of 1 to 8 pixels with gaps as
 * long, starting on an even column in the top eight bands and an odd
 * one in the rest, for the full artefacts (§9.5): runs of three or more
 * white with fringes, either way. */
static void fill_runs(uint8_t *ram, uint8_t mode) {
    (void)mode;
    for (unsigned y = 0; y < 192u; y++) {
        unsigned band = y / 12u, run = band % 8u + 1u, off = band / 8u;
        for (unsigned c = 0; c < 32u; c++) {
            uint8_t b = 0;
            for (unsigned p = 0; p < 8u; p++) {
                unsigned x = c * 8u + p;
                if (x >= off && ((x - off) / run) % 2u == 0u) b |= (uint8_t)(0x80u >> p);
            }
            ram[VDG_SCENE_BASE + y * 32u + c] = b;
        }
    }
}

/* ---- the list -------------------------------------------------------- */

#define G(gm) (uint8_t)(VDG_AG | (gm) << VDG_GM_SHIFT)

const vdg_scene_t vdg_scenes[] = {
    { "alpha-css0",    0x00,                0, VDG_ARTEFACT_OFF, fill_text },
    { "alpha-css1",    VDG_CSS,             0, VDG_ARTEFACT_OFF, fill_text },
    { "alpha-ext-css0", VDG_GM0,            0, VDG_ARTEFACT_OFF, fill_text },
    { "alpha-ext-css1", VDG_GM0 | VDG_CSS,  0, VDG_ARTEFACT_OFF, fill_text },
    { "cg1-css0", G(0),           1, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg1-css1", G(0) | VDG_CSS, 1, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg1-css0", G(1),           1, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg1-css1", G(1) | VDG_CSS, 1, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg2-css0", G(2),           2, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg2-css1", G(2) | VDG_CSS, 2, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg2-css0", G(3),           3, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg2-css1", G(3) | VDG_CSS, 3, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg3-css0", G(4),           4, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg3-css1", G(4) | VDG_CSS, 4, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg3-css0", G(5),           5, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg3-css1", G(5) | VDG_CSS, 5, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg6-css0", G(6),           6, VDG_ARTEFACT_OFF, fill_graphics },
    { "cg6-css1", G(6) | VDG_CSS, 6, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg6-css0", G(7),           6, VDG_ARTEFACT_OFF, fill_graphics },
    { "rg6-css1", G(7) | VDG_CSS, 6, VDG_ARTEFACT_OFF, fill_graphics },
    { "sg8",  0x00, 2, VDG_ARTEFACT_OFF, fill_sg },
    { "sg12", 0x00, 4, VDG_ARTEFACT_OFF, fill_sg },
    { "sg24", 0x00, 6, VDG_ARTEFACT_OFF, fill_sg },
    { "rg6-kbrw", G(7) | VDG_CSS, 6, VDG_ARTEFACT_KBRW, fill_artefact },
    { "rg6-krbw", G(7) | VDG_CSS, 6, VDG_ARTEFACT_KRBW, fill_artefact },
    { "rg6-full-kbrw", G(7) | VDG_CSS, 6, VDG_ARTEFACT_FULL_KBRW, fill_artefact },
    { "rg6-full-krbw", G(7) | VDG_CSS, 6, VDG_ARTEFACT_FULL_KRBW, fill_artefact },
    { "rg6-runs-simple", G(7) | VDG_CSS, 6, VDG_ARTEFACT_KBRW, fill_runs },
    { "rg6-runs-full", G(7) | VDG_CSS, 6, VDG_ARTEFACT_FULL_KBRW, fill_runs },
};
const unsigned vdg_scene_count = sizeof vdg_scenes / sizeof vdg_scenes[0];

void vdg_scene_frame(const vdg_scene_t *s, coco_t *m, coco_frame_t *f) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(m, &cfg);
    m->sam.reg = (uint16_t)(s->sam_v | (VDG_SCENE_BASE >> 9) << 3 | 2u << 13);
    m->pia1.b.ddr = 0xF8u;
    m->pia1.b.out = s->mode;
    s->fill(m->ram, s->mode);
    coco_video_snapshot(m, f);
}
