/* test_audio_port.c — the port's audio path, heard (design.md §10),
 * from pico-oric's.
 *
 * pico-logo's lesson: a test of what the guest asked for is not a test of
 * what the speaker plays. This compiles src/port/audio.c itself on the
 * host, against sdk_sim/'s stand-ins for the SDK, whose DMA reads the
 * ring as the hardware does and hands every word to a sink. A square
 * wave, a level stepped every 7,102 ticks as the DAC is, makes a tone
 * through the core's pcm; the field loop's audio_push queues it,
 * blocking while the queue is full; and the sink must then hold exactly
 * those samples as compare words, each twice, in order, at the square's
 * pitch.
 *
 * Then the paths that only fail on the device: the queue running dry
 * (underrun samples, silence, and on again with nothing replayed), the
 * IRQ held off past a refill's deadline (late refills, then playback
 * carrying on), mute, which must keep consuming, and the volume.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "pcm.h"
#include "sdk_sim.h"
#include "test_util.h"

#define MID      ((COCO_PWM_TOP + 1u) / 2u)
#define SILENCE  (MID | (MID << 16))
#define SINK_MAX 400000u

static uint32_t sink[SINK_MAX];
static pcm_t    out;
static uint64_t now;

/* The square: half a period in ticks, 14,318,180 / 14,204 = 1,008.039 Hz. */
#define TONE_HALF 7102u
#define TONE_HZ   (14318180.0 / (2.0 * TONE_HALF))
static uint64_t next_edge;
static uint32_t tone_level;

/* What audio.c is to make of a sample: ±32767 at full volume is ±1023
 * about the mid-point, the same word for both ears (audio.h). */
static uint32_t word_for(int16_t s, unsigned volume) {
    int32_t v = (int32_t)MID + ((int32_t)s * (int32_t)volume) / (1 << 13);
    if (v < 0) v = 0;
    if (v > (int32_t)COCO_PWM_TOP) v = COCO_PWM_TOP;
    return (uint32_t)v | ((uint32_t)v << 16);
}

/* One NTSC field of the square's samples, 238,944 ticks, pushed as core
 * 0 pushes them. */
static int16_t pcm[COCO_AUDIO_BUF_LEN];
static int16_t pushed[SINK_MAX / 2];
static size_t n_pushed;

static void field(void) {
    now += 238944u;
    for (; next_edge <= now; next_edge += TONE_HALF) {
        tone_level ^= 1u;
        pcm_set_level(&out, (uint32_t)next_edge, tone_level);
    }
    pcm_advance(&out, (uint32_t)now);
    size_t n = pcm_drain(&out, pcm, COCO_AUDIO_BUF_LEN);
    memcpy(pushed + n_pushed, pcm, n * sizeof pcm[0]);
    n_pushed += n;
    audio_push(pcm, n);
}

/* Rising crossings of the mid-point in the left channel's words. */
static double sink_hz(size_t from, size_t to) {
    double rate = 150000000.0 / ((COCO_PWM_TOP + 1u) * COCO_PWM_OVERSAMPLE);
    long first = -1, last = -1;
    unsigned n = 0;
    for (size_t i = from + 2u; i < to; i += 2u) {
        bool lo = (sink[i - 2] & 0xFFFFu) < MID, hi = (sink[i] & 0xFFFFu) >= MID;
        if (lo && hi) {
            if (first < 0) first = (long)i;
            last = (long)i;
            n++;
        }
    }
    if (n < 2) return 0;
    /* Two slots a frame. */
    return (n - 1) / ((double)(last - first) / 2.0) * rate;
}

