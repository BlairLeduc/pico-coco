/* drivesnd.c — the disk drives' sounds (scope in drivesnd.h). */

#include "drivesnd.h"

#include <string.h>

#include "drivesnd_coeffs.h"
#include "hot.h"

_Static_assert(DRIVESND_MODES <= DRIVESND_MODES_MAX, "the modes fit");

/* Milliseconds in samples at the nominal rate. */
#define MS_SAMPLES(ms) ((uint32_t)((uint64_t)(ms) * COCO_AUDIO_RATE_NUM / COCO_AUDIO_RATE_DEN / 1000u))

/* No step playing: past the end of any. */
#define NO_STEP UINT32_MAX

uint32_t drivesnd_inc(uint32_t rate) {
    /* rate / (NUM / DEN) in Q16, rounded. */
    uint64_t num = (uint64_t)rate * COCO_AUDIO_RATE_DEN << 16;
    return (uint32_t)((num + COCO_AUDIO_RATE_NUM / 2u) / COCO_AUDIO_RATE_NUM);
}

static void silence_step(drivesnd_t *d) {
    memset(d->y1, 0, sizeof d->y1);
    memset(d->y2, 0, sizeof d->y2);
    d->ring_left = d->bounce_in = 0;
    d->burst = d->tail = 0;
}

void drivesnd_init(drivesnd_t *d) {
    memset(d, 0, sizeof(*d));
    d->rng = 0x2545F491u;
    d->step_pos = NO_STEP;
    d->flutter = d->flutter_to = 32768;
}

void drivesnd_set_samples(drivesnd_t *d, const drivesnd_samples_t *rec) {
    if (rec == d->rec) return;
    d->rec = rec;
    d->step_pos = NO_STEP;
    d->motor_pos = 0;
    silence_step(d);
}

void drivesnd_quiet(drivesnd_t *d) {
    d->left = 0;
    d->step_pos = NO_STEP;
    silence_step(d);
    d->spin = d->motor ? 32767 : 0;   /* a drive restored already turning */
    d->h1 = d->h2 = d->x1 = d->lp = 0;
}

void drivesnd_enable(drivesnd_t *d, bool on) {
    if (on == d->on) return;
    d->on = on;
    d->left = 0;
    d->step_pos = NO_STEP;
    silence_step(d);
    d->spin = d->h1 = d->h2 = d->x1 = d->lp = 0;
}

void drivesnd_steps(drivesnd_t *d, uint32_t from, uint32_t n, uint32_t period) {
    if (!d->on) return;
    d->next = from;
    d->period = period;
    d->left = n;
}

void drivesnd_motor(drivesnd_t *d, bool on) {
    d->motor = on;
}

/* The modes' state carries 8 bits under the output's, or truncation
 * leaves each humming at a few tens until it is cleared. */
#define MODE_BITS 8

/* The synthesised step's modes struck at `level`, the sum of their peaks. */
static void strike(drivesnd_t *d, int32_t level) {
    for (unsigned i = 0; i < DRIVESND_MODES; i++)
        d->y1[i] += (int32_t)((int64_t)drivesnd_modes[i].strike * level >> (15 - MODE_BITS));
    d->ring_left = MS_SAMPLES(COCO_DRIVESND_RING_MS);
}

static uint32_t rand32(drivesnd_t *d) {
    d->rng = d->rng * 1664525u + 1013904223u;   /* Numerical Recipes' LCG */
    return d->rng;
}

/* sin(2 pi p / 2^32), Q15, from the quarter table. */
static int32_t sine(uint32_t p) {
    uint32_t i = (p >> 24) & 63u;
    switch (p >> 30) {
    case 0:  return drivesnd_sine_q[i];
    case 1:  return drivesnd_sine_q[64u - i];
    case 2:  return -drivesnd_sine_q[i];
    default: return -drivesnd_sine_q[64u - i];
    }
}

