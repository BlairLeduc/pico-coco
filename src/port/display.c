/* display.c — what core 1 puts on the panel (display.h, design.md §9). */

#include "display.h"

#include <string.h>

#include "pico/stdlib.h"

#include "config.h"
#include "lcd.h"
#include "mc6847.h"
#include "mc6847_font.h"
#include "present.h"
#include "settings.h"
#include "textpage.h"

#define PIC_W COCO_PIC_W
#define PIC_H COCO_PIC_H

_Static_assert(COCO_PIC_X >= COCO_BORDER_X && COCO_PIC_Y >= COCO_BORDER_Y &&
               COCO_PIC_X + PIC_W + COCO_BORDER_X <= COCO_PANEL_W &&
               COCO_PIC_Y + PIC_H + COCO_BORDER_Y <= COCO_PANEL_H,
               "the border must fit on the panel");
_Static_assert(COCO_TEXT_COLS * COCO_TEXT_GLYPH_W == COCO_PANEL_W,
               "a text line spans the panel");
_Static_assert(COCO_PERF_Y + COCO_TEXT_GLYPH_H <= COCO_PIC_Y - COCO_BORDER_Y,
               "the perf line must sit above the border");
_Static_assert(COCO_STATUS_Y >= COCO_PIC_Y + PIC_H + COCO_BORDER_Y &&
               COCO_STATUS_Y + COCO_TEXT_GLYPH_H <= COCO_PANEL_H,
               "the status line must sit below the border");
_Static_assert(COCO_TEXT_GLYPH_H == MC6847_FONT_ROWS, "the text lines use the VDG's glyphs");
_Static_assert(COCO_LINEBUF_PIXELS >= COCO_PANEL_W, "a line buffer holds a text line");

#define BORDER_PIXELS \
    ((PIC_W + 2u * COCO_BORDER_X) * (PIC_H + 2u * COCO_BORDER_Y) - PIC_W * PIC_H)

/* The renderers live here, on core 1, so their tables are rebuilt by the
 * only core that reads them (§9.3). Two, each with a mode's table: a
 * title that changes colour set on every line (§9.4) alternates between
 * them, where one would rebuild its table twice a line. s_vdg is the
 * one the text lines and the border take their colours from. */
#define VDGS 2u
static mc6847_t  s_vdgs[VDGS];
static unsigned  s_vdg_last;     /* the one used last: the other is rebuilt */
#define s_vdg    (s_vdgs[0])
static present_t s_present;

/* The owner's panel, measured by eye with paneltest.h on 2026-11-23: the
 * codes, as fractions of full, giving a quarter, a half and three
 * quarters of full light (hardware-notes.md §4.10). Red and blue agreed. */
static const double k_panel_rb[3] = { 23.0 / 31, 26.0 / 31, 28.0 / 31 };
static const double k_panel_g[3]  = { 46.0 / 63, 53.0 / 63, 57.0 / 63 };
static mc6847_curve_t s_curve;
static bool           s_curve_on;

static const mc6847_t *vdg_for(void *ctx, uint8_t mode) {
    (void)ctx;
    mode &= VDG_MODE_MASK;
    for (unsigned i = 0; i < VDGS; i++)
        if (s_vdgs[i].mode == mode) {
            s_vdg_last = i;
            return &s_vdgs[i];
        }
    s_vdg_last ^= 1u;
    mc6847_set_mode(&s_vdgs[s_vdg_last], mode);
    return &s_vdgs[s_vdg_last];
}

/* The colour the border was last filled with: black to begin with, since
 * lcd_init clears the panel. Nothing else draws there, so an invalidate
 * leaves it be. M9: the setting that turns it off. */
static uint16_t s_border_rgb = 0x0000u;

/* A full repaint's border, left to the next present (display_present). */
static bool s_border_deferred;

/* DMA ping-pong (hardware-notes.md §4.6), the panel's width so that the
 * text lines can use them too. */
static uint16_t s_line[COCO_LINEBUF_COUNT][COCO_LINEBUF_PIXELS];

/* What each text line shows now: blank, as lcd_init left the panel. */
typedef struct {
    unsigned y;
    char     text[COCO_TEXT_COLS];
} text_line_t;

