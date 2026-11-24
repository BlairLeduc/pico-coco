/* mc6847.c — the VDG's modes, its tables and a line's pixels
 * (design.md §9). See mc6847.h.
 *
 * What a byte shows in each mode is the MC6847 datasheet's, wired as
 * §3.4 has it, and held index for index to XRoar's VDG by
 * test_vdg_oracle (§15.2).
 */

#include "mc6847.h"

#include <math.h>
#include <string.h>

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* The set is the datasheet's; the analogue values a real VDG and its
 * modulator give are not, so these are the idealised primaries, pico-
 * atom's, and a rendering choice (§9.5). The artefact colours are XRoar's
 * simple renderer's. */
const uint16_t mc6847_palette[VDG_COLOUR_COUNT] = {
    [VDG_GREEN]   = RGB565(0x00, 0xFF, 0x00),
    [VDG_YELLOW]  = RGB565(0xFF, 0xFF, 0x00),
    [VDG_BLUE]    = RGB565(0x00, 0x00, 0xFF),
    [VDG_RED]     = RGB565(0xFF, 0x00, 0x00),
    [VDG_BUFF]    = RGB565(0xFF, 0xFF, 0xFF),
    [VDG_CYAN]    = RGB565(0x12, 0xAF, 0x56),   /* the owner's, 2026-11-24 (§9.5) */
    [VDG_MAGENTA] = RGB565(0xFF, 0x00, 0xFF),
    [VDG_ORANGE]  = RGB565(0xFF, 0x60, 0x00),   /* the owner's, 2026-11-24 (§9.5) */
    [VDG_BLACK]   = RGB565(0x00, 0x00, 0x00),
    /* The datasheet gives no level for either: dark green a quarter of
     * the ink, dark orange a dark red, as the chip's is (§9.5). */
    [VDG_DARK_GREEN]    = RGB565(0x00, 0x40, 0x00),
    [VDG_DARK_ORANGE]   = RGB565(0x44, 0x04, 0x00),   /* the owner's, 2026-11-24 */
    [VDG_BRIGHT_ORANGE] = RGB565(0xFF, 0x80, 0x00),
    [VDG_ARTEFACT_BLUE] = RGB565(0x00, 0x80, 0xFF),
    [VDG_ARTEFACT_RED]  = RGB565(0xFF, 0x80, 0x00),
};

void mc6847_palette_over(uint16_t out[VDG_COLOUR_COUNT], const uint32_t over[VDG_COLOUR_COUNT]) {
    for (unsigned c = 0; c < VDG_COLOUR_COUNT; c++) {
        uint32_t x = over[c];
        out[c] = x > 0xFFFFFFu ? mc6847_palette[c]
                               : RGB565((x >> 16) & 0xFFu, (x >> 8) & 0xFFu, x & 0xFFu);
    }
}

/* The luminance output alone (§9.5). The datasheet's Y levels are 0.72 V
 * for black, 0.65 V for blue and red, 0.54 V for green, cyan, magenta and
 * orange, and 0.42 V for yellow, buff and the text screen's orange;
 * lower is brighter. Scaled so
 * black is black and 0.42 V white. A monochrome set has no artefacts, so
 * their entries are never drawn. */
#define GREY(y_mv) RGB565((720u - (y_mv)) * 255u / 300u, (720u - (y_mv)) * 255u / 300u, \
                          (720u - (y_mv)) * 255u / 300u)
const uint16_t mc6847_palette_mono[VDG_COLOUR_COUNT] = {
    [VDG_GREEN]   = GREY(540u),
    [VDG_YELLOW]  = GREY(420u),
    [VDG_BLUE]    = GREY(650u),
    [VDG_RED]     = GREY(650u),
    [VDG_BUFF]    = GREY(420u),
    [VDG_CYAN]    = GREY(540u),
    [VDG_MAGENTA] = GREY(540u),
    [VDG_ORANGE]  = GREY(540u),
    [VDG_BLACK]   = GREY(720u),
    [VDG_DARK_GREEN]    = GREY(720u),
    [VDG_DARK_ORANGE]   = GREY(720u),
    [VDG_BRIGHT_ORANGE] = GREY(420u),
    [VDG_ARTEFACT_BLUE] = GREY(720u),
    [VDG_ARTEFACT_RED]  = GREY(720u),
};

