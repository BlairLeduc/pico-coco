/* pcm.h — a level in master-oscillator ticks to PCM: the box filter
 * (design.md §10; EL §6.1).
 *
 * pico-oric's, which was pico-ace's beeper generalised from a speaker bit
 * to any level; here the level is the CoCo's sound bus, the DAC, the
 * single bit and the multiplexer as coco.c computes it. A sample is the
 * time average of the level over the ticks it covers, a box filter at
 * exactly the sample period, not a point sample of it, which aliases
 * audibly. The cost is per change of level plus a short loop per sample.
 *
 * Time is kept as a rational, never a truncated rate (EL §6.1, HW §5.2):
 * ticks per sample is num/den, 91,636,352/234,375 at 14.31818 MHz and
 * 150 MHz / 4,096, and the sample boundaries are tracked in units of 1/den
 * of a tick, so they land where they should for ever rather than
 * drifting.
 *
 * Times are the low 32 bits of the machine's tick count, which wraps at
 * 2^32 (about 300 s). Only differences from the current sample's start
 * are taken, never more than a field's worth, so the wrap does no harm.
 *
 * Output is signed 16-bit mono through a one-pole DC blocker, so the
 * bus's unipolar level comes to rest at 0, which is the silence value
 * the port pads an underrun with.
 *
 * Beside it, a side channel for stereo (design.md §12.10): a signed
 * level, half the difference of the two ears, box-filtered as the mono
 * is. Each ear is made from the two at once, the left the mono plus the
 * side and the right the mono less it, rounded once and DC-blocked on
 * its own, and drained as pairs. It costs nothing until a side level
 * other than 0 is first set (an Orchestra-90's DACs apart); until then
 * both ears are the mono.
 */
#ifndef PICO_COCO_PCM_H
#define PICO_COCO_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "drivesnd.h"

/* The level `level_max` held for a whole sample, before the DC blocker.
 * The blocker's output for an input in [0, M] stays in [-M, M], so this
 * cannot wrap int16: headroom by construction. */
#define PCM_FULL_SCALE 32767

typedef struct {
    /* Ticks per sample = num / den = q + r / den. */
    uint32_t num, den, q, r;
    uint32_t level_max;
    uint64_t scale;          /* (FULL_SCALE << 48) / (num * level_max)   */

    /* The current sample began at tick `start` plus start_frac / den. */
    uint32_t start;
    uint32_t start_frac;
    uint32_t pos;            /* units (1/den tick)  of it accounted for   */
    uint64_t acc;            /* level x units over them                   */
    uint32_t level;

    bool     dc_block;       /* on by default; tests turn it off          */
    int32_t  hp_x;           /* last input                                */
    int32_t  hp_y;           /* last output, Q8                           */

    /* The disk drives' voice, added to each sample after the blocker
     * (drivesnd.h); off, nothing is added. pcm_init leaves it off. */
    drivesnd_t drive;

    /* The side channel (above): latched on by its first non-zero level. */
    bool     side_on;
    int32_t  side;
    int64_t  side_acc;       /* side x units over the current sample      */
    int32_t  hl_x, hl_y;     /* the left ear's DC blocker, as hp_x, hp_y  */
    int32_t  hr_x, hr_y;     /* the right's                               */

    int16_t  buf[COCO_AUDIO_BUF_LEN];
    int16_t  left[COCO_AUDIO_BUF_LEN], right[COCO_AUDIO_BUF_LEN];
    uint32_t count;
    uint32_t overflow;       /* samples lost because nobody drained       */
} pcm_t;

/* Start at tick `now` with the level at 0, for levels up to level_max.
 * The sample rate is rate_num / rate_den Hz: on the device clk_sys over
 * divider x (TOP + 1) x oversample, passed as that fraction so nothing
 * is rounded. Zero in any rate argument means the nominal
 * COCO_AUDIO_RATE at COCO_TICK_HZ. */
void pcm_init(pcm_t *p, uint32_t now, uint32_t level_max, uint32_t tick_hz,
              uint32_t rate_num, uint32_t rate_den);

/* A new rate, from a sample starting at `now`, as pcm_init takes it.
 * Everything else is kept: the level, the DC blocker's state, and the
 * samples not yet drained, in place (no copy, and nothing on the stack:
 * the device's main stack is 2 KiB). */
void pcm_set_rate(pcm_t *p, uint32_t now, uint32_t level_max, uint32_t tick_hz,
                  uint32_t rate_num, uint32_t rate_den);

/* The guest's clock jumped (a power-on, or from M10 a snapshot restored):
 * the next sample starts at `now`. The rate, the level, the DC blocker's
 * state and the samples not yet drained are kept. */
void pcm_restart(pcm_t *p, uint32_t now);

/* Emit every sample that ends at or before `now`, at the current level. */
void pcm_advance(pcm_t *p, uint32_t now);

/* The level changes to `level` at `now`. */
static inline void pcm_set_level(pcm_t *p, uint32_t now, uint32_t level) {
    if (level == p->level) return;
    pcm_advance(p, now);
    p->level = level;
}

/* The side level changes to `side` at `now`: half the difference of the
 * ears, in the levels' units, |side| at most level_max / 2. */
static inline void pcm_set_side(pcm_t *p, uint32_t now, int32_t side) {
    if (side == p->side) return;
    pcm_advance(p, now);
    p->side = side;
    if (side && !p->side_on) {
        /* The ears' blockers take up where the mono's is, so that the
         * first difference makes no step. */
        p->side_on = true;
        p->hl_x = p->hr_x = p->hp_x;
        p->hl_y = p->hr_y = p->hp_y;
    }
}

/* Move up to `max` samples out, oldest first. Returns how many. */
size_t pcm_drain(pcm_t *p, int16_t *dst, size_t max);

/* The same as stereo pairs, left then right: up to `max` pairs into dst,
 * 2 x max values. Returns how many pairs. */
size_t pcm_drain_stereo(pcm_t *p, int16_t *dst, size_t max);

#endif /* PICO_COCO_PCM_H */
