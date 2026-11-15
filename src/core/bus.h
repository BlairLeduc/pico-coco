/* bus.h — guest memory dispatch (design.md §8.1, EL §4.1).
 *
 * The fast path is one indexed load or store through the 256-entry page
 * table; everything else falls into bus_read_slow and bus_write_slow:
 * bus.c's, which decode the machine (coco.c), or a CPU-only test's or the
 * bench's own (coco.h).
 */
#ifndef PICO_COCO_BUS_H
#define PICO_COCO_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "coco.h"

uint8_t bus_read_slow(coco_t *m, uint16_t a);
void    bus_write_slow(coco_t *m, uint16_t a, uint8_t v);

/* `at` is the cycle of the instruction the access falls on, from 1, or 0
 * outside an instruction's data (mc6809_t.io_at). Only the slow path
 * keeps it, so RAM pays nothing (§7.2, EL §4.3). */
static inline uint8_t bus_read_at(coco_t *m, uint16_t a, uint32_t at) {
    const page_t *p = &m->page[a >> 8];
    if (__builtin_expect(p->read != NULL, 1)) return p->read[a & 0xFFu];
    m->cpu.io_at = (uint8_t)at;
    return bus_read_slow(m, a);
}

static inline void bus_write_at(coco_t *m, uint16_t a, uint8_t v, uint32_t at) {
    page_t *p = &m->page[a >> 8];
    if (__builtin_expect(p->write != NULL, 1)) {
        p->write[a & 0xFFu] = v;
    } else {
        m->cpu.io_at = (uint8_t)at;
        bus_write_slow(m, a, v);
    }
}

static inline uint8_t bus_read(coco_t *m, uint16_t a) {
    return bus_read_at(m, a, 0);
}

static inline void bus_write(coco_t *m, uint16_t a, uint8_t v) {
    bus_write_at(m, a, v, 0);
}

#endif /* PICO_COCO_BUS_H */