/* The datasheet's modes by GM2:GM0, then alpha. Bytes a line: the VDG
 * fetches 32 in the alphanumeric modes and in CG2, CG3, CG6 and RG6, and
 * 16 in the rest. */
static const mc6847_mode_info_t modes[9] = {
    { VDG_CG,    "CG1",       16, 16, 1, 1024 },
    { VDG_RG,    "RG1",       16, 16, 1, 1024 },
    { VDG_CG,    "CG2",       32,  8, 2, 2048 },
    { VDG_RG,    "RG2",       16, 16, 3, 1536 },
    { VDG_CG,    "CG3",       32,  8, 4, 3072 },
    { VDG_RG,    "RG3",       16, 16, 5, 3072 },
    { VDG_CG,    "CG6",       32,  8, 6, 6144 },
    { VDG_RG,    "RG6",       32,  8, 6, 6144 },
    { VDG_ALPHA, "Alpha",     32,  8, 0,  512 },
};

const mc6847_mode_info_t *mc6847_mode_info(uint8_t mode) {
    if (!(mode & VDG_AG)) return &modes[8];
    return &modes[(mode & VDG_GM_MASK) >> VDG_GM_SHIFT];
}

vdg_colour_t mc6847_border(uint8_t mode, bool t1) {
    if (!(mode & VDG_AG)) {
        if (t1 && (mode & 0x60u) == 0x40u) return (mode & VDG_CSS) ? VDG_ORANGE : VDG_GREEN;
        return VDG_BLACK;
    }
    return (mode & VDG_CSS) ? VDG_BUFF : VDG_GREEN;
}

/* ---- the table -------------------------------------------------------- */

bool mc6847_artefacts_full(const mc6847_t *v) {
    return vdg_artefact_full(v->artefact) && v->pal != mc6847_palette_mono;
}

/* The simple artefacts, in the table: RG6 with CSS high. */
static bool artefacts_shown(const mc6847_t *v) {
    return v->artefact != VDG_ARTEFACT_OFF && !vdg_artefact_full(v->artefact) &&
           v->pal != mc6847_palette_mono &&
           (v->mode & (VDG_AG | VDG_GM_MASK | VDG_CSS)) == (VDG_AG | VDG_GM_MASK | VDG_CSS);
}

static void build_art5(mc6847_t *v);

static void build_lut(mc6847_t *v) {
    for (unsigned c = 0; c < VDG_COLOUR_COUNT; c++)
        v->col[c] = mc6847_curve_apply(v->curve, v->pal[c]);
    if (!v->dark_bg) {
        v->col[VDG_DARK_GREEN] = v->col[VDG_BLACK];
        v->col[VDG_DARK_ORANGE] = v->col[VDG_BLACK];
        /* The full artefacts know black by its colour, and XRoar's
         * renderer does not take the dark backgrounds for black: a
         * shade apart, the lowest bit of green or red. */
        if (mc6847_artefacts_full(v)) {
            v->col[VDG_DARK_GREEN] = (uint16_t)(v->col[VDG_BLACK] ^ 0x0020u);
            v->col[VDG_DARK_ORANGE] = (uint16_t)(v->col[VDG_BLACK] ^ 0x0800u);
        }
    }
    build_art5(v);
    v->lut_valid = true;

    const mc6847_mode_info_t *info = mc6847_mode_info(v->mode);
    if (info->kind == VDG_ALPHA) return;   /* drawn a cell at a time */

    bool css = (v->mode & VDG_CSS) != 0;
    unsigned scale = info->px_per_byte / 4u;   /* pixels per CG pixel */

    if (info->kind == VDG_CG) {
        unsigned base = css ? VDG_BUFF : VDG_GREEN;
        for (unsigned b = 0; b < 256u; b++) {
            uint16_t *e = &v->lut[b * info->px_per_byte];
            for (unsigned p = 0; p < 4u; p++) {
                uint16_t c = v->col[base + ((b >> (6u - 2u * p)) & 3u)];
                for (unsigned s = 0; s < scale; s++) *e++ = c;
            }
        }
        return;
    }

    /* Two colours: green on dark green, or buff on black. */
    uint16_t fg = v->col[css ? VDG_BUFF : VDG_GREEN];
    uint16_t bg = v->col[css ? VDG_BLACK : VDG_DARK_GREEN];
    scale = info->px_per_byte / 8u;
    if (artefacts_shown(v)) {
        /* NTSC: a pair of pixels, left and right, as one of four colours,
         * as XRoar's simple renderer has them (§9.5). Only RG6 has single
         * pixels to pair; the coarser modes' pairs are always alike. */
        uint16_t pair[4] = { bg, v->col[VDG_ARTEFACT_BLUE], v->col[VDG_ARTEFACT_RED], fg };
        if (vdg_artefact_krbw(v->artefact)) {
            pair[1] = v->col[VDG_ARTEFACT_RED];
            pair[2] = v->col[VDG_ARTEFACT_BLUE];
        }
        for (unsigned b = 0; b < 256u; b++) {
            uint16_t *e = &v->lut[b * 8u];
            for (unsigned p = 0; p < 4u; p++) {
                uint16_t c = pair[(b >> (6u - 2u * p)) & 3u];
                *e++ = c;
                *e++ = c;
            }
        }
        return;
    }
    for (unsigned b = 0; b < 256u; b++) {
        uint16_t *e = &v->lut[b * info->px_per_byte];
        for (unsigned p = 0; p < 8u; p++) {
            uint16_t c = ((b >> (7u - p)) & 1u) ? fg : bg;
            for (unsigned s = 0; s < scale; s++) *e++ = c;
        }
    }
}

