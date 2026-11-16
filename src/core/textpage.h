/* textpage.h — the CoCo's text screen as text, and pages of text the
 * emulator draws itself (design.md §9.6, §14).
 *
 * Two directions through one wiring of a byte (§3.4): bit 7 A/S, bit 6
 * INV, and the VDG's own glyph order in bits 5-0, @ A-Z [ \ ] up-arrow
 * left-arrow and then space to ?. Color BASIC writes its text with bit 6
 * set, which the VDG shows dark on green, and clear for inverse.
 *
 * Reading: a row of the guest's 32-column screen as ASCII, from RAM where
 * the SAM's display offset says, for the boot's "OK", the UART's screen
 * dump on the device and the host harness alike (§15.3).
 *
 * Writing: a page is a coco_frame_t in the VDG's alphanumeric mode, 16
 * rows of 32 cells, drawn by the guest's own renderer and presented
 * like any snapshot (EL §5.8, §10). The missing-ROM page now, the menu
 * and its pages from M9. The VDG has no lower case, and neither has a
 * page: lower-case letters are written as capitals.
 *
 * Adapted from pico-atom's textpage, which wrote the Atom's screen bytes.
 */
#ifndef PICO_COCO_TEXTPAGE_H
#define PICO_COCO_TEXTPAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"

#define TEXT_COLS 32
#define TEXT_ROWS 16

/* A screen byte as ASCII: a semigraphics cell as a space, an inverse
 * cell as its plain character. */
char textpage_char(uint8_t b);

/* An ASCII character as the byte Color BASIC writes for it, bit 6 set,
 * or clear when `inverse`. Lower case as upper; a character the VDG
 * lacks as a space. */
uint8_t textpage_byte(char c, bool inverse);

/* The character's glyph in the VDG's set (mc6847_font.h), for drawing
 * text outside the picture: textpage_byte's bits 5-0. */
static inline uint8_t textpage_glyph(char c) {
    return (uint8_t)(textpage_byte(c, false) & 0x3Fu);
}

/* Row `row` of the guest's screen, TEXT_COLS cells from the display
 * offset, as ASCII into `out` with trailing spaces trimmed and a NUL.
 * Returns its length. */
unsigned textpage_screen_row(const coco_t *m, int row, char out[TEXT_COLS + 1]);

/* The first row at or after `from` whose text starts with `s`; -1 if
 * none. */
int textpage_screen_find(const coco_t *m, const char *s, int from);

/* A blank page: alphanumeric, CSS low, every cell a space. */
void textpage_clear(coco_frame_t *f);

/* Text from (row, col), cut at the edge. */
void textpage_put(coco_frame_t *f, int row, int col, const char *s, bool inverse);

/* A whole row: the text, then spaces to the edge, all in one video. */
void textpage_line(coco_frame_t *f, int row, const char *s, bool inverse);

/* The byte in a page's cell. */
uint8_t textpage_cell(const coco_frame_t *f, int row, int col);

#endif /* PICO_COCO_TEXTPAGE_H */
