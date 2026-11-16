/* roms.h — which BASIC the boot starts, and the page shown when it
 * cannot (design.md §12.1, §14).
 *
 * The CoCo boots Extended Color BASIC 1.1 over Color BASIC 1.3 in 64 KB
 * (D6), its two ROMs found on the card by SHA-1 (D10). Without them, a
 * page names what is missing, never a blank screen; if the card has a
 * Color BASIC alone, 1.3 or else 1.1, the page offers it on ENTER. The
 * Machine page that chooses BASIC and RAM is M9's.
 *
 * No SDK header: tools/page-ppm.c renders the pages on the host, so
 * they can be looked at before the board shows them. Adapted from
 * pico-oric's roms.c.
 */
#ifndef PICO_COCO_ROMS_H
#define PICO_COCO_ROMS_H

#include <stdbool.h>

#include "card.h"
#include "coco.h"

typedef enum {
    BOOT_NONE,         /* nothing on the card starts a CoCo          */
    BOOT_EXTENDED,     /* Extended Color BASIC 1.1 over 1.3: the one wanted */
    BOOT_COLOR13,      /* Color BASIC 1.3 alone                      */
    BOOT_COLOR11,      /* Color BASIC 1.1 alone                      */
} boot_basic_t;

/* What the job's ROMs can start: BOOT_EXTENDED if both are loaded, and
 * otherwise the best a Color BASIC alone gives. */
boot_basic_t roms_choice(const card_job_t *job);

/* "EXTENDED COLOR BASIC 1.1", "COLOR BASIC 1.3", ... */
const char *roms_basic_name(boot_basic_t b);

/* The page for a card that does not start Extended Color BASIC: what is
 * missing, and the alternative ENTER starts when there is one. */
void roms_page(const card_job_t *job, coco_frame_t *f);

#endif /* PICO_COCO_ROMS_H */