static text_line_t s_perf   = { .y = COCO_PERF_Y };
static text_line_t s_status = { .y = COCO_STATUS_Y };

void display_init(void) {
    mc6847_curve_build(&s_curve, k_panel_rb, k_panel_g, k_panel_rb);
    s_curve_on = true;
    for (unsigned i = 0; i < VDGS; i++) {
        mc6847_init(&s_vdgs[i]);
        mc6847_set_font(&s_vdgs[i], font_6847);
        mc6847_set_artefact(&s_vdgs[i], VDG_ARTEFACT_KBRW);   /* §9.5: on, on a CoCo */
        mc6847_set_curve(&s_vdgs[i], &s_curve);
    }
    memset(s_perf.text, ' ', sizeof s_perf.text);
    memset(s_status.text, ' ', sizeof s_status.text);
    present_invalidate(&s_present);
}

void display_invalidate(void) {
    present_invalidate(&s_present);
}

void display_set_artefacts(unsigned how, bool red) {
    vdg_artefact_t a = how == 0 ? VDG_ARTEFACT_OFF
                     : how == 1 ? (red ? VDG_ARTEFACT_KRBW : VDG_ARTEFACT_KBRW)
                                : (red ? VDG_ARTEFACT_FULL_KRBW : VDG_ARTEFACT_FULL_KBRW);
    if (a == s_vdg.artefact) return;
    for (unsigned i = 0; i < VDGS; i++) mc6847_set_artefact(&s_vdgs[i], a);
    /* The full artefacts reach two pixels beside a changed byte. */
    s_present.reach = vdg_artefact_full(a) ? 2u : 0u;
    present_invalidate(&s_present);
}

void display_set_palette(unsigned palette) {
    bool tv = palette >= SETTINGS_PALETTE_TV100 && palette < SETTINGS_PALETTE_COUNT;
    const uint16_t *pal = tv ? mc6847_palette_tv[palette - SETTINGS_PALETTE_TV100] : mc6847_palette;
    if (pal == s_vdg.pal) return;
    for (unsigned i = 0; i < VDGS; i++) {
        mc6847_set_palette(&s_vdgs[i], pal);
        mc6847_set_dark_bg(&s_vdgs[i], tv);
    }
    present_invalidate(&s_present);
}

void display_set_curve(bool on) {
    if (on == s_curve_on) return;
    s_curve_on = on;
    for (unsigned i = 0; i < VDGS; i++) mc6847_set_curve(&s_vdgs[i], on ? &s_curve : NULL);
    present_invalidate(&s_present);
}

bool display_curve_on(void) {
    return s_curve_on;
}

uint16_t display_corrected(uint16_t rgb565) {
    return mc6847_curve_apply(s_curve_on ? &s_curve : NULL, rgb565);
}

uint16_t display_colour(unsigned c) {
    return c < VDG_COLOUR_COUNT ? s_vdg.col[c] : 0u;
}

void display_set_t1(bool t1) {
    if (t1 == mc6847_is_t1(&s_vdg)) return;
    for (unsigned i = 0; i < VDGS; i++) mc6847_set_t1(&s_vdgs[i], t1 ? font_6847t1 : NULL);
    present_invalidate(&s_present);
}

/* A frame COCO_BORDER_X wide and COCO_BORDER_Y tall around the picture,
 * in four fills, sent only when its colour changes (EL §5.4): between
 * alphanumeric and graphics, or of CSS in graphics. */
static bool fill_border(uint8_t mode) {
    uint16_t rgb = s_vdg.col[mc6847_border(mode, mc6847_is_t1(&s_vdg))];
    if (rgb == s_border_rgb) return false;
    const unsigned bx = COCO_BORDER_X, by = COCO_BORDER_Y;
    const unsigned x0 = COCO_PIC_X - bx, w = PIC_W + 2u * bx;
    const unsigned x1 = COCO_PIC_X + PIC_W, y1 = COCO_PIC_Y + PIC_H;
    lcd_fill(x0, COCO_PIC_Y - by, w, by, rgb);
    lcd_fill(x0, y1, w, by, rgb);
    lcd_fill(x0, COCO_PIC_Y, bx, PIC_H, rgb);
    lcd_fill(x1, COCO_PIC_Y, bx, PIC_H, rgb);
    s_border_rgb = rgb;
    return true;
}

