/* keymapio.h — the game layouts the menu offers: the built-in ones, the
 * keyboard joystick as either stick, then the .map files in /coco/keymaps/
 * on the card (design.md §11.3, §11.4, §14). Pico-oric's, renamed.
 *
 * Core 1 only. The card's layouts are read at boot and again each time
 * the menu opens, with the card mounted (storage.h) and core 0 parked or
 * not yet started. A rescan rewrites the card's layouts in place, which
 * is safe because core 0 takes the layout again after every park, and a
 * key already down holds a copy of its binding (keymatrix.h). A file
 * that does not parse is left out, and the first such file and line is
 * kept for the menu's status row.
 */
#ifndef PICO_COCO_KEYMAPIO_H
#define PICO_COCO_KEYMAPIO_H

#include "keymatrix.h"

#define KEYMAPIO_DIR COCO_KEYMAP_DIR

void keymapio_scan(void);

/* Built-in layouts first, then the card's, in directory order. */
unsigned           keymapio_count(void);
const keylayout_t *keymapio_get(unsigned i);

/* The index of the layout with this name, or -1. */
int keymapio_find(const char *name);

/* The first layout whose tapes or carts line names this file, or NULL. */
const keylayout_t *keymapio_for_file(const char *path);

/* The layout in force, in g_ui.layout for core 0 (handoff.h). Chosen by
 * the user, in the menu or the settings file, or selected by loading a
 * file a layout's tapes or carts line names, which the menu says and a
 * save of the settings does not keep (§11.3). A rescan finds the one in
 * force again by name, or falls back to the standard map if its file has
 * gone. */
void        keymapio_choose(const keylayout_t *l);
void        keymapio_file_loaded(const char *path);
const char *keymapio_chosen_by(void);   /* "" when the user chose it */

/* "" when every file parsed; else, e.g., "fire.map 3: no such CoCo key". */
const char *keymapio_error(void);

#endif /* PICO_COCO_KEYMAPIO_H */
