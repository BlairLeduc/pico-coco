/* display.c — what core 1 puts on the panel (design.md §9). */

#include "display.h"

#include "pico/stdlib.h"

#include "config.h"
#include "lcd.h"

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3))

#define PIC_W COCO_PIC_W
#define PIC_H COCO_PIC_H

_Static_assert(COCO_PIC_X + PIC_W <= COCO_PANEL_W && COCO_PIC_Y + PIC_H <= COCO_PANEL_H,
               "the picture must fit on the panel");
_Static_assert(COCO_LINEBUF_PIXELS >= PIC_W, "a line buffer holds a picture row");

/* DMA ping-pong (hardware-notes.md §4.6). */
static uint16_t s_line[COCO_LINEBUF_COUNT][COCO_LINEBUF_PIXELS];

void display_test_pattern(void) {
    const unsigned x = COCO_PIC_X, y = COCO_PIC_Y;
    const unsigned w = PIC_W, h = PIC_H;
    const unsigned pw = COCO_PANEL_W, ph = COCO_PANEL_H;
    const uint16_t white = RGB565(0xFF, 0xFF, 0xFF);
    const uint16_t grey = RGB565(0x80, 0x80, 0x80);

    lcd_fill(0, 0, pw, ph, 0x0000);

    lcd_fill(0, 0, pw, 1, grey);
    lcd_fill(0, ph - 1u, pw, 1, grey);
    lcd_fill(0, 0, 1, ph, grey);
    lcd_fill(pw - 1u, 0, 1, ph, grey);

    lcd_fill(x, y, w, 1, white);               /* top    */
    lcd_fill(x, y + h - 1u, w, 1, white);      /* bottom */
    lcd_fill(x, y, 1, h, white);               /* left   */
    lcd_fill(x + w - 1u, y, 1, h, white);      /* right  */

    lcd_fill(x + 2u, y + 2u, 16, 16, RGB565(0xFF, 0x00, 0x00));
    lcd_fill(x + w - 18u, y + 2u, 16, 16, RGB565(0x00, 0xFF, 0x00));
    lcd_fill(x + 2u, y + h - 18u, 16, 16, RGB565(0x00, 0x00, 0xFF));
    lcd_fill(x + w - 18u, y + h - 18u, 16, 16, RGB565(0xFF, 0xFF, 0x00));
}

void display_measure(uint32_t *fill_us, uint32_t *blit_us, uint32_t *row_us,
                     uint32_t *panel_us) {
    const unsigned x = COCO_PIC_X, y = COCO_PIC_Y;

    /* Two different rows, so the blit is seen to happen. */
    for (unsigned i = 0; i < PIC_W; i++) {
        s_line[0][i] = RGB565(i, 0x40, 0xFF - i);
        s_line[1][i] = RGB565(0xFF - i, i, 0x40);
    }

    uint32_t t0 = time_us_32();
    lcd_fill(x, y, PIC_W, PIC_H, RGB565(0x00, 0x00, 0x80));
    *fill_us = time_us_32() - t0;

    /* Rows as the presenter will send them: lcd_blit_row waits out the
     * previous row's DMA before starting this one. */
    t0 = time_us_32();
    lcd_blit_begin(x, y, PIC_W, PIC_H);
    for (unsigned r = 0; r < PIC_H; r++) lcd_blit_row(s_line[r & 1u], PIC_W);
    lcd_blit_end();
    *blit_us = time_us_32() - t0;

    t0 = time_us_32();
    lcd_blit_begin(x, y + PIC_H / 2u, PIC_W, 1);
    lcd_blit_row(s_line[0], PIC_W);
    lcd_blit_end();
    *row_us = time_us_32() - t0;

    t0 = time_us_32();
    lcd_fill(0, 0, COCO_PANEL_W, COCO_PANEL_H, 0x0000);
    *panel_us = time_us_32() - t0;
}
