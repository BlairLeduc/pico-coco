/* mc6847.h — the MC6847 VDG, as the CoCo wires it
 * (design.md §9), adapted from pico-atom's.
 *
 * There is no decoded framebuffer (§9.1): the picture is a function of
 * the bytes the VDG fetches and a few mode bits, so rows are generated
 * straight from a snapshot into RGB565 line buffers. What changes from
 * the Atom is that the SAM, not the VDG, chooses the bytes (§9.2): the
 * snapshot holds, for each of the 192 active lines, the bytes the VDG
 * fetched on it, and this file decodes one line of them in the VDG's
 * mode. Every pairing of SAM and VDG modes, the mismatched ones too,
 * renders through the same path.
 */
#ifndef PICO_COCO_MC6847_H
#define PICO_COCO_MC6847_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* ---- the mode bits, as PIA1 port B carries them (§3.3) --------------- */

/* PB7 A/G, PB6-PB4 GM2-GM0, PB3 CSS: the packed mode is port B's pins
 * with the low three bits cleared. Settled by executing Extended Color
 * BASIC's PMODE and SCREEN on the host (test_video, §18). */
#define VDG_AG        0x80u   /* alpha (0) or graphics (1)             */
#define VDG_GM_MASK   0x70u   /* GM2:GM0                               */
#define VDG_GM_SHIFT  4u
#define VDG_GM0       0x10u   /* also INT/EXT, wired to it (§3.4)      */
#define VDG_CSS       0x08u   /* colour set select                     */
#define VDG_MODE_MASK 0xF8u

static inline uint8_t mc6847_mode_from_pb(uint8_t pb) {
    return (uint8_t)(pb & VDG_MODE_MASK);
}

/* ---- how a byte is wired in the alphanumeric modes (§3.4) ------------ */

/* D7 drives A/S: a byte with bit 7 set is a semigraphics cell, SG4 with
 * GM0 low and SG6 with it high, since INT/EXT is GM0. D6 drives INV. */
#define VDG_BYTE_AS   0x80u
#define VDG_BYTE_INV  0x40u

/* ---- modes ----------------------------------------------------------- */

typedef enum { VDG_ALPHA, VDG_CG, VDG_RG } vdg_kind_t;

typedef struct {
    vdg_kind_t  kind;
    const char *name;
    uint8_t     bytes_per_line;  /* the VDG fetches 16 or 32 a line      */
    uint8_t     px_per_byte;     /* 16 or 8 screen pixels                */
    uint8_t     sam_v;           /* the SAM mode that matches it (§9.2)  */
    uint16_t    vram_bytes;      /* a field's bytes in the matching mode */
} mc6847_mode_info_t;

/* Valid for any mode byte; alpha ignores GM2:0. */
const mc6847_mode_info_t *mc6847_mode_info(uint8_t mode);

/* ---- colours (§9.5) -------------------------------------------------- */

/* The VDG's own set, in the order the chip's colour codes give, then the
 * two artefact colours (§9.5). The order of the first twelve is XRoar's,
 * which the VDG oracle compares against index for index. */
typedef enum {
    VDG_GREEN, VDG_YELLOW, VDG_BLUE, VDG_RED,
    VDG_BUFF, VDG_CYAN, VDG_MAGENTA, VDG_ORANGE,
    VDG_BLACK,
    /* The backgrounds of an alphanumeric cell, by CSS, and of the
     * two-colour graphics modes with CSS low: "dark green" and "dark
     * orange" in the datasheet. Drawn black unless the dark background
     * is chosen (mc6847_set_dark_bg). */
    VDG_DARK_GREEN, VDG_DARK_ORANGE,
    /* The alphanumeric ink with CSS high. */
    VDG_BRIGHT_ORANGE,
    /* NTSC artefacts, in the 256-pixel two-colour mode with CSS high. */
    VDG_ARTEFACT_BLUE, VDG_ARTEFACT_RED,
    VDG_COLOUR_COUNT
} vdg_colour_t;

extern const uint16_t mc6847_palette[VDG_COLOUR_COUNT];

/* The colours the chip's outputs make, derived from the datasheet's
 * voltages through the MC1372 (§9.5), the primaries' alternative. */
