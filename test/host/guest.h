/* guest.h — a real CoCo, on the host, for the tests that need
 * its ROMs (design.md §15.3), adapted from pico-oric's.
 *
 * Needs the user's ROM images, which the tree does not ship (D10). It
 * looks in PICO_COCO_ROMS if that is set, else the repository's roms/
 * staging directory, and recognises images by SHA-1 whatever they are
 * called (romset.h).
 *
 * Typing takes the firmware's path: PicoCalc events, as
 * keymap_picocalc_text makes them, through the held-key set and the
 * mapping table into the matrix, one keymatrix_field a field (§11).
 */
#ifndef PICO_COCO_TEST_GUEST_H
#define PICO_COCO_TEST_GUEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "coco.h"
#include "keymatrix.h"
#include "romset.h"

typedef struct guest_s {
    coco_t      m;
    keymatrix_t k;
    /* Called after every field the harness runs, when set: the audio
     * tests drain the samples there. */
    void      (*after_field)(struct guest_s *g);
    /* Ticks from power-on until "OK" first showed, to within a field; 0
     * if it never did. */
    uint64_t ok_ticks;
} guest_t;

/* Scan for images. True if Color BASIC was found; *dir is where it looked,
 * for the skip message. */
bool guest_find_roms(const char **dir);
bool guest_have_rom(rom_id_t r);
const uint8_t *guest_rom_image(rom_id_t r);

/* Build a CoCo with `ram_k` of RAM and Color BASIC 1.3, and Extended
 * Color BASIC if `extended`; load the ROMs, then power on (§13.3). */
void guest_build(guest_t *g, unsigned ram_k, bool extended);

/* The same for any BASIC: `basic` is a Color BASIC at $A000, with
 * Extended at $8000 if `extended`. The caller checks the images are
 * present. */
void guest_build_as(guest_t *g, unsigned ram_k, rom_id_t basic, bool extended);

/* Disk Extended Color BASIC: Color BASIC 1.3, Extended 1.1 and the
 * RS-DOS cartridge with Disk BASIC 1.1, in `ram_k` of 16 or more. The
 * caller checks the images are present and serves the drives
 * (disk_util.h), from after_field. */
void guest_build_disk(guest_t *g, unsigned ram_k);

/* Run fields until "OK" shows at the start of a screen row, or for ten
 * guest seconds. True if it showed. */
bool guest_boot(guest_t *g);

/* Fields, each after a keymatrix_field on a CoCo. */
void guest_fields(guest_t *g, int n);

/* Type a string. Each byte is the PicoCalc events that type it
 * (keymap_picocalc_text), letters in lower case, which types the same
 * capitals with fewer events (§11.3), each byte's events given once the
 * last byte's have all reached the matrix. Then a few
 * fields for the ROM to act. */
void guest_type(guest_t *g, const char *s);

/* One PicoCalc key pressed and released, inside Shift or Alt, on a CoCo;
 * then fields until it has reached the matrix and gone. */
void guest_press(guest_t *g, uint8_t code, bool shift, bool alt);

/* Fields until the held-key set is idle, up to `max`; true if it is. */
bool guest_settle(guest_t *g, int max);

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
