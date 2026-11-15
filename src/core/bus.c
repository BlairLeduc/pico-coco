/* bus.c — the slow path, for the machine (design.md §8.1).
 *
 * Only these two functions, so that a test or the bench that supplies its
 * own never pulls this file in (coco.h). The decode is coco.c's.
 */

#include "bus.h"

#include "hot.h"

uint8_t COCO_HOT1(bus_read_slow)(coco_t *m, uint16_t a) {
    return coco_slow_read(m, a);
}

void COCO_HOT1(bus_write_slow)(coco_t *m, uint16_t a, uint8_t v) {
    coco_slow_write(m, a, v);
}