extern const uint16_t mc6847_palette_datasheet[VDG_COLOUR_COUNT];

/* The same colours through the luminance output alone (§9.5). */
extern const uint16_t mc6847_palette_mono[VDG_COLOUR_COUNT];

/* The border: black in the alphanumeric modes, green or buff by CSS in
 * the graphics ones. A T1's (t1) in the alphanumeric modes with GM2 high
 * and GM1 low is the ink's, green or orange by CSS. */
vdg_colour_t mc6847_border(uint8_t mode, bool t1);

/* Artefact colours (§9.5): off, or one of the two phases, named as
 * XRoar names its composite inputs by the colours of the pairs 00, 01,
 * 10 and 11 (a pair being two pixels, left then right): black, blue,
 * red, white; or black, red, blue, white.
 *
 * Each phase is drawn simply, a pair at a time from the line's start,
 * as XRoar's 2-bit renderer does, or fully, each pixel by the two either
 * side of it, as its 5-bit one does (mc6847_artefact_line). */
typedef enum {
    VDG_ARTEFACT_OFF,
    VDG_ARTEFACT_KBRW,
    VDG_ARTEFACT_KRBW,
    VDG_ARTEFACT_FULL_KBRW,
    VDG_ARTEFACT_FULL_KRBW,
} vdg_artefact_t;

static inline bool vdg_artefact_full(vdg_artefact_t a) {
    return a == VDG_ARTEFACT_FULL_KBRW || a == VDG_ARTEFACT_FULL_KRBW;
}

/* The phase whose pair 01 is red. */
static inline bool vdg_artefact_krbw(vdg_artefact_t a) {
    return a == VDG_ARTEFACT_KRBW || a == VDG_ARTEFACT_FULL_KRBW;
}

/* ---- the character ROM ----------------------------------------------- */

#define MC6847_FONT_GLYPHS 64u
#define MC6847_FONT_ROWS   12u
#define MC6847_FONT_BYTES  (MC6847_FONT_GLYPHS * MC6847_FONT_ROWS)   /* 768 */

/* The ROM is a flat array: glyph g row r is font[g * 12 + r]. A glyph is
 * 5 pixels wide in bits 5..1 of its byte, and the byte is drawn as it
 * stands, bit 7 leftmost: two spacing columns on the left, one on the
 * right. The 7 glyph rows sit at rows 3..9 of the 12-row cell, which the
 * data carries.
 *
 * The glyph order is the MC6847's own, not ASCII:
 *
 *     index  0..31  ->  ASCII $40..$5F   (@ A..Z [ \ ] ^ _)
 *     index 32..63  ->  ASCII $20..$3F   (space ! " ... ?)
 *
 * Zeroed RAM shows '@' in every cell until the ROM clears the screen, and
 * that is correct (EL §5.5). */
#define MC6847_FONT_GLYPH_W  5u
#define MC6847_FONT_LEFT_BIT 5u
#define MC6847_GLYPH_SPACE   32u

static inline uint8_t mc6847_glyph_ascii(uint8_t index) {
    return (uint8_t)(0x20u + ((index + 0x20u) & 0x3Fu));
}

/* ---- the VDG ---------------------------------------------------------- */

typedef struct {
    uint8_t  mode;             /* packed; VDG_MODE_MASK                  */
    bool     lut_valid;
    const uint16_t *pal;       /* mc6847_palette, the mono one, or a test's */
    bool     dark_bg;
    vdg_artefact_t artefact;

    /* The palette as drawn: pal through dark_bg and the artefact phase. */
    uint16_t col[VDG_COLOUR_COUNT];

    /* Graphics: one entry per byte value, already stretched. Worst case
     * 16 pixels x 2 B x 256 = 8 KiB (§16). */
    uint16_t lut[256u * COCO_LUT_PX_MAX];

    const uint8_t *font;       /* MC6847_FONT_BYTES, or NULL             */
    const uint8_t *font_t1;    /* the T1's 128 glyphs: a T1 when set     */
} mc6847_t;

void mc6847_init(mc6847_t *v);

