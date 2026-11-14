/* bus.h — guest memory dispatch (design.md §8.1, EL §4.1).
 *
 * The fast path is one indexed load or store through the 256-entry page
 * table; everything else falls into bus_read_slow and bus_write_slow. In
 * M1 those are the caller's (coco.h); bus.c brings the machine's own in
 * M3, with what unpopulated memory and unused I/O read, which is not yet
 * settled and is not assumed to be $FF (§18).
 */
#ifndef PICO_COCO_BUS_H
#define PICO_COCO_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "coco.h"

uint8_t bus_read_slow(coco_t *m, uint16_t a);
void    bus_write_slow(coco_t *m, uint16_t a, uint8_t v);

/* M3: the cycle within the instruction on which a slow-path access falls,
 * so a PIA can be brought up to it first (§7.2, EL §4.3). */

static inline uint8_t bus_read(coco_t *m, uint16_t a) {
    const page_t *p = &m->page[a >> 8];
    if (__builtin_expect(p->read != NULL, 1)) return p->read[a & 0xFFu];
    return bus_read_slow(m, a);
}

static inline void bus_write(coco_t *m, uint16_t a, uint8_t v) {
    page_t *p = &m->page[a >> 8];
    if (__builtin_expect(p->write != NULL, 1)) p->write[a & 0xFFu] = v;
    else                                       bus_write_slow(m, a, v);
}

#endif /* PICO_COCO_BUS_H */
