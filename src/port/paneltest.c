/* paneltest.c — see paneltest.h (design.md §9.5). */

#include "paneltest.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "lcd.h"
#include "log.h"
#include "textpage.h"

/* The menu's text rows are 12 pixels over the picture (textpage.h); the
 * lines fill rows 1 to 11. */
#define ROW_PX     (COCO_PIC_H / TEXT_ROWS)
#define FIELD_TOP  (COCO_PIC_Y + 1u * ROW_PX)
#define FIELD_H    (11u * ROW_PX)
#define PATCH_W    96u
#define PATCH_H    48u

enum { CH_RED, CH_GREEN, CH_BLUE, CH_GREY, CHANNELS };
#define LEVELS 3u                    /* a quarter, a half, three quarters */
#define TESTS  (CHANNELS * LEVELS)
#define VIEW   TESTS                 /* after the tests: a grey ramp and the VDG's colours */
#define PAGES  (TESTS + 1u)

static const char *const k_ch[CHANNELS] = { "RED", "GREEN", "BLUE", "GREY" };
static const char *const k_lv[LEVELS] = { "25%", "50%", "75%" };

/* A code in the channel's own steps: 31 for red and blue, 63 for green
 * and grey (grey's red and blue take half its code). */
static unsigned max_of(unsigned ch) { return ch == CH_GREEN || ch == CH_GREY ? 63u : 31u; }

static struct {
    unsigned test;
    unsigned code[TESTS];
} p;

void paneltest_reset(void) {
    p.test = 0;
    for (unsigned t = 0; t < TESTS; t++) {
        double share = 0.25 * (double)(t % LEVELS + 1u);
        p.code[t] = (unsigned)lround(max_of(t / LEVELS) * pow(share, 1.0 / 2.2));
    }
}

void paneltest_key(int dx, int dy) {
    if (p.test < TESTS) {
        unsigned m = max_of(p.test / LEVELS);
        int c = (int)p.code[p.test] + dx;
        p.code[p.test] = (unsigned)(c < 0 ? 0 : c > (int)m ? (int)m : c);
    }
    if (dy) {
        p.test = (p.test + PAGES + (dy < 0 ? PAGES - 1u : 1u)) % PAGES;
        paneltest_log();
    }
}

static uint16_t rgb(unsigned ch, unsigned code) {
    switch (ch) {
    case CH_RED:   return (uint16_t)(code << 11);
    case CH_GREEN: return (uint16_t)(code << 5);
    case CH_BLUE:  return (uint16_t)code;
    default:       return (uint16_t)((code >> 1) << 11 | code << 5 | (code >> 1));
    }
}

void paneltest_summary(unsigned row, char *line, unsigned size) {
    /* Two channels a row: " RED   8 18 25  GREEN 17 37 52". */
    unsigned a = 2u * row;
    snprintf(line, size, " %-5s %2u %2u %2u  %-5s %2u %2u %2u", k_ch[a], p.code[a * 3u],
             p.code[a * 3u + 1u], p.code[a * 3u + 2u], k_ch[a + 1u], p.code[a * 3u + 3u],
             p.code[a * 3u + 4u], p.code[a * 3u + 5u]);
}

void paneltest_status(char *line, unsigned size) {
    const char *how = display_curve_on() ? "CORRECTED" : "RAW";
    if (p.test == VIEW) {
        snprintf(line, size, " VIEW, %s", how);
        return;
    }
    unsigned ch = p.test / LEVELS;
    snprintf(line, size, " %s %s: %u OF %u, %s", k_ch[ch], k_lv[p.test % LEVELS], p.code[p.test],
             max_of(ch), how);
}

void paneltest_log(void) {
    log_core1("  panel test   : %s, red %u %u %u of 31, green %u %u %u of 63, blue %u %u %u of 31,"
              " grey %u %u %u of 63\n", display_curve_on() ? "corrected" : "raw",
              p.code[0], p.code[1], p.code[2], p.code[3], p.code[4], p.code[5], p.code[6],
              p.code[7], p.code[8], p.code[9], p.code[10], p.code[11]);
}

/* The view: sixteen greys from black to white on rows 1 to 5, the eight
 * colours on rows 6 to 8, and the text screen's pairs on rows 9 to 11,
 * dark green, green, dark orange and orange; each as the screen draws it. */
static void draw_view(void) {
    for (unsigned i = 0; i < 16u; i++) {
        unsigned g = (i * 63u + 7u) / 15u;
        lcd_fill(COCO_PIC_X + 16u * i, FIELD_TOP, 16u, 5u * ROW_PX,
                 display_corrected((uint16_t)((g >> 1) << 11 | g << 5 | (g >> 1))));
    }
    for (unsigned i = 0; i < 8u; i++)
        lcd_fill(COCO_PIC_X + 32u * i, FIELD_TOP + 5u * ROW_PX, 32u, 3u * ROW_PX, display_colour(i));
    static const unsigned pair[4] = { 9u, 0u, 10u, 11u };   /* vdg_colour_t's */
    for (unsigned i = 0; i < 4u; i++)
        lcd_fill(COCO_PIC_X + 64u * i, FIELD_TOP + 8u * ROW_PX, 64u, 3u * ROW_PX,
                 display_colour(pair[i]));
}

void paneltest_draw(void) {
    if (p.test == VIEW) {
        draw_view();
        return;
    }
    unsigned ch = p.test / LEVELS, lit = p.test % LEVELS + 1u;   /* lines lit of 4 */
    uint16_t full = rgb(ch, max_of(ch));
    for (unsigned y = 0; y < FIELD_H; y++)
        lcd_fill(COCO_PIC_X, FIELD_TOP + y, COCO_PIC_W, 1u, (y % 4u) < lit ? full : 0u);
    lcd_fill(COCO_PIC_X + (COCO_PIC_W - PATCH_W) / 2u, FIELD_TOP + (FIELD_H - PATCH_H) / 2u,
             PATCH_W, PATCH_H, display_corrected(rgb(ch, p.code[p.test])));
}
