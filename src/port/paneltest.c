/* paneltest.c — see paneltest.h (design.md §9.5). */

#include "paneltest.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "lcd.h"
#include "textpage.h"

/* The menu's text rows are 12 pixels over the picture (textpage.h). */
#define ROW_PX   (COCO_PIC_H / TEXT_ROWS)
#define PATCHES  6u
#define CELL_PX  40u                /* a patch's column, five characters  */
#define LEFT_PX  8u                 /* one character before the first     */

/* RGB565 codes for the patches: red and blue have 5 bits, green 6. The
 * grey takes the red code for red and blue and the matching green. */
static const uint8_t k_code5[PATCHES] = { 19, 20, 21, 22, 23, 24 };
static const uint8_t k_code6[PATCHES] = { 39, 41, 43, 45, 47, 49 };

/* The band's first text row: red at 1, green at 4, blue at 7, grey at 10,
 * two rows of lines and patches, the labels on the third. */
static unsigned band_row(unsigned band) { return 1u + 3u * band; }

int paneltest_label_row(unsigned band) { return (int)band_row(band) + 2; }

static uint16_t colour(unsigned band, unsigned i) {
    unsigned r = k_code5[i], g = k_code6[i];
    switch (band) {
    case 0:  return (uint16_t)(r << 11);
    case 1:  return (uint16_t)(g << 5);
    case 2:  return (uint16_t)r;
    default: return (uint16_t)(r << 11 | g << 5 | r);
    }
}

static uint16_t full(unsigned band) {
    static const uint16_t f[PANELTEST_BANDS] = { 0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu };
    return f[band];
}

/* The gamma under which code c of max m gives half the light. */
static double gamma_of(unsigned c, unsigned m) {
    return log(0.5) / log((double)c / (double)m);
}

void paneltest_label(unsigned band, char *line, unsigned size) {
    memset(line, ' ', size - 1u);
    line[size - 1u] = 0;
    for (unsigned i = 0; i < PATCHES; i++) {
        double g = band == 1 ? gamma_of(k_code6[i], 63u) : gamma_of(k_code5[i], 31u);
        char t[8];
        snprintf(t, sizeof t, "%.1f", g);
        unsigned at = 2u + 5u * i;
        for (unsigned k = 0; t[k] && at + k < size - 1u; k++) line[at + k] = t[k];
    }
}

void paneltest_draw(void) {
    const unsigned x0 = COCO_PIC_X, w = COCO_PIC_W;
    for (unsigned b = 0; b < PANELTEST_BANDS; b++) {
        unsigned top = COCO_PIC_Y + band_row(b) * ROW_PX, h = 2u * ROW_PX;
        /* Lines across the band, full on the even ones. */
        for (unsigned y = 0; y < h; y++) lcd_fill(x0, top + y, w, 1u, (y & 1u) ? 0u : full(b));
        /* Each patch inset 4 pixels in its column and band, so lines
         * show all round it. */
        for (unsigned i = 0; i < PATCHES; i++)
            lcd_fill(x0 + LEFT_PX + CELL_PX * i + 4u, top + 4u, CELL_PX - 8u, h - 8u, colour(b, i));
    }
    /* The six darkest greys on black, on row 13. */
    unsigned top = COCO_PIC_Y + 13u * ROW_PX;
    lcd_fill(x0, top, w, ROW_PX, 0u);
    for (unsigned i = 0; i < PATCHES; i++) {
        unsigned r = i + 1u, g = 2u * (i + 1u);
        lcd_fill(x0 + LEFT_PX + CELL_PX * i + 4u, top + 2u, CELL_PX - 8u, ROW_PX - 4u,
                 (uint16_t)(r << 11 | g << 5 | r));
    }
}
