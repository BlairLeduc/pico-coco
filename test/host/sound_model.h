/* sound_model.h — the CoCo's sound bus, modelled from the pins
 * (design.md §10, §17 M8).
 *
 * The reference for test_audio and test_audio_rom. A step hook logs the
 * pins of both PIAs that reach the bus after every instruction that moved
 * one, stamped with the tick at that instruction's start (EL §6.1); the
 * model then works out the bus's level from each record, in floating
 * point and volts as XRoar's sound.c does, the pins read through XRoar's
 * PIA (its out_source and out_sink), and box-filters it with exact
 * integer boundaries at the pcm's rational sample period. Written from
 * XRoar's sound.c, dragon.c and mc6821.c, not from coco.c, which works in
 * integers and through mc6821.h; the two are to agree on every sample to
 * 1 LSB with the DC blocker off.
 */
#ifndef PICO_COCO_TEST_SOUND_MODEL_H
#define PICO_COCO_TEST_SOUND_MODEL_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "coco.h"
#include "pcm.h"

/* The pins that reach the bus: PIA1 port A (the DAC) and port B (the
 * single bit), PIA1 CRB (CB2, the multiplexer's enable), PIA0 CRA and
 * CRB (CA2 and CB2, its source). */
typedef struct {
    uint64_t at;
    uint8_t  pa_out, pa_ddr, pb_out, pb_ddr, cr1b, cr0a, cr0b;
    /* An Orchestra-90's DACs, beside the bus (§12.10); zero without one. */
    uint8_t  orch_l, orch_r;
} snd_rec_t;

#define SND_MAX_RECS 400000u
#define SND_MAX_SAMPLES 4000000u

static snd_rec_t snd_recs[SND_MAX_RECS];
static size_t    snd_n;
static snd_rec_t snd_last;
static uint64_t  snd_insn_start;   /* the tick the next step starts at */
static size_t    snd_lost;

static void snd_pins(const coco_t *m, snd_rec_t *r) {
    r->pa_out = m->pia1.a.out;
    r->pa_ddr = m->pia1.a.ddr;
    r->pb_out = m->pia1.b.out;
    r->pb_ddr = m->pia1.b.ddr;
    /* The control registers without their flags, which the field's
     * interrupts move and nothing in the bus reads. */
    r->cr1b = m->pia1.b.cr & 0x3Fu;
    r->cr0a = m->pia0.a.cr & 0x3Fu;
    r->cr0b = m->pia0.b.cr & 0x3Fu;
    r->orch_l = r->orch_r = 0;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        if (m->port.slot[s].kind != CART_ORCH90) continue;
        r->orch_l = m->port.slot[s].u.orch.left;
        r->orch_r = m->port.slot[s].u.orch.right;
    }
}

static bool snd_same(const snd_rec_t *a, const snd_rec_t *b) {
    return a->pa_out == b->pa_out && a->pa_ddr == b->pa_ddr && a->pb_out == b->pb_out &&
           a->pb_ddr == b->pb_ddr && a->cr1b == b->cr1b && a->cr0a == b->cr0a &&
           a->cr0b == b->cr0b && a->orch_l == b->orch_l && a->orch_r == b->orch_r;
}

/* coco_t.step_hook: after each step, a record if the pins moved. */
static void snd_hook(coco_t *m) {
    snd_rec_t r;
    snd_pins(m, &r);
    if (!snd_same(&r, &snd_last)) {
        r.at = snd_insn_start;
        if (snd_n < SND_MAX_RECS) snd_recs[snd_n++] = r;
        else snd_lost++;
        snd_last = r;
    }
    snd_insn_start = coco_ticks(m);
}

/* Start logging from a machine just powered on: its pins now, at tick 0. */
static void snd_log_start(coco_t *m) {
    snd_n = 0;
    snd_lost = 0;
    snd_pins(m, &snd_last);
    snd_last.at = coco_ticks(m);
    snd_recs[snd_n++] = snd_last;
    snd_insn_start = coco_ticks(m);
    m->step_hook = snd_hook;
}

/* ---- the bus, as XRoar's sound.c works it out ------------------------- */

#define SND_MAX_V 4.70

/* By source (DAC, tape, cartridge, the fourth) and by the single bit:
 * disabled, enabled low, enabled high. Volts. */
static const double snd_gain_v[4][3] = {
    { 4.50, 2.84, 3.40 }, { 0.50, 0.40, 0.50 }, { 4.70, 2.84, 3.40 }, { 4.70, 2.84, 3.40 },
};
static const double snd_offset_v[4][3] = {
    { 0.20, 0.18, 1.30 }, { 2.05, 1.60, 2.35 }, { 0.00, 0.18, 1.30 }, { 0.00, 0.18, 1.30 },
};
static const double snd_sbs_offset_v[3] = { 0.00, 0.00, 3.90 };