/* Set the mode. Rebuilds the table only when the mode changes. */
void mc6847_set_mode(mc6847_t *v, uint8_t mode);

/* The character ROM. With none, alpha cells are drawn as hollow boxes:
 * plainly wrong, not plausibly wrong (EL §5.5). */
void mc6847_set_font(mc6847_t *v, const uint8_t *font);
static inline bool mc6847_has_font(const mc6847_t *v) { return v->font != NULL; }

/* The MC6847T1, the CoCo 2B's VDG (§9.2), with its own ROM
 * (font_6847t1), or NULL for the MC6847. It differs only in the
 * alphanumeric modes, as XRoar 1.13 has it (§18: medium):
 *
 *   - GM0 high is lower case, not INT/EXT: a byte's seven bits pick one
 *     of the 128 glyphs, drawn inverted, so $00..$1F are lower case and
 *     $40..$5F upper case, both dark on green as the CoCo's text is.
 *     GM0 low picks among $40..$7F with D6 as INV, as the MC6847 does;
 *   - GM1 high inverts every cell;
 *   - GM2 high, with GM1 low, draws the border in the ink's colour;
 *   - D7 is always SG4: there is no SG6;
 *   - the ink with CSS high is orange, not bright orange. */
void mc6847_set_t1(mc6847_t *v, const uint8_t *font_t1);
static inline bool mc6847_is_t1(const mc6847_t *v) { return v->font_t1 != NULL; }

/* Any palette of VDG_COLOUR_COUNT entries: mc6847_palette, the mono one,
 * or, for a test, one whose entries are their own indices. The mono
 * palette shows no artefacts. */
void mc6847_set_palette(mc6847_t *v, const uint16_t *pal);

/* Colour or monochrome: mc6847_set_palette with one of the two. */
void mc6847_set_mono(mc6847_t *v, bool mono);

/* The dark backgrounds drawn as such, or black. */
void mc6847_set_dark_bg(mc6847_t *v, bool dark);

void mc6847_set_artefact(mc6847_t *v, vdg_artefact_t a);

/* Does this renderer want mc6847_artefact_line on its lines? */
bool mc6847_artefacts_full(const mc6847_t *v);

/* The full artefacts (§9.5), over a line already drawn: XRoar's 5-bit
 * renderer. A pixel whose neighbours either side and itself are each
 * black or buff takes its colour from a table, by whether each of the
 * five pixels centred on it is buff, and by phase, which alternates
 * pixel by pixel from the line's first. Any mode: a two-pixel pattern
 * in RG3 colours as XRoar's does. `left` and `right` are the border's
 * colours either side, as drawn; the line's pixels are compared with
 * the colours the renderer draws black and buff, which in this mode are
 * no other colour's (the dark backgrounds, drawn black, are drawn a
 * shade apart). Nothing is changed unless mc6847_artefacts_full. */
void mc6847_artefact_line(const mc6847_t *v, uint16_t *px, uint16_t left, uint16_t right);

/* One active line, y of 192, from the bytes the VDG fetched on it
 * (`bytes`, bytes_per_line of them), as COCO_PIC_W RGB565 pixels into
 * `dst`. The row within a text cell is y modulo 12, as the VDG counts
 * from the first active line. */
void mc6847_render_line(const mc6847_t *v, const uint8_t *bytes, unsigned y, uint16_t *dst);

/* ---- dirty bands (§9.3, EL §5.3) --------------------------------------- */

/* Compare the lines of band `band` (COCO_BAND_ROWS lines, from
 * band * COCO_BAND_ROWS) between a snapshot and the shadow, each
 * COCO_VDG_LINE_BYTES a line, as `mode` uses them. If any byte the band
 * shows differs, return true with the inclusive pixel columns [x0..x1]
 * covering every one. Each line's bytes are its own, so a band depends
 * on its own lines alone.
 *
 * This sees bytes only. A change of mode or colour set repaints the whole
 * picture while every byte stays the same, so the caller compares the
 * modes first (EL §5.3). */
bool mc6847_band_span(uint8_t mode, const uint8_t *lines, const uint8_t *shadow,
                      unsigned band, uint16_t *x0, uint16_t *x1);

#endif /* PICO_COCO_MC6847_H */