void display_present(const coco_frame_t *f, display_stats_t *st) {
    uint32_t t0 = time_us_32();
    /* The plan first: a new mode repaints every band, since every byte
     * can stay while every pixel changes (EL §5.3). The border is the
     * mode's that most lines are in (§9.4). */
    present_band_t bands[COCO_BANDS];
    present_plan(&s_present, f, bands);
    uint8_t border = s_present.mode;
    display_stats_t s = { .full = s_present.full };
    /* The border is 43,008 pixels to the picture's 49,152: with a full
     * repaint it made a 22 ms present, over a field, and the pool
     * superseded a snapshot (§17 M11's soak). A full repaint leaves the
     * border to the next present, once, so it lags by a field at most. */
    vdg_colour_t edge = mc6847_border(border, mc6847_is_t1(&s_vdg));
    if (s.full && !s_border_deferred && s_vdg.col[edge] != s_border_rgb) {
        s_border_deferred = true;
    } else {
        s.border = fill_border(border);
        s_border_deferred = false;
    }

    unsigned cur = 0;
    for (unsigned b = 0; b < COCO_BANDS; b++) {
        if (!bands[b].dirty) continue;
        unsigned x0 = bands[b].x0, w = bands[b].x1 - x0 + 1u;
        unsigned y0 = b * COCO_BAND_ROWS;
        lcd_blit_begin(COCO_PIC_X + x0, COCO_PIC_Y + y0, w, COCO_BAND_ROWS);
        for (unsigned y = y0; y < y0 + COCO_BAND_ROWS; y++) {
            /* The buffer not on the wire: lcd_blit_row waits out the
             * previous row's DMA before starting this one. */
            cur ^= 1u;
            /* Each line as the frame says it is drawn (§9.4). */
            static uint16_t s_tmp[COCO_PIC_W];
            present_render_line(f, y, vdg_for, NULL, s_line[cur], s_tmp);
            lcd_blit_row(&s_line[cur][x0], w);
        }
        lcd_blit_end();
        s.bands++;
        s.pixels += w * COCO_BAND_ROWS;
    }
    if (s.border) s.pixels += BORDER_PIXELS;

    s.us = time_us_32() - t0;
    if (st) *st = s;
}

void display_settle(void) {
    if (!s_border_deferred) return;
    fill_border(s_present.mode);
    s_border_deferred = false;
}

static void draw_line(text_line_t *l, const char *text) {
    char line[COCO_TEXT_COLS];
    size_t n = strnlen(text, COCO_TEXT_COLS);
    memcpy(line, text, n);
    memset(line + n, ' ', COCO_TEXT_COLS - n);
    if (memcmp(line, l->text, COCO_TEXT_COLS) == 0) return;
    memcpy(l->text, line, COCO_TEXT_COLS);

    /* The screen's own green on black (pico-atom's). */
    const uint16_t ink = s_vdg.col[VDG_GREEN], paper = 0x0000u;
    unsigned cur = 0;
    lcd_blit_begin(0, l->y, COCO_PANEL_W, COCO_TEXT_GLYPH_H);
    for (unsigned r = 0; r < COCO_TEXT_GLYPH_H; r++) {
        cur ^= 1u;
        uint16_t *px = s_line[cur];
        for (unsigned c = 0; c < COCO_TEXT_COLS; c++) {
            /* Drawn as the byte stands, bit 7 leftmost (mc6847.h). */
            uint8_t bits = font_6847[textpage_glyph(l->text[c]) * MC6847_FONT_ROWS + r];
            for (unsigned b = 0; b < COCO_TEXT_GLYPH_W; b++)
                *px++ = (bits & (0x80u >> b)) ? ink : paper;
        }
        lcd_blit_row(s_line[cur], COCO_PANEL_W);
    }
    lcd_blit_end();
}

void display_perf(const char *text) {
    draw_line(&s_perf, text);
}

void display_status(const char *text) {
    draw_line(&s_status, text);
}