/* Cx2 through XRoar's PIA: an output set by hand is its bit 3; an output
 * in a pulse mode starts high; an input is high on the A side, which
 * has pull-ups (cx2_out_sink & cx2_in_sink), and low on the B side
 * ((cx2_out_source | cx2_in_source) & ..., both sources 0). */
static bool snd_cx2(uint8_t cr, bool side_a) {
    if (cr & 0x20u) return (cr & 0x10u) ? (cr & 0x08u) != 0 : true;
    return side_a;
}

/* The bus after the record, normalised to 4.70 V, from the level before
 * it (which it holds when nothing drives it). */
static double snd_level(const snd_rec_t *r, double held) {
    uint8_t a_sink = (uint8_t)~(~r->pa_out & r->pa_ddr);            /* port A, pulled up */
    uint8_t b_source = (uint8_t)(r->pb_out & r->pb_ddr);
    uint8_t b_sink = (uint8_t)(r->pb_out | ~r->pb_ddr);
    bool sbs_enabled = !((b_source ^ b_sink) & 0x02u);
    bool sbs_level = (b_source & b_sink & 0x02u) != 0;
    unsigned s = sbs_enabled ? (sbs_level ? 2u : 1u) : 0u;
    bool mux = snd_cx2(r->cr1b, false);
    if (mux) {
        unsigned src = (snd_cx2(r->cr0b, false) ? 2u : 0u) | (snd_cx2(r->cr0a, true) ? 1u : 0u);
        double in = 0.0;
        if (src == 0) in = (double)(a_sink & 0xFCu) / 252.0;
        else if (src == 1) in = 0.5;   /* XRoar's tape with nothing in it */
        return (in * snd_gain_v[src][s] + snd_offset_v[src][s]) / SND_MAX_V;
    }
    if (sbs_enabled) return snd_sbs_offset_v[s] / SND_MAX_V;
    return held;
}

/* Which channel snd_model_render makes: 0 mono, 1 the left ear, 2 the
 * right. */
static int snd_channel;

/* What is heard: the bus and, beside it, an Orchestra-90's two DACs as
 * XRoar's orch90.c gives them to its mono output (each D / 255 of the
 * full scale, summed), here at half, so that both at 255 reach the full
 * scale, and clipped there (§12.10). In stereo, as the real cartridge's
 * own outputs have them: the mono and half the DACs' difference, the
 * left ear plus it and the right less it, so that each ear hears the bus
 * and its own DAC at full scale. */
static double snd_heard(const snd_rec_t *r, double bus) {
    double v = bus + ((double)r->orch_l / 255.0 + (double)r->orch_r / 255.0) / 2.0;
    if (v > 1.0) v = 1.0;
    double side = ((double)r->orch_l - (double)r->orch_r) / 255.0 / 2.0;
    return snd_channel == 1 ? v + side : snd_channel == 2 ? v - side : v;
}

/* ---- the box filter, exactly ------------------------------------------ */

static int16_t snd_model_out[SND_MAX_SAMPLES];

/* Every sample from `from` (the pcm's start) to `to`, at num/den ticks a
 * sample, from the records: each sample the mean of the level over its
 * span, the boundaries exact integers in 1/den of a tick, times 32,767,
 * rounded. `shift` moves every record that many ticks later (the
 * control). Returns the count. */
static size_t snd_model_render(uint64_t from, uint64_t to, uint32_t num, uint32_t den,
                               int64_t shift) {
    size_t n = 0, ri = 0;
    double level = 0.0, heard = 0.0;
    uint64_t end_u = (to - from) * den;
    /* The records in units from `from`, applied in order. */
    for (uint64_t k = 0; (k + 1u) * num <= end_u && n < SND_MAX_SAMPLES; k++) {
        uint64_t s0 = k * num, s1 = s0 + num, pos = s0;
        double acc = 0.0;
        for (;;) {
            uint64_t at_u = UINT64_MAX;
            if (ri < snd_n) {
                int64_t t = (int64_t)(snd_recs[ri].at - from) + (ri ? shift : 0);
                at_u = t <= 0 ? 0 : (uint64_t)t * den;
            }
            if (at_u >= s1) break;
            if (at_u > pos) {
                acc += heard * (double)(at_u - pos);
                pos = at_u;
            }
            level = snd_level(&snd_recs[ri], level);
            heard = snd_heard(&snd_recs[ri], level);
            ri++;
        }
        acc += heard * (double)(s1 - pos);
        double v = acc / (double)num * PCM_FULL_SCALE;
        long q = lround(v);
        snd_model_out[n++] = (int16_t)(q > INT16_MAX ? INT16_MAX : q < INT16_MIN ? INT16_MIN : q);
    }
    return n;
}

#endif /* PICO_COCO_TEST_SOUND_MODEL_H */
