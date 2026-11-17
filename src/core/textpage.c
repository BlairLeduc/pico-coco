/* textpage.c — see textpage.h. */

#include "textpage.h"

#include <string.h>

#include "mc6847.h"

/* A text row is 12 of the VDG's lines (mc6847.h). */
#define CELL_LINES MC6847_FONT_ROWS

_Static_assert(TEXT_ROWS * CELL_LINES == COCO_PIC_H, "16 rows of 12 lines fill the picture");
_Static_assert(TEXT_COLS <= COCO_VDG_LINE_BYTES, "a text row fits a line's bytes");

char textpage_char(uint8_t b) {
    if (b & VDG_BYTE_AS) return ' ';
    return (char)mc6847_glyph_ascii(b & 0x3Fu);
}

uint8_t textpage_byte(char c, bool inverse) {
    uint8_t ch = (uint8_t)c;
    if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 'A');
    uint8_t glyph;
    if (ch >= 0x40u && ch <= 0x5Fu) glyph = (uint8_t)(ch - 0x40u);
    else if (ch >= 0x20u && ch <= 0x3Fu) glyph = ch;
    else glyph = MC6847_GLYPH_SPACE;
    return (uint8_t)(glyph | (inverse ? 0u : VDG_BYTE_INV));
}

unsigned textpage_screen_row(const coco_t *m, int row, char out[TEXT_COLS + 1]) {
    uint16_t base = (uint16_t)(sam6883_display_base(&m->sam) + row * TEXT_COLS);
    unsigned n = 0;
    for (int c = 0; c < TEXT_COLS; c++) {
        int32_t i = coco_ram_index(m, (uint16_t)(base + c));
        out[n++] = i < 0 ? ' ' : textpage_char(m->ram[i]);
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

int textpage_screen_find(const coco_t *m, const char *s, int from) {
    size_t len = strlen(s);
    char row[TEXT_COLS + 1];
    for (int r = from < 0 ? 0 : from; r < TEXT_ROWS; r++) {
        textpage_screen_row(m, r, row);
        if (strncmp(row, s, len) == 0) return r;
    }
    return -1;
}

static void set_cell(coco_frame_t *f, int row, int col, uint8_t b) {
    uint8_t *p = &f->lines[(unsigned)row * CELL_LINES * COCO_VDG_LINE_BYTES + (unsigned)col];
    for (unsigned l = 0; l < CELL_LINES; l++) p[l * COCO_VDG_LINE_BYTES] = b;
}

void textpage_clear(coco_frame_t *f) {
    memset(f, 0, sizeof *f);
    f->mode = 0;   /* alphanumeric, CSS low */
    coco_frame_plain(f);
    f->sam_v = 0;
    for (int r = 0; r < TEXT_ROWS; r++)
        for (int c = 0; c < TEXT_COLS; c++) set_cell(f, r, c, textpage_byte(' ', false));
}

void textpage_put(coco_frame_t *f, int row, int col, const char *s, bool inverse) {
    if (row < 0 || row >= TEXT_ROWS) return;
    for (; *s && col < TEXT_COLS; s++, col++)
        if (col >= 0) set_cell(f, row, col, textpage_byte(*s, inverse));
}

void textpage_line(coco_frame_t *f, int row, const char *s, bool inverse) {
    if (row < 0 || row >= TEXT_ROWS) return;
    int col = 0;
    for (; *s && col < TEXT_COLS; s++, col++) set_cell(f, row, col, textpage_byte(*s, inverse));
    for (; col < TEXT_COLS; col++) set_cell(f, row, col, textpage_byte(' ', inverse));
}

uint8_t textpage_cell(const coco_frame_t *f, int row, int col) {
    return f->lines[(unsigned)row * CELL_LINES * COCO_VDG_LINE_BYTES + (unsigned)col];
}