int main(void) {
    g_sim.sink = sink;
    g_sim.sink_max = SINK_MAX;
    /* A wait for room lasts until the next refill (audio.h). */
    g_sim.wfi_words = COCO_DMA_SLOTS_PER_HALF;

    audio_init();
    uint32_t rate_num, rate_den;
    audio_rate(&rate_num, &rate_den);
    CHECK(rate_num == 150000000u && rate_den == 4096u, "the rate is %u/%u", rate_num, rate_den);

    /* Before anything is queued the ring plays silence, and nothing is
     * an underrun: playback has not started. */
    sim_play(4u * COCO_DMA_SLOTS_PER_HALF);
    audio_stats_t st;
    audio_stats(&st, true);
    bool quiet = true;
    for (size_t i = 0; i < g_sim.sink_len; i++) quiet &= sink[i] == SILENCE;
    CHECK(quiet, "the ring before playback is not silence");
    CHECK(!st.started && st.underrun_samples == 0, "an underrun before playback started");
    size_t lead = g_sim.sink_len;

    /* ---- a tone, through the queue and the ring ----------------------- */
    pcm_init(&out, 0, 1, COCO_TICK_HZ, rate_num, rate_den);
    next_edge = TONE_HALF;
    for (int f = 0; f < 100; f++) field();
    audio_stats(&st, false);
    CHECK(st.started, "playback never started");
    CHECK(st.underrun_samples == 0 && st.late_refills == 0,
          "a steady producer underran %u, late %u", st.underrun_samples, st.late_refills);
    CHECK(st.level >= COCO_PCM_QUEUE_LEN - COCO_DMA_FRAMES_PER_HALF,
          "pacing on the queue keeps it full: %u", st.level);

    /* Every slot after the lead-in silence is the next pushed sample's
     * word, each twice, both ears alike, none skipped or repeated. */
    size_t at = lead;
    while (at < g_sim.sink_len && sink[at] == SILENCE) at++;
    size_t k = 0;
    while (k < n_pushed && word_for(pushed[k], 256) == SILENCE) k++;
    size_t played = 0, wrong = 0;
    for (; at + 1u < g_sim.sink_len && k < n_pushed; at += 2u, k++, played++) {
        uint32_t w = word_for(pushed[k], 256);
        if (sink[at] != w || sink[at + 1u] != w) wrong++;
    }
    printf("tone: %zu samples played as pushed, %zu wrong, queue %u\n", played, wrong, st.level);
    CHECK(played > 50000u && wrong == 0, "%zu of %zu frames are not the samples pushed", wrong,
          played);
    /* The control: one sample out of step, and the comparison sees it. */
    {
        size_t a = at - 2u * played, off = 0;
        for (size_t j = 0; j + 1u < played; j++)
            if (sink[a + 2u * j] != word_for(pushed[k - played + j + 1u], 256)) off++;
        CHECK(off > played / 10u, "a sample out of step matched %zu of %zu", played - off, played);
    }
    double hz = sink_hz(lead, g_sim.sink_len);
    printf("tone: %.3f Hz in the ring, %.3f computed\n", hz, TONE_HZ);
    CHECK(fabs(hz / TONE_HZ - 1) < 2e-4, "the ring's pitch is %.3f Hz", hz);

    /* ---- the queue runs dry ----------------------------------------- */
    {
        audio_stats(&st, false);
        uint32_t under0 = st.underrun_samples, consumed0 = st.consumed;
        size_t from = g_sim.sink_len;
        /* What is queued plays out, then the ring pads with silence. */
        sim_play(2u * (COCO_PCM_QUEUE_LEN + 4u * COCO_DMA_FRAMES_PER_HALF));
        audio_stats(&st, false);
        uint32_t under = st.underrun_samples - under0;
        CHECK(under >= 3u * COCO_DMA_FRAMES_PER_HALF, "a dry queue counted %u underrun samples",
              under);
        CHECK(st.consumed - consumed0 == (g_sim.sink_len - from) / 2u,
              "consumed %u, played %zu: an underrun still consumes", st.consumed - consumed0,
              (g_sim.sink_len - from) / 2u);
        CHECK(sink[g_sim.sink_len - 1] == SILENCE, "a dry queue does not play silence");
        /* Then on again: the new samples, not the old ones replayed. */
        n_pushed = 0;
        for (int f = 0; f < 10; f++) field();
        size_t i = g_sim.sink_len - 2u;
        bool found = false;
        for (size_t j = 0; j < n_pushed && !found; j++) found = sink[i] == word_for(pushed[j], 256);
        CHECK(found, "after an underrun the ring does not play the new samples");
    }

    /* ---- the IRQ held off past a refill's deadline (EL §6.4) ---------- */
    {
        for (int f = 0; f < 10; f++) field();
        audio_stats(&st, false);
        uint32_t late0 = st.late_refills, under0 = st.underrun_samples;
        uint32_t irqs0 = g_sim.irqs;
        g_sim.irq_masked = true;
        /* Two halves and a bit: both channels finish before the IRQ. */
        sim_play(2u * COCO_DMA_SLOTS_PER_HALF + 10u);
        sim_unmask();
        for (int f = 0; f < 20; f++) field();
        audio_stats(&st, false);
        uint32_t late = st.late_refills - late0;
        /* A refill a half, and no more: the twenty fields' own. */
        uint32_t halves = (uint32_t)(20u * 611u / COCO_DMA_FRAMES_PER_HALF) + 2u;
        printf("late: %u late refills, %u IRQs, %u underrun samples after one held IRQ\n", late,
               g_sim.irqs - irqs0, st.underrun_samples - under0);
        CHECK(late >= 1u && late <= 4u, "a held IRQ counted %u late refills", late);
        CHECK(g_sim.irqs - irqs0 <= halves + 4u, "the IRQ stormed: %u for %u halves",
              g_sim.irqs - irqs0, halves);
        /* Playback carries on: the last frames are the pushed ones. */
        size_t i = g_sim.sink_len - 2u;
        bool found = false;
        for (size_t j = 0; j < n_pushed && !found; j++) found = sink[i] == word_for(pushed[j], 256);
        CHECK(found, "after a late refill the ring does not play the pushed samples");
    }

    /* ---- mute keeps consuming; the volume scales ---------------------- */
    {
        audio_set_muted(true);
        audio_stats(&st, false);
        uint32_t consumed0 = st.consumed, under0 = st.underrun_samples;
        size_t from = g_sim.sink_len;
        for (int f = 0; f < 20; f++) field();
        audio_stats(&st, false);
        bool silent = true;
        /* After what was queued and in the ring before the mute, which
         * was converted when it was pushed. */
        size_t after = from + 2u * COCO_PCM_QUEUE_LEN + COCO_DMA_RING_SLOTS;
        for (size_t i = after; i < g_sim.sink_len; i++) silent &= sink[i] == SILENCE;
        CHECK(silent, "muted, the ring is not silent");
        CHECK(st.consumed - consumed0 > 10000u && st.underrun_samples == under0,
              "muted, %u consumed and %u underrun", st.consumed - consumed0,
              st.underrun_samples - under0);
        audio_set_muted(false);

        audio_set_volume(128);
        n_pushed = 0;
        for (int f = 0; f < 20; f++) field();
        size_t i = g_sim.sink_len - 2u;
        bool found = false;
        for (size_t j = 0; j < n_pushed && !found; j++) found = sink[i] == word_for(pushed[j], 128);
        CHECK(found, "at half volume the ring does not hold the scaled samples");
    }

    TEST_DONE();
}
