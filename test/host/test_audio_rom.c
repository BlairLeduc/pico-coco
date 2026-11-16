/* test_audio_rom.c — BASIC's own sounds (design.md §10, §17 M8).
 *
 * Extended Color BASIC booted from power-on with every instruction's
 * pins logged (sound_model.h), and SOUND and PLAY typed at it. Every
 * sample since power-on is then held against the independent model run
 * on the log, to 1 LSB with the DC blocker off.
 *
 * Then the pitch. Both commands play a four-step staircase on the DAC
 * ($02, $7E, $FE, $7E for SOUND); the steps are the ROM's delay loop,
 * and an interrupt now and then lengthens one. For each tone, three
 * figures: the loop's own period, the shortest four steps in the log, in
 * cycles; the pitch the edges make, interrupts and all; and the pitch
 * measured off the output samples, at the same rising step. The last two
 * must agree to 1 part in 10^3, and SOUND's loop must be 20 (256 - T) +
 * 100 cycles, which is 5 (256 - T) + 23 a step and 8 more on the fourth:
 * read off the log at four tones, 2026-11-15. PLAY's notes are held to
 * equal temperament at A = 440 Hz, O2's A an octave below, to 0.5 %: the
 * seven of O2 measured -0.26 % to +0.24 % by their loops. The field
 * interrupt lengthens a step 60 times a second, so what is heard is
 * about 0.7 % flatter than the loop, as it is on the machine.
 *
 * Then the idle prompt: two seconds of BASIC's keyboard scan, which
 * writes PIA0 and reads the joystick's comparator through the DAC's
 * port, must not move the bus at all.
 *
 * Needs bas13.rom and extbas11.rom (guest.h); skips without them.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"
#include "sound_model.h"
#include "test_util.h"

static guest_t g;
static int16_t core_out[SND_MAX_SAMPLES];
static size_t  n_core;

static void after_field(guest_t *gg) {
    n_core += coco_audio_drain(&gg->m, core_out + n_core, SND_MAX_SAMPLES - n_core);
}

/* The E clock, in Hz: the master oscillator over 16 (§13.2). */
#define E_HZ ((double)COCO_TICK_HZ / 16.0)

/* The sample a tick falls in, as a real number: sample k covers ticks
 * [k, k+1) x num/den from power-on. */
static double tick_to_sample(double t) {
    return t * (double)g.m.pcm.den / (double)g.m.pcm.num;
}

typedef struct {
    double loop_cycles;   /* the shortest four steps                    */
    double edge_hz;       /* the rising step's rate in the log          */
    double out_hz;        /* the same off the output                    */
    unsigned periods;
} tone_t;

/* The tone in records [r0, r1): the DAC's steps while the multiplexer is
 * on it. The rising step is the one to the DAC's highest value. */
static int measure(size_t r0, size_t r1, tone_t *t) {
    memset(t, 0, sizeof *t);
    uint8_t hi = 0;
    for (size_t i = r0; i < r1; i++)
        if (snd_recs[i].pa_out > hi) hi = snd_recs[i].pa_out;

    /* The edges up to `hi`, in ticks, and the shortest period between
     * two, which no interrupt lengthened. */
    static uint64_t up[20000];
    size_t nup = 0;
    for (size_t i = r0 + 1; i < r1; i++)
        if (snd_recs[i].pa_out == hi && snd_recs[i - 1].pa_out != hi && nup < 20000u)
            up[nup++] = snd_recs[i].at;
    CHECK(nup >= 8, "a tone of only %zu periods", nup);
    if (nup < 8) return 1;
    double best = 1e30;
    for (size_t k = 1; k < nup; k++)
        if ((double)(up[k] - up[k - 1]) / 16.0 < best) best = (double)(up[k] - up[k - 1]) / 16.0;
    t->loop_cycles = best;
    t->periods = (unsigned)(nup - 1u);
    t->edge_hz = (double)(nup - 1u) * COCO_TICK_HZ / (double)(up[nup - 1] - up[0]);

    /* Off the output: where it rises through the middle of the step to
     * `hi`, between whole samples, interpolated. A box filter makes the
     * step a ramp a sample long, so the crossing falls inside it. Each
     * crossing is looked for in a window around its edge. */
    double first = -1, last = -1;
    unsigned crossings = 0;
    for (size_t k = 0; k < nup; k++) {
        /* The level just before the edge and at it, from the model. */
        size_t ri = r0;
        while (ri < r1 && snd_recs[ri].at < up[k]) ri++;
        double before = snd_level(&snd_recs[ri - 1], 0.0) * PCM_FULL_SCALE;
        double after = snd_level(&snd_recs[ri], 0.0) * PCM_FULL_SCALE;
        double mid = (before + after) / 2.0;
        size_t s = (size_t)tick_to_sample((double)up[k]);
        double at = -1;
        for (size_t j = s > 2 ? s - 2 : 0; j + 1 < n_core && j < s + 3; j++) {
            if (core_out[j] < mid && core_out[j + 1] >= mid) {
                at = (double)j + (mid - core_out[j]) / (double)(core_out[j + 1] - core_out[j]);
                break;
            }
        }
        CHECK(at >= 0, "no rising crossing at the edge %zu of %zu", k, nup);
        if (at < 0) return 1;
        if (first < 0) first = at;
        last = at;
        crossings++;
    }
    /* Samples a second, exactly: den/num samples a tick. */
    double rate = (double)COCO_TICK_HZ * (double)g.m.pcm.den / (double)g.m.pcm.num;
    t->out_hz = (double)(crossings - 1u) * rate / (last - first);
    return 0;
}

