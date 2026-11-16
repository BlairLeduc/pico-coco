/* core0.h — core 0's loop: the guest (design.md §6, §11.1, §13). */
#ifndef PICO_COCO_CORE0_H
#define PICO_COCO_CORE0_H

#include "coco.h"
#include "handoff.h"
#include "keymatrix.h"

/* The machine powered on as mc (design.md §13.3, §14): the ROMs and the
 * cartridge from g_boot loaded, then power on; the deck's tape back in
 * (tapeio.h), and the PCM at the PWM's real rate. The boot and the
 * Machine page's Apply both come here. */
void core0_power_on(coco_t *m, const machine_t *mc);

/* Run the machine a field at a time for ever: the park between fields
 * (park.h), keys, the field, the snapshot. Paced on the audio queue, or
 * on time_us_64() against an absolute field deadline (EL §6.3). Core 1
 * must be up: from here on core 0 logs only through the ring. */
void core0_run(coco_t *m, keymatrix_t *k) __attribute__((noreturn));

#endif /* PICO_COCO_CORE0_H */
