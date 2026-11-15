/* core1.h — core 1's loop: the panel, the southbridge, the card and the
 * log (design.md §6).
 */
#ifndef PICO_COCO_CORE1_H
#define PICO_COCO_CORE1_H

/* Bring up the southbridge, the LCD and the card in hardware-notes.md
 * §10's order, set g_c1.ready, then poll the keyboard, watch the card's
 * slot and drain core 0's log for ever. Launched with
 * multicore_launch_core1. */
void core1_main(void);

#endif /* PICO_COCO_CORE1_H */
