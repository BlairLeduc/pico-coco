/* test_textpage.c — a page of text as a VDG frame, and the screen read
 * as text (textpage.h, design.md §9.6). Needs no ROM; test_boot holds
 * both to what Color BASIC itself writes.
 */

#include <string.h>

#include "mc6847.h"
#include "textpage.h"
#include "test_util.h"

static coco_frame_t f;

int main(void) {
    /* Every character the VDG has, there and back, plain and inverse:
     * plain sets bit 6 as BASIC writes it, and bit 7 is never set. */
    for (unsigned c = 0x20; c < 0x60; c++) {
        uint8_t b = textpage_byte((char)c, false), inv = textpage_byte((char)c, true);
        CHECK((b & VDG_BYTE_INV) && !(inv & VDG_BYTE_INV) && !(b & VDG_BYTE_AS),
              "'%c': %02X %02X", c, b, inv);
        CHECK(textpage_char(b) == (char)c && textpage_char(inv) == (char)c, "'%c' back: %c %c",
              c, textpage_char(b), textpage_char(inv));
    }
    /* The VDG's own order: @ is glyph 0, space glyph 32. */
    CHECK(textpage_glyph('@') == 0 && textpage_glyph(' ') == MC6847_GLYPH_SPACE &&
          textpage_glyph('A') == 1 && textpage_glyph('0') == 0x30,
          "glyphs: %u %u %u %u", textpage_glyph('@'), textpage_glyph(' '), textpage_glyph('A'),
          textpage_glyph('0'));
    CHECK(textpage_byte('q', false) == textpage_byte('Q', false), "lower case as upper");
    CHECK(textpage_byte('~', false) == textpage_byte(' ', false) &&
          textpage_byte('\n', true) == textpage_byte(' ', true), "what the VDG lacks, a space");
    CHECK(textpage_char(0x80u | 0x41u) == ' ' && textpage_char(0xFFu) == ' ',
          "semigraphics read as spaces");

    /* A page: alphanumeric, blank, each cell's byte on all 12 of its
     * lines and nowhere else. */
    memset(&f, 0xA5, sizeof f);
    textpage_clear(&f);
    CHECK(f.mode == 0 && f.sam_v == 0, "alphanumeric: mode %02X, SAM %u", f.mode, f.sam_v);
    textpage_put(&f, 3, 30, "HELLO", false);
    textpage_put(&f, 15, -2, "XYZ", true);
    textpage_put(&f, 16, 0, "OFF", false);
    unsigned bad = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        for (unsigned c = 0; c < COCO_VDG_LINE_BYTES; c++) {
            uint8_t want = 0;
            if (c < TEXT_COLS) {
                int row = (int)(y / 12u);
                want = textpage_byte(' ', false);
                if (row == 3 && c == 30) want = textpage_byte('H', false);
                if (row == 3 && c == 31) want = textpage_byte('E', false);
                if (row == 15 && c == 0) want = textpage_byte('Z', true);
            }
            if (f.lines[y * COCO_VDG_LINE_BYTES + c] != want) bad++;
        }
    }
    CHECK(bad == 0, "the page's bytes: %u wrong, cut at both edges and below", bad);
    CHECK(textpage_cell(&f, 3, 31) == textpage_byte('E', false), "a cell read back");

    textpage_line(&f, 5, "AB", true);
    CHECK(textpage_cell(&f, 5, 0) == textpage_byte('A', true) &&
          textpage_cell(&f, 5, 31) == textpage_byte(' ', true), "a whole row, padded");

    TEST_DONE();
}
