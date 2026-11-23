/* soundio.h — the drives' recorded sounds off the card (design.md §10,
 * drivesnd.h).
 *
 * Core 1, in the boot's card job, with core 0 not yet started: step.wav
 * and motor.wav from COCO_DRIVESND_DIR, read through wav.h into buffers
 * here, each cut at its COCO_DRIVESND_*_MAX. A file that is not there
 * leaves that sound synthesised; one that is there and cannot be read is
 * logged and left synthesised too. Core 0 takes soundio_samples() for
 * the machine each field, after the boot has handed over (handoff.h), so
 * the buffers never change under it.
 */
#ifndef PICO_COCO_SOUNDIO_H
#define PICO_COCO_SOUNDIO_H

#include "drivesnd.h"

/* The card is mounted. */
void soundio_load(void);

/* The recordings, or NULL if the card had neither. */
const drivesnd_samples_t *soundio_samples(void);

#endif /* PICO_COCO_SOUNDIO_H */
