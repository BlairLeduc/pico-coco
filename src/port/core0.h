/* core0.h — core 0's loop: the guest (design.md §6, §11.1, §13). */
#ifndef PICO_COCO_CORE0_H
#define PICO_COCO_CORE0_H

#include "coco.h"
#include "keymatrix.h"

/* Run the machine a field at a time for ever: keys first, then the
 * field, then the snapshot. Until M8 brings audio, pace on time_us_64()
 * against an absolute field deadline, so that a late field does not
 * accumulate (EL §6.3). Core 1 must be up: from here on core 0 logs only
 * through the ring. */
void core0_run(coco_t *m, keymatrix_t *k) __attribute__((noreturn));

#endif /* PICO_COCO_CORE0_H */