int32_t COCO_HOT1(drivesnd_next)(drivesnd_t *d, uint32_t t) {
    const drivesnd_samples_t *rec = d->rec;
    bool recorded_step = rec && rec->step_len;
    if (d->left && (int32_t)(t - d->next) >= 0) {
        d->left--;
        d->next += d->period;
        d->clicks++;
        if (recorded_step) {
            d->step_pos = 0;
        } else {
            strike(d, COCO_DRIVESND_STEP);
            d->bounce_in = MS_SAMPLES(COCO_DRIVESND_BOUNCE_MS);
            d->burst = COCO_DRIVESND_BURST;
            d->tail = COCO_DRIVESND_TAIL;
        }
    }

    /* White noise, the LCG's high bits, the good ones: RMS about 18,900. */
    int32_t noise = (int32_t)(rand32(d) >> 16) - 32768;
    int32_t out = 0;

    /* The recorded step. */
    if (rec && (d->step_pos >> 16) < rec->step_len) {
        out += rec->step[d->step_pos >> 16];
        d->step_pos += rec->step_inc;
        if ((d->step_pos >> 16) >= rec->step_len) d->step_pos = NO_STEP;
    }

    /* The synthesised step: the head settling, the modes, the burst. */
    if (d->bounce_in && --d->bounce_in == 0) strike(d, COCO_DRIVESND_STEP / 2);
    if (d->ring_left) {
        int32_t sum = 0;
        for (unsigned i = 0; i < DRIVESND_MODES; i++) {
            int32_t y = (int32_t)(((int64_t)d->y1[i] * drivesnd_modes[i].c1 -
                                   (int64_t)d->y2[i] * drivesnd_modes[i].c2) >> 14);
            d->y2[i] = d->y1[i];
            d->y1[i] = y;
            sum += y;
        }
        out += sum >> MODE_BITS;
        if (--d->ring_left == 0) {   /* rung out: what truncation would leave humming */
            memset(d->y1, 0, sizeof d->y1);
            memset(d->y2, 0, sizeof d->y2);
        }
    }
    if (d->burst) {
        out += (noise * d->burst) >> 15;
        d->burst = (d->burst * COCO_DRIVESND_BURST_DECAY) >> 15;   /* reaches 0 */
    }

    /* The motor's level, swelling and fading. */
    if (d->motor && d->spin < 32767) {
        int32_t up = (32767 - d->spin) >> COCO_DRIVESND_SPIN_SHIFT;
        d->spin += up ? up : 1;
    } else if (!d->motor && d->spin) {
        d->spin += (0 - d->spin) >> COCO_DRIVESND_SPIN_SHIFT;   /* rounds down to 0 */
    }

    if (d->spin && rec && rec->motor_len) {
        /* The recorded loop, round and round, at the motor's level. */
        int32_t v = rec->motor[d->motor_pos >> 16];
        d->motor_pos += rec->motor_inc;
        if ((d->motor_pos >> 16) >= rec->motor_len) d->motor_pos -= rec->motor_len << 16;
        out += (int32_t)((int64_t)v * d->spin >> 15);
    } else if (d->spin || d->tail) {
        /* The drive's noise: white through the hump, at the motor's
         * wandering level and the step's swell. */
        if ((d->n++ & ((1u << COCO_DRIVESND_FLUTTER_SHIFT) - 1u)) == 0)
            d->flutter_to = 24576 + (int32_t)(rand32(d) >> 17);   /* 0.75 to 1.25 */
        d->flutter += (d->flutter_to - d->flutter) >> 9;
        int32_t x = (int32_t)((int64_t)(noise - ((d->x1 * DRIVESND_HUMP_ZERO) >> 15)) *
                              DRIVESND_HUMP_IN >> 15);
        d->x1 = noise;
        int32_t y = (int32_t)(((int64_t)d->h1 * DRIVESND_HUMP_C1 -
                               (int64_t)d->h2 * DRIVESND_HUMP_C2) >> 14) + x;
        d->h2 = d->h1;
        d->h1 = y;
        d->lp += (int32_t)((int64_t)(y - d->lp) * DRIVESND_HUMP_LP >> 15);
        int32_t h = d->lp;
        int32_t motor = (int32_t)((int64_t)d->spin * d->flutter >> 15);
        int32_t g = motor + d->tail;                                  /* Q15 of NOISE */
        out += (int32_t)((int64_t)h * g / 32768 * COCO_DRIVESND_NOISE / 18900);
        /* The tones, at the motor's level alone. */
        for (unsigned i = 0; i < DRIVESND_TONES; i++) {
            d->phase[i] += drivesnd_tones[i].step;
            int32_t amp = (int32_t)((int64_t)drivesnd_tones[i].level * COCO_DRIVESND_NOISE * 46341 >> 30);
            out += (int32_t)((int64_t)sine(d->phase[i]) * amp >> 15) * motor >> 15;
        }
    } else {
        d->h1 = d->h2 = d->x1 = d->lp = 0;
    }
    if (d->tail) {
        d->tail = (d->tail * COCO_DRIVESND_TAIL_DECAY) >> 15;
        if (d->tail < 1024) d->tail = 0;   /* -30 dB, about 70 ms: under the motor's noise */
    }
    return out;
}
