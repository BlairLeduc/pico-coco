/* drivesnd.h — the disk drives' sounds: the head's steps and the motor
 * (design.md §10).
 *
 * Synthesised from what was measured in recordings of a drive
 * (tools/drivesnd-coeffs.py): a step is a bank of resonators at the
 * frequencies a real step rings at, struck, and struck again more softly
 * as the head settles, with a burst of noise on the strike and the
 * drive's noise swelling after it; the motor is noise shaped to the
 * drive's hump at 1 kHz, wandering in level, with its two faint tones,
 * swelling when the motor starts and fading when it stops. Or recorded: a step played from its start at each pulse, cutting
 * short the last if it still sounds, and the motor's loop played round
 * while the motor turns, swelling and fading as the synthesised one
 * does (drivesnd_samples_t). The recordings are the user's, on the card
 * (COCO_DRIVESND_DIR), as the ROMs are: none is in the tree. The CoCo itself makes no such sound: the drives do,
 * and this only puts them in the same speaker. Nothing of it reaches the
 * guest, and with it off the PCM is what it was without it, sample for
 * sample.
 *
 * The voice lives in the PCM (pcm.h), which adds a sample of it to each
 * sample of the guest's sound, after the DC blocker. Its events come
 * from the RS-DOS cartridge (rsdos.c): the WD1793's step pulses, a train
 * of them a step's time apart from the tick its command was written,
 * and the latch's motor bit. Each is handed over with the PCM brought
 * up to the moment first, so a click is struck in the sample its pulse
 * falls in, and a train is never cut short by the next command.
 *
 * Times are the low 32 bits of the machine's tick count, as the PCM's
 * are; a train never runs past 255 steps of 30 ms, far short of the wrap.
 */
#ifndef PICO_COCO_DRIVESND_H
#define PICO_COCO_DRIVESND_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

/* drivesnd_coeffs.h's DRIVESND_MODES, at most. */
#define DRIVESND_MODES_MAX 12u

/* Recorded sounds, each played at `inc`, its samples per output sample
 * in Q16 (drivesnd_inc); a length of 0 leaves that one synthesised. The
 * samples are the port's, outside the machine, so a copy of the machine
 * points at the same ones. */
typedef struct {
    const int16_t *step;
    uint32_t       step_len, step_inc;
    const int16_t *motor;
    uint32_t       motor_len, motor_inc;
} drivesnd_samples_t;

/* A recording's step at `rate` Hz, for the PCM's nominal rate. */
uint32_t drivesnd_inc(uint32_t rate);

typedef struct {
    bool     on;            /* the setting: off, it is silent and idle   */
    const drivesnd_samples_t *rec;   /* recorded sounds, or NULL         */
    uint32_t step_pos;      /* where the step plays, Q16; past its end, none */
    uint32_t motor_pos;     /* where the motor's loop is, Q16            */
    bool     motor;         /* the latch's motor bit                     */

    /* The step pulses still to come: `left` of them, the next at tick
     * `next`, then every `period`. */
    uint32_t next, period;
    uint32_t left;

    /* A step: its modes' last two samples, the samples until they are
     * rung out, until the head's second strike, and the strike's burst
     * and the noise's swell now. */
    int32_t  y1[DRIVESND_MODES_MAX], y2[DRIVESND_MODES_MAX];   /* 8 bits more */
    uint32_t ring_left, bounce_in;
    int32_t  burst, tail;
    /* The motor: its level and its wandering, Q15, the wandering's aim,
     * the noise's band pass, and the tones' phases. */
    int32_t  spin, flutter, flutter_to;
    int32_t  h1, h2, x1, lp;
    uint32_t phase[2];
    uint32_t n;             /* samples made, for the wandering's beat    */
    uint32_t rng;

    uint32_t clicks;        /* steps struck, for the tests               */
} drivesnd_t;

/* Silent, off, the motor stopped. */
void drivesnd_init(drivesnd_t *d);

/* On or off. Turned off, everything sounding stops at once. */
void drivesnd_enable(drivesnd_t *d, bool on);

/* Nothing sounding and nothing to come: a state restored, or the
 * machine powered on. The setting and the motor bit stay. */
void drivesnd_quiet(drivesnd_t *d);

/* The recorded sounds, or NULL for the synthesised ones. What sounds
 * stops. */
void drivesnd_set_samples(drivesnd_t *d, const drivesnd_samples_t *rec);

/* `n` step pulses, the first at tick `from`, then every `period` ticks.
 * Any of an earlier train still to come is dropped. */
void drivesnd_steps(drivesnd_t *d, uint32_t from, uint32_t n, uint32_t period);

/* The motor bit, from the sample now being made. */
void drivesnd_motor(drivesnd_t *d, bool on);

/* The voice's sample for the one beginning at tick t. Samples are asked
 * for in order, one each. */
int32_t drivesnd_next(drivesnd_t *d, uint32_t t);

/* Whether a sample would be anything but 0: the PCM's quick test. */
static inline bool drivesnd_sounding(const drivesnd_t *d) {
    return d->on && (d->left || d->ring_left || d->bounce_in || d->burst || d->tail || d->spin ||
                     d->motor ||
                     (d->rec && (d->step_pos >> 16) < d->rec->step_len));
}

#endif /* PICO_COCO_DRIVESND_H */