/* Type a command, let it play out (two seconds is more than any here
 * takes), and measure its tone. */
static int play(const char *cmd, tone_t *t) {
    size_t r0 = snd_n;
    guest_type(&g, cmd);
    guest_fields(&g, 120);
    return measure(r0, snd_n, t);
}

static int test_sounds(void) {
    /* SOUND at four tones: the loop's count, and the pitch off the output
     * against the edges. */
    static const unsigned tones[] = { 1, 100, 200, 255 };
    for (unsigned i = 0; i < sizeof tones / sizeof tones[0]; i++) {
        char cmd[32];
        snprintf(cmd, sizeof cmd, "SOUND %u,4\r", tones[i]);
        tone_t t;
        if (play(cmd, &t)) return 1;
        double want = 20.0 * (256.0 - tones[i]) + 100.0;
        printf("SOUND %3u: loop %.0f cycles (%.3f Hz), edges %.3f Hz, output %.3f Hz, "
               "%u periods\n", tones[i], t.loop_cycles, E_HZ / t.loop_cycles, t.edge_hz,
               t.out_hz, t.periods);
        CHECK(t.loop_cycles == want, "SOUND %u: the loop is %.0f cycles, not %.0f", tones[i],
              t.loop_cycles, want);
        CHECK(fabs(t.out_hz / t.edge_hz - 1.0) < 1e-3,
              "SOUND %u: %.3f Hz off the output against %.3f from the edges", tones[i],
              t.out_hz, t.edge_hz);
        /* The control: the next tone up is not this one. */
        double next = E_HZ / (20.0 * (255.0 - tones[i]) + 100.0);
        if (tones[i] < 255u)
            CHECK(fabs(t.out_hz / next - 1.0) > 2e-3, "SOUND %u passes as %u", tones[i],
                  tones[i] + 1u);
    }

    /* A scale in PLAY's octave 2, each note against equal temperament. */
    static const char notes[] = "CDEFGAB";
    static const int semis[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (unsigned i = 0; i < 7u; i++) {
        char cmd[32];
        snprintf(cmd, sizeof cmd, "PLAY \"O2L8%c\"\r", notes[i]);
        tone_t t;
        if (play(cmd, &t)) return 1;
        double et = 220.0 * pow(2.0, (semis[i] - 9) / 12.0);
        printf("PLAY O2 %c: loop %.0f cycles (%.3f Hz), edges %.3f Hz, output %.3f Hz, "
               "tempered %.3f Hz (%+.2f%%)\n", notes[i], t.loop_cycles,
               E_HZ / t.loop_cycles, t.edge_hz, t.out_hz, et,
               100.0 * (E_HZ / t.loop_cycles / et - 1.0));
        CHECK(fabs(t.out_hz / t.edge_hz - 1.0) < 1e-3,
              "PLAY %c: %.3f Hz off the output against %.3f from the edges", notes[i],
              t.out_hz, t.edge_hz);
        CHECK(fabs(E_HZ / t.loop_cycles / et - 1.0) < 0.005,
              "PLAY %c: %.3f Hz, not within 0.5%% of %.3f", notes[i], E_HZ / t.loop_cycles, et);
    }
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: needs bas13.rom and extbas11.rom in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    guest_build(&g, 64, true);
    g.m.pcm.dc_block = false;
    snd_log_start(&g.m);
    g.after_field = after_field;
    CHECK(guest_boot(&g), "Extended Color BASIC did not boot");
    CHECK(g.m.sound_edges == 0, "the boot moved the bus %u times", g.m.sound_edges);

    /* The idle prompt: BASIC's scan, for two seconds. */
    size_t r0 = snd_n;
    guest_fields(&g, 120);
    CHECK(g.m.sound_edges == 0, "the idle prompt moved the bus %u times", g.m.sound_edges);
    printf("idle: two seconds at the prompt, %zu records of the pins, the bus still\n",
           snd_n - r0);

    if (test_sounds()) return 1;

    /* Every sample since power-on against the model. */
    CHECK(snd_lost == 0, "%zu records lost", snd_lost);
    CHECK(g.m.pcm.overflow == 0, "the pcm overflowed");
    size_t n = snd_model_render(0, coco_ticks(&g.m), g.m.pcm.num, g.m.pcm.den, 0);
    CHECK(n == n_core, "the model made %zu samples and the core %zu", n, n_core);
    if (n > n_core) n = n_core;
    int d = 0;
    size_t at = 0;
    for (size_t i = 0; i < n; i++)
        if (abs(core_out[i] - snd_model_out[i]) > d) {
            d = abs(core_out[i] - snd_model_out[i]);
            at = i;
        }
    CHECK(d <= 1, "sample %zu differs by %d (core %d, model %d)", at, d, core_out[at],
          snd_model_out[at]);
    printf("since power-on: %zu samples, %u changes of level, every sample equal to the "
           "model\n", n, g.m.sound_edges);
    TEST_DONE();
}