void mc6847_init(mc6847_t *v) {
    memset(v, 0, sizeof(*v));
    v->pal = mc6847_palette;
    build_lut(v);
}

void mc6847_set_mode(mc6847_t *v, uint8_t mode) {
    mode &= VDG_MODE_MASK;
    if (v->lut_valid && mode == v->mode) return;
    v->mode = mode;
    build_lut(v);
}

void mc6847_set_font(mc6847_t *v, const uint8_t *font) {
    v->font = font;
}

void mc6847_set_t1(mc6847_t *v, const uint8_t *font_t1) {
    v->font_t1 = font_t1;
}

/* ---- the panel's response (§9.5) --------------------------------------- */

double mc6847_curve_light(const double q[3], double f) {
    static const double share[3] = { 0.25, 0.5, 0.75 };
    double pf[5] = { 0.0, q[0], q[1], q[2], 1.0 }, pl[5] = { 0.0, share[0], share[1], share[2], 1.0 };
    if (f <= 0.0) return 0.0;
    if (f >= 1.0) return 1.0;
    unsigned s = 1u;
    while (s < 4u && f > pf[s + 1u]) s++;
    /* The segment from point s to s + 1 as a power; below the quarter,
     * the next segment's power, carried down. */
    unsigned a = s, b = s + 1u;
    if (f < pf[1]) { a = 1u; b = 2u; }
    double g = log(pl[b] / pl[a]) / log(pf[b] / pf[a]);
    return pl[a] * pow(f / pf[a], g);
}

/* One channel's table: for each code, the code whose light on the panel
 * is nearest, as a display of gamma 2.2 would see it, to what the code
 * gives on such a display. Never falling, 0 to 0 and full to full. */
static void curve_channel(uint8_t *t, unsigned m, const double q[3]) {
    unsigned prev = 0;
    for (unsigned i = 0; i <= m; i++) {
        double want = (double)i / (double)m;
        unsigned best = prev;
        double bd = 2.0;
        for (unsigned o = prev; o <= m; o++) {
            double d = fabs(pow(mc6847_curve_light(q, (double)o / (double)m), 1.0 / 2.2) - want);
            if (d < bd) { bd = d; best = o; }
        }
        if (i == 0) best = 0;
        if (i == m) best = m;
        t[i] = (uint8_t)best;
        prev = best;
    }
}

void mc6847_curve_build(mc6847_curve_t *c, const double r[3], const double g[3],
                        const double b[3]) {
    curve_channel(c->r, 31u, r);
    curve_channel(c->g, 63u, g);
    curve_channel(c->b, 31u, b);
}

uint16_t mc6847_curve_apply(const mc6847_curve_t *c, uint16_t x) {
    if (!c) return x;
    return (uint16_t)(c->r[x >> 11] << 11 | c->g[(x >> 5) & 63u] << 5 | c->b[x & 31u]);
}

void mc6847_set_curve(mc6847_t *v, const mc6847_curve_t *c) {
    if (c == v->curve) return;
    v->curve = c;
    build_lut(v);
}

void mc6847_set_palette(mc6847_t *v, const uint16_t *pal) {
    if (pal == v->pal) return;
    v->pal = pal;
    build_lut(v);
}

