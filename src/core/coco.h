/* coco.h — the machine (design.md §6, §8).
 *
 * M1 has only the CPU and the page table it reaches memory through. The
 * SAM, the PIAs, RAM, the ROMs, the configuration and the seam of §6
 * (coco_init, coco_power_on, coco_run, coco_copy) arrive in M3, and with
 * them bus.c's slow path. Until then whoever links the CPU supplies
 * bus_read_slow and bus_write_slow: the host tests and, in M2, the bench.
 */
#ifndef PICO_COCO_COCO_H
#define PICO_COCO_COCO_H

#include <stdint.h>

#include "config.h"
#include "mc6809.h"

/* Exactly two pointers, as the siblings' (EL §4.1). A NULL read pointer
 * sends the access to the slow path; a NULL write pointer sends a write
 * there, which is how ROM and I/O are told from RAM. */
typedef struct {
    const uint8_t *read;
    uint8_t       *write;
} page_t;

typedef struct coco_s {
    mc6809_t cpu;
    page_t   page[COCO_PAGE_COUNT];
    /* M3: the SAM, the PIAs, RAM and ROM, the configuration (§8). */
} coco_t;

#endif /* PICO_COCO_COCO_H */
