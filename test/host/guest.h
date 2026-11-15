/* guest.h — a real CoCo, on the host, for the tests that need its ROMs
 * (design.md §15.3), adapted from pico-oric's.
 *
 * Needs the user's ROM images, which the tree does not ship (D10). It
 * looks in PICO_COCO_ROMS if that is set, else the repository's roms/
 * staging directory, and recognises images by SHA-1 whatever they are
 * called (romset.h).
 *
 * Typing goes straight into the matrix by the CoCo's own rows and columns,
 * a key held and released over whole fields. M5 replaces it with the
 * firmware's path, PicoCalc events through the mapping table (§11).
 */
#ifndef PICO_COCO_TEST_GUEST_H
#define PICO_COCO_TEST_GUEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "coco.h"
#include "romset.h"

typedef struct {
    coco_t   m;
    /* Ticks from power-on until "OK" first showed, to within a field; 0
     * if it never did. */
    uint64_t ok_ticks;
} guest_t;

/* Scan for images. True if Color BASIC was found; *dir is where it looked,
 * for the skip message. */
bool guest_find_roms(const char **dir);
bool guest_have_rom(rom_id_t r);
const uint8_t *guest_rom_image(rom_id_t r);

/* Build a CoCo with `ram_k` of RAM and Color BASIC, and Extended Color
 * BASIC if `extended`; load the ROMs, then power on (§13.3). */
void guest_build(guest_t *g, unsigned ram_k, bool extended);

/* Run fields until "OK" shows at the start of a screen row, or for ten
 * guest seconds. True if it showed. */
bool guest_boot(guest_t *g);

void guest_fields(guest_t *g, int n);

/* Type a string a key at a time, SHIFT held where the CoCo's keyboard
 * needs it; '\n' is ENTER. Then a few fields for the ROM to act. */
void guest_type(guest_t *g, const char *s);

/* The text on one of the 16 rows of the 32-column screen, as ASCII,
 * read from where the SAM's display offset says: semigraphics cells as
 * spaces, inverse as the plain character, trailing spaces trimmed. */
const char *guest_row(const coco_t *m, int row);

/* The first row at or after `from` whose text starts with `s`; -1 if
 * none. */
int guest_find_row(const coco_t *m, const char *s, int from);

/* The whole screen, a row per line, for a failing test's output. */
void guest_dump(const coco_t *m, FILE *f);

#endif /* PICO_COCO_TEST_GUEST_H */