void mc6847_set_mono(mc6847_t *v, bool mono) {
    mc6847_set_palette(v, mono ? mc6847_palette_mono : mc6847_palette);
}

void mc6847_set_dark_bg(mc6847_t *v, bool dark) {
    if (dark == v->dark_bg) return;
    v->dark_bg = dark;
    build_lut(v);
}

void mc6847_set_artefact(mc6847_t *v, vdg_artefact_t a) {
    if (a == v->artefact) return;
    v->artefact = a;
    build_lut(v);
}

/* ---- the alphanumeric modes ------------------------------------------- */

/* A semigraphics cell: two elements across, each 4 pixels, on black. SG4
 * (INT/EXT low) has two rows of 6 lines, bits 3-2 then 1-0, in any of the
 * eight colours by bits 6-4. SG6 (INT/EXT high) has three rows of 4,
 * bits 5-4, 3-2, 1-0, in the colour set CSS chooses by bits 7-6; with
 * bit 7 set, as A/S needs, that is blue or red, magenta or orange. */
static void sg_cell(const mc6847_t *v, uint8_t byte, unsigned row, bool sg6, bool css,
                    uint16_t *dst) {
    unsigned bits;
    uint16_t ink;
    if (sg6) {
        bits = byte >> (row < 4u ? 4u : row < 8u ? 2u : 0u);
        ink = v->col[(css ? VDG_BUFF : VDG_GREEN) + ((byte >> 6) & 3u)];
    } else {
        bits = byte >> (row < 6u ? 2u : 0u);
        ink = v->col[(byte >> 4) & 7u];
    }
    uint16_t l = (bits & 2u) ? ink : v->col[VDG_BLACK];
    uint16_t r = (bits & 1u) ? ink : v->col[VDG_BLACK];
    dst[0] = dst[1] = dst[2] = dst[3] = l;
    dst[4] = dst[5] = dst[6] = dst[7] = r;
}

/* A character cell: the internal ROM's glyph row with INT/EXT low; with
 * it high, the byte itself, as there is no external ROM to answer. Ink
 * green or bright orange on its dark background, swapped by INV. */
static void text_cell(const mc6847_t *v, uint8_t byte, unsigned row, bool ext, bool css,
                      uint16_t *dst) {
    uint8_t bits;
    if (ext) {
        bits = byte;
    } else if (v->font) {
        bits = v->font[(byte & 0x3Fu) * MC6847_FONT_ROWS + row];
    } else {
        bits = (row == 0 || row == MC6847_FONT_ROWS - 1u) ? 0xFEu : 0x82u;
    }
    if (byte & VDG_BYTE_INV) bits = (uint8_t)~bits;
    uint16_t fg = v->col[css ? VDG_BRIGHT_ORANGE : VDG_GREEN];
    uint16_t bg = v->col[css ? VDG_DARK_ORANGE : VDG_DARK_GREEN];
    for (unsigned x = 0; x < 8u; x++) dst[x] = ((bits >> (7u - x)) & 1u) ? fg : bg;
}

/* A T1's character cell (mc6847.h): GM0 high, the byte's seven bits
 * pick the glyph and the cell is inverted; low, $40..$7F by its six with
 * D6 as INV. GM1 inverts either. */
static void t1_cell(const mc6847_t *v, uint8_t byte, unsigned row, uint8_t mode,
                    uint16_t *dst) {
    bool lower = (mode & VDG_GM0) != 0;
    unsigned glyph = lower ? (byte & 0x7Fu) : (0x40u | (byte & 0x3Fu));
    bool inv = lower || (byte & VDG_BYTE_INV);
    if (mode & 0x20u) inv = !inv;
    uint8_t bits = v->font_t1[glyph * MC6847_FONT_ROWS + row];
    if (inv) bits = (uint8_t)~bits;
    bool css = (mode & VDG_CSS) != 0;
    uint16_t fg = v->col[css ? VDG_ORANGE : VDG_GREEN];
    uint16_t bg = v->col[css ? VDG_DARK_ORANGE : VDG_DARK_GREEN];
    for (unsigned x = 0; x < 8u; x++) dst[x] = ((bits >> (7u - x)) & 1u) ? fg : bg;
}

/* ---- a line ----------------------------------------------------------- */

void mc6847_render_line(const mc6847_t *v, const uint8_t *bytes, unsigned y, uint16_t *dst) {
    const mc6847_mode_info_t *info = mc6847_mode_info(v->mode);

    if (info->kind == VDG_ALPHA) {
        unsigned row = y % MC6847_FONT_ROWS;
        bool gm0 = (v->mode & VDG_GM0) != 0;
        bool css = (v->mode & VDG_CSS) != 0;
        if (v->font_t1) {
            for (unsigned c = 0; c < 32u; c++) {
                uint8_t b = bytes[c];
                if (b & VDG_BYTE_AS) sg_cell(v, b, row, false, css, dst + c * 8u);
                else                 t1_cell(v, b, row, v->mode, dst + c * 8u);
            }
            return;
        }
        for (unsigned c = 0; c < 32u; c++) {
            uint8_t b = bytes[c];
            if (b & VDG_BYTE_AS) sg_cell(v, b, row, gm0, css, dst + c * 8u);
            else                 text_cell(v, b, row, gm0, css, dst + c * 8u);
        }
        return;
    }

    /* Unrolled by entry size, not memcpy: at 16 or 32 bytes the call is
     * the cost (hardware-notes.md §9.4). */
    if (info->px_per_byte == 8u) {
        for (unsigned b = 0; b < 32u; b++) {
            const uint16_t *e = &v->lut[bytes[b] * 8u];
            uint16_t *d = dst + b * 8u;
            d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; d[3] = e[3];
            d[4] = e[4]; d[5] = e[5]; d[6] = e[6]; d[7] = e[7];
        }
    } else {
        for (unsigned b = 0; b < 16u; b++) {
            const uint16_t *e = &v->lut[bytes[b] * 16u];
            uint16_t *d = dst + b * 16u;
            for (unsigned i = 0; i < 16u; i++) d[i] = e[i];
        }
    }
}

/* ---- the full artefacts (§9.5) ----------------------------------------- */

/* XRoar 1.13's 5-bit table (vo_render.c's cmp_lut_5bit), by phase and by
 * the five pixels centred on one, the leftmost in bit 4, a bit set where
 * the pixel is buff: data, held entry by entry to XRoar's renderer by
 * test_vdg_oracle. The first phase is KBRW's on even pixels. */
static const uint16_t art5[2][32] = {
    {
        RGB565(0x00, 0x00, 0x00), RGB565(0x00, 0x00, 0x00),
        RGB565(0x00, 0x32, 0x78), RGB565(0x00, 0x28, 0x00),
        RGB565(0xFF, 0x8C, 0x64), RGB565(0xFF, 0x8C, 0x64),
        RGB565(0xFF, 0xD2, 0xFF), RGB565(0xFF, 0xF0, 0xC8),
        RGB565(0x00, 0x32, 0x78), RGB565(0x00, 0x00, 0x3C),
        RGB565(0x00, 0x80, 0xFF), RGB565(0x00, 0x80, 0xFF),
        RGB565(0xD2, 0xFF, 0xD2), RGB565(0xFF, 0xFF, 0xFF),
        RGB565(0x64, 0xF0, 0xFF), RGB565(0xFF, 0xFF, 0xFF),
        RGB565(0x3C, 0x00, 0x00), RGB565(0x3C, 0x00, 0x00),
        RGB565(0x00, 0x00, 0x00), RGB565(0x00, 0x28, 0x00),
        RGB565(0xFF, 0x80, 0x00), RGB565(0xFF, 0x80, 0x00),
        RGB565(0xFF, 0xFF, 0xFF), RGB565(0xFF, 0xF0, 0xC8),
        RGB565(0x28, 0x00, 0x28), RGB565(0x28, 0x00, 0x28),
        RGB565(0x00, 0x80, 0xFF), RGB565(0x00, 0x80, 0xFF),
        RGB565(0xFF, 0xF0, 0xC8), RGB565(0xFF, 0xF0, 0xC8),
        RGB565(0xFF, 0xFF, 0xFF), RGB565(0xFF, 0xFF, 0xFF),
    },
    {
        RGB565(0x00, 0x00, 0x00), RGB565(0x00, 0x00, 0x00),
        RGB565(0xB4, 0x3C, 0x1E), RGB565(0x28, 0x00, 0x28),
        RGB565(0x46, 0xC8, 0xFF), RGB565(0x46, 0xC8, 0xFF),
        RGB565(0xD2, 0xFF, 0xD2), RGB565(0x64, 0xF0, 0xFF),
        RGB565(0xB4, 0x3C, 0x1E), RGB565(0x3C, 0x00, 0x00),
        RGB565(0xFF, 0x80, 0x00), RGB565(0xFF, 0x80, 0x00),
        RGB565(0xFF, 0xD2, 0xFF), RGB565(0xFF, 0xFF, 0xFF),
        RGB565(0xFF, 0xF0, 0xC8), RGB565(0xFF, 0xFF, 0xFF),
        RGB565(0x00, 0x00, 0x3C), RGB565(0x00, 0x00, 0x3C),
        RGB565(0x00, 0x00, 0x00), RGB565(0x28, 0x00, 0x28),
        RGB565(0x00, 0x80, 0xFF), RGB565(0x00, 0x80, 0xFF),
        RGB565(0xFF, 0xFF, 0xFF), RGB565(0x64, 0xF0, 0xFF),
        RGB565(0x00, 0x28, 0x00), RGB565(0x00, 0x28, 0x00),
        RGB565(0xFF, 0x80, 0x00), RGB565(0xFF, 0x80, 0x00),
        RGB565(0x64, 0xF0, 0xFF), RGB565(0x64, 0xF0, 0xFF),
        RGB565(0xFF, 0xFF, 0xFF), RGB565(0xFF, 0xFF, 0xFF),
    },
};

static void build_art5(mc6847_t *v) {
    for (unsigned p = 0; p < 2u; p++)
        for (unsigned i = 0; i < 32u; i++) v->art5[p][i] = mc6847_curve_apply(v->curve, art5[p][i]);
}

void mc6847_artefact_line(const mc6847_t *v, uint16_t *px, uint16_t left, uint16_t right) {
    if (!mc6847_artefacts_full(v)) return;
    const uint16_t black = v->col[VDG_BLACK], white = v->col[VDG_BUFF];
    /* Per pixel, from two left of the line to two right: bit 1 black or
     * white, bit 0 white, as XRoar's is_black_or_white has it. */
    uint8_t f[COCO_PIC_W + 4u];
    uint8_t fl = left == white ? 3u : left == black ? 2u : 0u;
    uint8_t fr = right == white ? 3u : right == black ? 2u : 0u;
    f[0] = f[1] = fl;
    f[COCO_PIC_W + 2u] = f[COCO_PIC_W + 3u] = fr;
    bool any = false;
    for (unsigned x = 0; x < COCO_PIC_W; x++) {
        uint16_t c = px[x];
        uint8_t k = c == white ? 3u : c == black ? 2u : 0u;
        f[x + 2u] = k;
        any |= k == 3u;
    }
    /* With no buff on the line or beside it, the table gives black for
     * black: nothing changes where black is drawn as the table's. */
    if (!any && !(fl & 1u) && !(fr & 1u) && black == v->art5[0][0]) return;
    unsigned p = vdg_artefact_krbw(v->artefact) ? 1u : 0u;
    unsigned idx = (f[0] & 1u) << 3 | (f[1] & 1u) << 2 | (f[2] & 1u) << 1 | (f[3] & 1u);
    for (unsigned x = 0; x < COCO_PIC_W; x++) {
        /* f[x + 2] is pixel x: the window is f[x .. x + 4]. */
        idx = ((idx << 1) | (f[x + 4u] & 1u)) & 31u;
        if (f[x + 1u] & f[x + 2u] & f[x + 3u] & 2u) px[x] = v->art5[(x & 1u) ^ p][idx];
    }
}

/* ---- dirty bands ------------------------------------------------------ */

bool mc6847_band_span(uint8_t mode, const uint8_t *lines, const uint8_t *shadow,
                      unsigned band, uint16_t *x0, uint16_t *x1) {
    const mc6847_mode_info_t *info = mc6847_mode_info(mode);
    unsigned n = info->bytes_per_line, lo = n, hi = 0;
    for (unsigned y = band * COCO_BAND_ROWS; y < (band + 1u) * COCO_BAND_ROWS; y++) {
        const uint8_t *a = lines + y * COCO_VDG_LINE_BYTES;
        const uint8_t *b = shadow + y * COCO_VDG_LINE_BYTES;
        for (unsigned c = 0; c < n; c++) {
            if (a[c] != b[c]) {
                if (c < lo) lo = c;
                if (c > hi) hi = c;
            }
        }
    }
    if (lo > hi) return false;
    *x0 = (uint16_t)(lo * info->px_per_byte);
    *x1 = (uint16_t)((hi + 1u) * info->px_per_byte - 1u);
    return true;
}
