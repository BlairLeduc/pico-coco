/* test_drivesnd.c — the disk drives' sounds (drivesnd.h, design.md §10).
 *
 * Parts that need no ROM, so CI runs them:
 *
 *   voice     the synthesiser alone: a step struck in the sample its
 *             pulse falls in, a train's count, silence after it to the
 *             sample, the motor's swell and fade, and off is silent
 *   recorded  the WAVE reader (wav.h) on files made here, and the voice
 *             playing recordings: a step from its first sample at each
 *             pulse, the motor's loop round and round, the synthesised
 *             sounds again without them
 *   machine   through the cartridge: the WD1793's pulses and the latch's
 *             motor bit reach the voice, and with the sounds off the
 *             machine's samples are those of one that never had them
 *
 * With Disk BASIC (bas13, extbas11 and disk11 in roms/; a skip without
 * them is not a pass): a DIR heard, every step of the head's way to the
 * directory struck, the motor on while RS-DOS runs it and off when it
 * stops it, and the sound the machine with the sounds off makes, apart
 * from that. `--wav FILE` writes what it heard, to listen to, and
 * `--sounds DIR` plays DIR's step.wav and motor.wav, as the card's
 * /coco/sounds/ (tools/drive-sounds.py cuts them).
 */

#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "wav.h"
#include "disk_util.h"
#include "guest.h"
#include "test_util.h"

/* A sample's ticks at the nominal rate, rounded: near enough to place
 * samples for the voice alone, which takes whatever times it is given. */
#define S 391u
#define MS(ms) ((uint32_t)((uint64_t)(ms) * COCO_TICK_HZ / 1000u))

/* Goertzel's power at f Hz over x[0..n), at the nominal rate. */
static double tone_power(const int32_t *x, size_t n, double f) {
    double w = 2.0 * 3.14159265358979 * f / 36621.0, c = 2.0 * __builtin_cos(w), s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* A band's mean power, from Goertzel's at eight points across it. */
static double band_db(const int32_t *x, size_t n, double lo, double hi) {
    double p = 0;
    for (int i = 0; i < 8; i++) p += tone_power(x, n, lo * __builtin_pow(hi / lo, (i + 0.5) / 8.0));
    return 10.0 * __builtin_log10(p / 8.0 + 1e-9);
}

/* ---- voice --------------------------------------------------------------- */

static int test_voice(void) {
    static drivesnd_t d;
    drivesnd_init(&d);

    /* Off: nothing taken, nothing sounding. */
    drivesnd_steps(&d, 0, 5, MS(6));
    drivesnd_motor(&d, true);
    CHECK(!drivesnd_sounding(&d) && d.left == 0, "off: the train not taken, silent");
    drivesnd_motor(&d, false);

    /* A train of three, 6 ms apart, from a tick inside sample 10. */
    drivesnd_enable(&d, true);
    const uint32_t from = 10u * S + 100u, period = MS(6);
    drivesnd_steps(&d, from, 3, period);
    unsigned struck[4] = { 0 }, n = 0;
    int32_t peak = 0;
    uint32_t last_loud = 0;
    for (uint32_t k = 0; k < 4000u; k++) {
        uint32_t before = d.clicks;
        int32_t v = drivesnd_next(&d, k * S);
        if (d.clicks != before && n < 4) struck[n++] = k;
        if (v < 0) v = -v;
        if (v > peak) peak = v;
        if (v) last_loud = k;
    }
    CHECK(n == 3 && d.clicks == 3, "three steps struck: %u", n);
    for (unsigned i = 0; i < n && i < 3; i++) {
        uint32_t at = from + i * period, want = (at + S - 1u) / S;   /* first sample at or after */
        CHECK(struck[i] == want, "step %u in sample %u, want %u", i, struck[i], want);
    }
    CHECK(peak >= 2000 && peak <= 16000, "a step's peak %d: heard, and short of the guest's",
          (int)peak);
    /* The last dies into true silence, 0 to the sample, within 80 ms: a
     * recorded step's tail is gone by 45 ms, and its modes are rung out
     * at 60 (config.h). At the fastest rate, 6 ms, a seek's steps run
     * together, as a real drive's buzz does. */
    CHECK(last_loud < struck[2] + MS(80) / S, "silent within 80 ms of the last: %u samples",
          (unsigned)(last_loud - struck[2]));
    CHECK(!drivesnd_sounding(&d), "idle after the train");

    /* A step's shape, the motor stopped: a strike, the head settling
     * 11 ms on, and the drive's noise raised after it, still there from
     * 45 to 55 ms when the modes are all but rung out. */
    {
        static drivesnd_t e;
        drivesnd_init(&e);
        drivesnd_enable(&e, true);
        drivesnd_steps(&e, 0, 1, MS(6));
        double ms_rms[56];
        for (unsigned m = 0; m < 56; m++) {
            double sq = 0;
            for (unsigned i = 0; i < 37; i++) {
                int32_t v = drivesnd_next(&e, (m * 37u + i) * S);
                sq += (double)v * v;
            }
            ms_rms[m] = __builtin_sqrt(sq / 37.0);
        }
        CHECK(ms_rms[10] > 1.3 * ms_rms[9], "the head settles at 11 ms: %.0f after %.0f",
              ms_rms[10], ms_rms[9]);
        CHECK(ms_rms[0] > 2.0 * ms_rms[11], "the strike the loudest: %.0f against %.0f",
              ms_rms[0], ms_rms[11]);
        drivesnd_steps(&e, 0, 0, 0);
        double late = 0;
        for (unsigned m = 45; m < 56; m++) late += ms_rms[m] / 11.0;
        CHECK(late > 15.0, "the noise still raised from 45 to 55 ms: %.0f", late);
        /* Struck again and quietened: nothing left sounding. */
        drivesnd_steps(&e, 56u * 37u * S, 1, MS(6));
        for (unsigned i = 0; i < 100u; i++) (void)drivesnd_next(&e, (56u * 37u + i) * S);
        drivesnd_quiet(&e);
        CHECK(!drivesnd_sounding(&e), "quiet: the step stops ringing");
    }

    /* A new train drops what was left of the last. */
    drivesnd_steps(&d, 0, 10, period);
    drivesnd_steps(&d, 0, 2, period);
    CHECK(d.left == 2, "the new train in place of the old");
    drivesnd_quiet(&d);
    CHECK(!drivesnd_sounding(&d), "quiet");

    /* The motor: a swell to a steady rush, then a fade to nothing. */
    drivesnd_motor(&d, true);
    uint64_t sq_early = 0, sq_late = 0;
    uint32_t k = 0;
    for (; k < 36621u; k++) {   /* a second */
        int32_t v = drivesnd_next(&d, k * S);
        if (k < 1000u) sq_early += (uint64_t)((int64_t)v * v);
        if (k >= 36621u - 7324u) sq_late += (uint64_t)((int64_t)v * v);   /* a revolution */
    }
    double rms_early = __builtin_sqrt((double)sq_early / 1000.0);
    double rms_late = __builtin_sqrt((double)sq_late / 7324.0);
    CHECK(rms_late >= 150.0 && rms_late <= 3000.0, "the motor's rush, RMS %.0f", rms_late);
    CHECK(rms_early < rms_late / 3.0, "it swells: %.0f in its first 27 ms against %.0f",
          rms_early, rms_late);
    /* Its colour, the drive's as measured (tools/drivesnd-coeffs.py): a
     * hump at 1 kHz, 300 Hz some 6 dB under it and 6.3 kHz some 22,
     * measured over these bands here at 10 and 25. */
    static int32_t run[16384];
    for (unsigned i = 0; i < 16384u; i++, k++) run[i] = drivesnd_next(&d, k * S);
    double b300 = band_db(run, 16384, 300, 400), b1k = band_db(run, 16384, 1000, 1300),
           b6k = band_db(run, 16384, 6300, 8000);
    CHECK(b1k - b300 >= 4.0 && b1k - b300 <= 12.0, "300 Hz %.1f dB under 1 kHz", b1k - b300);
    CHECK(b1k - b6k >= 23.0 && b1k - b6k <= 30.0, "6.3 kHz %.1f dB under 1 kHz", b1k - b6k);
    drivesnd_motor(&d, false);
    uint32_t stopped = k;
    for (; k < stopped + 2u * 36621u && drivesnd_sounding(&d); k++) (void)drivesnd_next(&d, k * S);
    CHECK(!drivesnd_sounding(&d), "the motor's sound gone within 2 s: %u samples",
          (unsigned)(k - stopped));
    CHECK(k - stopped > 36621u / 20u, "and fading, not cut: %u samples", (unsigned)(k - stopped));

    /* Turned off while it sounds: the next sample is 0. */
    drivesnd_motor(&d, true);
    drivesnd_steps(&d, k * S, 40, period);   /* 240 ms: still running when off */
    for (unsigned i = 0; i < 2000u; i++, k++) (void)drivesnd_next(&d, k * S);
    drivesnd_enable(&d, false);
    CHECK(!drivesnd_sounding(&d), "off stops it at once");
    /* And on again, later: none of the old train struck, though its
     * steps' times have all gone by. */
    uint32_t c = d.clicks;
    drivesnd_enable(&d, true);
    for (unsigned i = 0; i < 2000u; i++, k++) (void)drivesnd_next(&d, (k + 100000u) * S);
    CHECK(d.clicks == c, "on again: nothing left of the old train, %u struck",
          (unsigned)(d.clicks - c));
    return 0;
}

/* ---- recorded ------------------------------------------------------------ */

static size_t put_wav(uint8_t *b, uint32_t rate, uint16_t ch, uint16_t bits, bool list,
                      const uint8_t *data, uint32_t n) {
    size_t at = 0;
#define P4(v) do { uint32_t v_ = (v); for (int i_ = 0; i_ < 4; i_++) b[at++] = (uint8_t)(v_ >> (8 * i_)); } while (0)
#define P2(v) do { uint16_t v_ = (uint16_t)(v); b[at++] = (uint8_t)v_; b[at++] = (uint8_t)(v_ >> 8); } while (0)
    memcpy(b + at, "RIFF", 4), at += 4;
    P4(0);
    memcpy(b + at, "WAVE", 4), at += 4;
    memcpy(b + at, "fmt ", 4), at += 4;
    P4(16);
    P2(1);
    P2(ch);
    P4(rate);
    P4(rate * ch * bits / 8u);
    P2(ch * bits / 8u);
    P2(bits);
    if (list) {   /* an editor's LIST chunk, odd-sized and padded */
        memcpy(b + at, "LIST", 4), at += 4;
        P4(5);
        memcpy(b + at, "INFOx\0", 6), at += 6;
    }
    memcpy(b + at, "data", 4), at += 4;
    P4(n);
    memcpy(b + at, data, n), at += n;
#undef P4
#undef P2
    return at;
}

static int test_recorded(void) {
    static uint8_t f[4096];
    wav_info_t w;
    int16_t out[8];

    /* 16-bit mono, with a LIST chunk before the data. */
    const uint8_t m16[] = { 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x7F };
    size_t n = put_wav(f, 36621, 1, 16, true, m16, sizeof m16);
    CHECK(wav_header(f, n, (uint32_t)n, &w) == NULL && w.rate == 36621 && w.channels == 1 &&
              w.bits == 16 && w.data_len == 8,
          "16-bit mono, past a LIST chunk");
    CHECK(wav_mono16(&w, f + w.data_at, w.data_len, out, 8) == 4 && out[0] == 1 && out[1] == -1 &&
              out[2] == -32768 && out[3] == 32767,
          "its samples");
    /* 8-bit stereo, mixed: (255 - 128 + 1 - 128) / 2 x 256 = 0. */
    const uint8_t s8[] = { 255, 1, 128, 128, 192, 192 };
    n = put_wav(f, 22050, 2, 8, false, s8, sizeof s8);
    CHECK(wav_header(f, n, (uint32_t)n, &w) == NULL && w.data_len == 6, "8-bit stereo");
    CHECK(wav_mono16(&w, f + w.data_at, w.data_len, out, 8) == 3 && out[0] == 0 && out[1] == 0 &&
              out[2] == 64 * 256,
          "mixed to mono: %d %d %d", out[0], out[1], out[2]);
    CHECK(wav_mono16(&w, f + w.data_at, w.data_len, out, 2) == 2, "no more than asked for");
    /* 16-bit stereo: (1000 + 3000) / 2, (-2 + 0) / 2. */
    const uint8_t s16[] = { 0xE8, 0x03, 0xB8, 0x0B, 0xFE, 0xFF, 0x00, 0x00 };
    n = put_wav(f, 44100, 2, 16, false, s16, sizeof s16);
    CHECK(wav_header(f, n, (uint32_t)n, &w) == NULL &&
              wav_mono16(&w, f + w.data_at, w.data_len, out, 8) == 2 && out[0] == 2000 &&
              out[1] == -1,
          "16-bit stereo mixed: %d %d", out[0], out[1]);
    /* A file cut short: its data as far as it goes, in whole frames. */
    n = put_wav(f, 36621, 1, 16, false, m16, sizeof m16);
    CHECK(wav_header(f, n - 3, (uint32_t)(n - 3), &w) == NULL && w.data_len == 4, "cut short: %u",
          (unsigned)w.data_len);
    /* Refused. */
    n = put_wav(f, 36621, 1, 24, false, m16, 6);
    CHECK(wav_header(f, n, (uint32_t)n, &w) != NULL, "24 bits refused");
    n = put_wav(f, 36621, 3, 16, false, m16, 6);
    CHECK(wav_header(f, n, (uint32_t)n, &w) != NULL, "three channels refused");
    n = put_wav(f, 4000, 1, 16, false, m16, 8);
    CHECK(wav_header(f, n, (uint32_t)n, &w) != NULL, "4 kHz refused");
    n = put_wav(f, 36621, 1, 16, false, m16, 8);
    f[20] = 3;   /* IEEE float */
    CHECK(wav_header(f, n, (uint32_t)n, &w) != NULL, "not PCM refused");
    CHECK(wav_header((const uint8_t *)"RIFX....WAVE", 12, 12, &w) != NULL, "not RIFF refused");

    /* The rate: one for one at 36,621 Hz's nominal, half at half. */
    CHECK(drivesnd_inc(36621) == 65536u || drivesnd_inc(36621) == 65535u ||
              drivesnd_inc(36621) == 65537u,
          "36,621 Hz: %u", (unsigned)drivesnd_inc(36621));
    uint32_t half = drivesnd_inc(18310);
    CHECK(half >= 32760u && half <= 32775u, "18,310 Hz: %u", (unsigned)half);

    /* The voice playing recordings: a step is its samples, in order,
     * from the sample its pulse falls in; the motor's loop comes round. */
    static int16_t step[10], motor[7];
    for (int i = 0; i < 10; i++) step[i] = (int16_t)(1000 * (i + 1));
    for (int i = 0; i < 7; i++) motor[i] = (int16_t)(100 * (i + 1));
    drivesnd_samples_t rec = { step, 10, 65536u, motor, 7, 65536u };
    static drivesnd_t d;
    drivesnd_init(&d);
    drivesnd_enable(&d, true);
    drivesnd_set_samples(&d, &rec);
    drivesnd_steps(&d, 5u * S + 1u, 1, MS(6));
    int32_t got[20];
    for (uint32_t k = 0; k < 20; k++) got[k] = drivesnd_next(&d, k * S);
    bool same = true;
    for (int k = 0; k < 20; k++) {
        int32_t want = k >= 6 && k < 16 ? 1000 * (k - 5) : 0;
        if (got[k] != want) same = false;
    }
    CHECK(same, "the step's samples from sample 6: %d %d %d ... %d %d", (int)got[5], (int)got[6],
          (int)got[7], (int)got[15], (int)got[16]);
    CHECK(!drivesnd_sounding(&d), "idle after the step");
    /* Struck again while it plays: from its start again. */
    drivesnd_steps(&d, 20u * S, 2, 3u * S);
    for (uint32_t k = 20; k < 30; k++) got[k - 20] = drivesnd_next(&d, k * S);
    CHECK(got[0] == 1000 && got[2] == 3000 && got[3] == 1000 && got[4] == 2000,
          "a second step cuts the first short: %d %d %d %d %d", (int)got[0], (int)got[1],
          (int)got[2], (int)got[3], (int)got[4]);
    /* The motor at its full level, once it has swelled: the loop, round. */
    drivesnd_quiet(&d);
    drivesnd_motor(&d, true);
    drivesnd_quiet(&d);   /* restored turning: at full level at once */
    d.motor_pos = 0;
    uint32_t k0 = 1000;
    same = true;
    for (uint32_t k = 0; k < 21; k++) {
        int32_t v = drivesnd_next(&d, (k0 + k) * S);
        int32_t want = (int32_t)((int64_t)motor[k % 7] * 32767 >> 15);
        if (v != want) same = false;
    }
    CHECK(same, "the motor's loop round three times");
    /* Without recordings, the synthesised step again. */
    drivesnd_motor(&d, false);
    drivesnd_quiet(&d);
    drivesnd_set_samples(&d, NULL);
    uint32_t c = d.clicks;
    drivesnd_steps(&d, 2000u * S, 1, MS(6));
    int32_t loud = 0;
    for (uint32_t k = 2000; k < 2100; k++) {
        int32_t v = drivesnd_next(&d, k * S);
        if (v < 0) v = -v;
        if (v > loud) loud = v;
    }
    CHECK(d.clicks == c + 1 && loud > 2000 && loud != 1000, "synthesised again: %d", (int)loud);
    return 0;
}

/* The card's two recordings, as soundio.c reads them, from a directory. */
static int16_t s_step[COCO_DRIVESND_STEP_MAX], s_motor[COCO_DRIVESND_MOTOR_MAX];
static drivesnd_samples_t s_rec;

static uint32_t load_wav(const char *dir, const char *name, int16_t *dst, size_t max,
                         uint32_t *inc) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    static uint8_t buf[1u << 20];
    size_t n = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    wav_info_t w;
    const char *err = wav_header(buf, n < WAV_HEAD_MAX ? n : WAV_HEAD_MAX, (uint32_t)n, &w);
    if (err) {
        printf("  %s: %s\n", path, err);
        return 0;
    }
    *inc = drivesnd_inc(w.rate);
    return (uint32_t)wav_mono16(&w, buf + w.data_at, w.data_len, dst, max);
}

static void put_le(FILE *f, uint32_t v, int n);
static void wav_out_header(FILE *f, uint32_t samples);

/* `--calib DIR`: the voice alone, for tools/drivesnd-analyse.py to set
 * beside the recordings: DIR/motor.wav, 6 s of the motor; DIR/steps.wav,
 * 12 s of it with a step every 350 ms. */
static void calib_write(const char *path, bool steps) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static drivesnd_t d;
    drivesnd_init(&d);
    drivesnd_enable(&d, true);
    drivesnd_motor(&d, true);
    drivesnd_quiet(&d);   /* at full speed from the start */
    uint32_t n = (steps ? 12u : 6u) * 36621u;
    if (steps) drivesnd_steps(&d, 36621u / 2u * S, 34, MS(350));
    wav_out_header(f, n);
    for (uint32_t k = 0; k < n; k++) {
        int32_t v = drivesnd_next(&d, k * S);
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        put_le(f, (uint32_t)(uint16_t)(int16_t)v, 2);
    }
    fclose(f);
    printf("  wrote %s\n", path);
}

/* ---- machine --------------------------------------------------------------- */

static coco_t s_m[2];
static int16_t s_pcm[2][COCO_AUDIO_BUF_LEN];

static void machine_on(coco_t *m) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.cart = CART_RSDOS;
    coco_init(m, &cfg);
    coco_power_on(m);
}

/* A field of each, the two machines' samples drained and compared. True
 * if they differed; *loud the voice's loudest sample, as the difference. */
static bool field_pair(int32_t *loud) {
    size_t n[2];
    for (int i = 0; i < 2; i++) {
        coco_run_field(&s_m[i]);
        n[i] = coco_audio_drain(&s_m[i], s_pcm[i], COCO_AUDIO_BUF_LEN);
    }
    bool diff = n[0] != n[1];
    for (size_t j = 0; j < n[0] && j < n[1]; j++) {
        int32_t v = s_pcm[1][j] - s_pcm[0][j];
        if (v < 0) v = -v;
        if (v > *loud) *loud = v;
        if (v) diff = true;
    }
    return diff;
}

static int test_machine(void) {
    /* Machine 0 never has the sounds; machine 1 has them on. No ROM, so
     * the CPU runs what an empty map gives it, alike in both. */
    machine_on(&s_m[0]);
    machine_on(&s_m[1]);
    CHECK(!s_m[1].pcm.drive.on, "off after coco_init");
    coco_disk_sounds(&s_m[1], true);
    coco_disk_sounds(&s_m[0], false);
    int32_t loud = 0;
    bool diff = false;
    for (int f = 0; f < 3; f++) diff |= field_pair(&loud);
    CHECK(!diff, "on and idle: the same samples as never on");

    /* The motor bit: the rush comes, through the latch. */
    for (int i = 0; i < 2; i++) bus_write(&s_m[i], 0xFF40u, DK_MOTOR | DK_DS0);
    CHECK(s_m[1].pcm.drive.motor && !s_m[0].pcm.drive.motor, "the motor bit reaches the voice");
    loud = 0;
    for (int f = 0; f < 30; f++) diff |= field_pair(&loud);
    CHECK(diff && loud > 100, "the motor heard: %d", (int)loud);

    /* A disk's park: the drives go on, the guest's time stands, and the
     * machine without the sounds plays silence (park.c). */
    {
        static int16_t pk[2][2048];
        uint64_t t0 = coco_ticks(&s_m[1]);
        for (int i = 0; i < 2; i++) coco_disk_park_sound(&s_m[i], pk[i], 2048);
        double sq = 0;
        bool zero = true;
        for (int j = 0; j < 2048; j++) {
            sq += (double)pk[1][j] * pk[1][j];
            if (pk[0][j]) zero = false;
        }
        double rms = __builtin_sqrt(sq / 2048.0);
        CHECK(rms > 100.0, "a park plays the motor on: RMS %.0f", rms);
        CHECK(zero, "and silence with the sounds off");
        CHECK(coco_ticks(&s_m[1]) == t0, "guest time stands through it");
    }

    /* A Seek to track 20 at 6 ms steps: 20 pulses, 20 clicks. */
    uint32_t c0 = s_m[1].pcm.drive.clicks;
    for (int i = 0; i < 2; i++) {
        bus_write(&s_m[i], 0xFF4Bu, 20);       /* data: the track sought */
        bus_write(&s_m[i], 0xFF48u, 0x10u);    /* Seek, r1 r0 = 0: 6 ms */
    }
    for (int f = 0; f < 10; f++) field_pair(&loud);
    CHECK(coco_disk_fdc(&s_m[1])->drv[0].cyl == 20, "the head at 20: %u", coco_disk_fdc(&s_m[1])->drv[0].cyl);
    CHECK(s_m[1].pcm.drive.clicks - c0 == 20, "20 steps struck: %u",
          (unsigned)(s_m[1].pcm.drive.clicks - c0));
    CHECK(s_m[0].pcm.drive.clicks == 0, "and none with the sounds off");

    /* A Step-out at track 0 pulses nothing: TR00 stops it (wd1793.c). */
    for (int i = 0; i < 2; i++) bus_write(&s_m[i], 0xFF48u, 0x00u);   /* Restore */
    for (int f = 0; f < 15; f++) field_pair(&loud);
    CHECK(coco_disk_fdc(&s_m[1])->drv[0].cyl == 0, "restored to 0");
    CHECK(s_m[1].pcm.drive.clicks - c0 == 40, "the Restore's 20 struck: %u",
          (unsigned)(s_m[1].pcm.drive.clicks - c0));
    for (int i = 0; i < 2; i++) bus_write(&s_m[i], 0xFF48u, 0x60u);   /* Step-out */
    for (int f = 0; f < 2; f++) field_pair(&loud);
    CHECK(s_m[1].pcm.drive.clicks - c0 == 40, "a Step-out at 0 struck nothing");
    for (int i = 0; i < 2; i++) bus_write(&s_m[i], 0xFF48u, 0x40u);   /* Step-in */
    for (int f = 0; f < 2; f++) field_pair(&loud);
    CHECK(s_m[1].pcm.drive.clicks - c0 == 41, "a Step-in struck once");

    /* A command written in the middle of a field, after the last one's
     * step is due but before the field's end has made its sample: the
     * PCM is brought up to the write, so the first is struck too. */
    for (int i = 0; i < 2; i++) {
        bus_write(&s_m[i], 0xFF48u, 0x60u);              /* Step-out, 6 ms */
        coco_run(&s_m[i], MS(10));
        bus_write(&s_m[i], 0xFF48u, 0x40u);              /* Step-in */
    }
    for (int f = 0; f < 2; f++) field_pair(&loud);
    CHECK(s_m[1].pcm.drive.clicks - c0 == 43, "both of two steps in a field struck: %u",
          (unsigned)(s_m[1].pcm.drive.clicks - c0));

    /* A state restored, or the power switched on, mid-seek: what was to
     * come is dropped. The motor follows the latch after a restore, and
     * is stopped by a power-on. */
    bus_write(&s_m[1], 0xFF4Bu, 30);
    bus_write(&s_m[1], 0xFF48u, 0x13u);                  /* Seek, 30 ms steps */
    coco_restored(&s_m[1]);
    CHECK(s_m[1].pcm.drive.left == 0 && s_m[1].pcm.drive.motor,
          "restored: no steps to come, the motor still on");
    bus_write(&s_m[1], 0xFF48u, 0xD0u);                  /* Force Interrupt */
    /* From the head, a step ahead of the track register: Step-in without
     * its update flag moved it and not the register. */
    unsigned head = coco_disk_fdc(&s_m[1])->drv[0].cyl;
    bus_write(&s_m[1], 0xFF48u, 0x03u);                  /* Restore */
    CHECK(head == 31 && s_m[1].pcm.drive.left == head, "the Restore's %u to come: %u", head,
          (unsigned)s_m[1].pcm.drive.left);
    coco_power_on(&s_m[1]);
    CHECK(s_m[1].pcm.drive.left == 0 && !s_m[1].pcm.drive.motor && s_m[1].pcm.drive.on,
          "powered on: silent, the motor stopped, the setting kept");
    machine_on(&s_m[1]);
    machine_on(&s_m[0]);
    coco_disk_sounds(&s_m[1], true);

    /* Motor off and the sounds off: the samples agree again. */
    for (int i = 0; i < 2; i++) bus_write(&s_m[i], 0xFF40u, 0);
    for (int f = 0; f < 120; f++) field_pair(&loud);
    loud = 0;
    CHECK(!field_pair(&loud), "the motor stopped: silent again");
    coco_disk_sounds(&s_m[1], true);
    bus_write(&s_m[1], 0xFF40u, DK_MOTOR);
    bus_write(&s_m[0], 0xFF40u, DK_MOTOR);
    field_pair(&loud);
    coco_disk_sounds(&s_m[1], false);
    loud = 0;
    CHECK(!field_pair(&loud), "turned off: at once what never had it");
    return 0;
}

/* ---- Disk BASIC ----------------------------------------------------------- */

static guest_t s_g[2];
static host_disk_t s_disk[2];
static host_disk_t *s_drives[2][COCO_DISK_DRIVES];
static FILE *s_wav;
static uint32_t s_wav_n;
static int32_t s_loud, s_loud_motor;
static bool s_diff_idle;
static int s_phase;   /* 0 idle, 1 the DIR, 2 after it */

static void serve0(guest_t *g) { host_disk_serve(&g->m, s_drives[0]); }
static void serve1(guest_t *g) { host_disk_serve(&g->m, s_drives[1]); }

static void put_le(FILE *f, uint32_t v, int n) {
    for (int i = 0; i < n; i++) fputc((int)((v >> (8 * i)) & 0xFFu), f);
}

static void wav_out_header(FILE *f, uint32_t samples) {
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    put_le(f, 36u + samples * 2u, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4);
    put_le(f, 1, 2);        /* PCM */
    put_le(f, 1, 2);        /* mono */
    put_le(f, 36621, 4);    /* 150 MHz / 4,096 */
    put_le(f, 36621u * 2u, 4);
    put_le(f, 2, 2);
    put_le(f, 16, 2);
    fwrite("data", 1, 4, f);
    put_le(f, samples * 2u, 4);
}

/* Both machines a field, in step; the difference is the voice. */
static void fields(int n) {
    for (int k = 0; k < n; k++) {
        size_t c[2];
        for (int i = 0; i < 2; i++) {
            guest_fields(&s_g[i], 1);
            c[i] = coco_audio_drain(&s_g[i].m, s_pcm[i], COCO_AUDIO_BUF_LEN);
        }
        for (size_t j = 0; j < c[0] && j < c[1]; j++) {
            int32_t v = s_pcm[1][j] - s_pcm[0][j];
            int32_t a = v < 0 ? -v : v;
            if (a && s_phase == 0) s_diff_idle = true;
            if (s_phase == 1 && a > s_loud) s_loud = a;
            if (s_wav) {
                put_le(s_wav, (uint32_t)(uint16_t)s_pcm[1][j], 2);
                s_wav_n++;
            }
        }
        if (s_phase == 1 && s_g[1].m.pcm.drive.motor && !s_g[1].m.pcm.drive.left &&
            !s_g[1].m.pcm.drive.burst) {
            for (size_t j = 0; j < c[1] && j < c[0]; j++) {
                int32_t a = s_pcm[1][j] - s_pcm[0][j];
                if (a < 0) a = -a;
                if (a > s_loud_motor) s_loud_motor = a;
            }
        }
    }
}

static bool rows_have(const guest_t *g, const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(&g->m, r), s)) return true;
    return false;
}

static int test_disk_basic(const char *wav, const char *sounds) {
    /* A disk formatted by DSKINI on the host would take a while; an
     * RS-DOS disk's directory is enough for DIR: track 17, sectors 2 to
     * 11 of $FF entries, the FAT in sector 2 ($FF free), all from a blank
     * image of $FF. */
    for (int i = 0; i < 2; i++) {
        host_disk_blank(&s_disk[i], 35, 0xFFu);
        memset(s_drives[i], 0, sizeof s_drives[i]);
        s_drives[i][0] = &s_disk[i];
        guest_build_disk(&s_g[i], 32);
        s_g[i].after_field = i ? serve1 : serve0;
        host_disk_insert(&s_g[i].m, 0, &s_disk[i]);
    }
    coco_disk_sounds(&s_g[1].m, true);
    if (sounds) {
        s_rec.step = s_step;
        s_rec.motor = s_motor;
        s_rec.step_len = load_wav(sounds, "step.wav", s_step, COCO_DRIVESND_STEP_MAX, &s_rec.step_inc);
        s_rec.motor_len =
            load_wav(sounds, "motor.wav", s_motor, COCO_DRIVESND_MOTOR_MAX, &s_rec.motor_inc);
        printf("  recorded: step %u samples, motor %u\n", (unsigned)s_rec.step_len,
               (unsigned)s_rec.motor_len);
        drivesnd_set_samples(&s_g[1].m.pcm.drive, &s_rec);
    }
    if (wav) {
        s_wav = fopen(wav, "wb");
        if (s_wav) wav_out_header(s_wav, 0);
    }

    bool ok = true;
    for (int i = 0; i < 2; i++) ok &= guest_boot(&s_g[i]);
    CHECK(ok, "Disk BASIC booted");
    fields(30);
    CHECK(!s_diff_idle, "booted and idle: the samples of the machine without the sounds");
    CHECK(coco_disk_fdc(&s_g[1].m)->drv[0].cyl == 0 && s_g[1].m.pcm.drive.clicks == 0,
          "no step yet: the head at %u, %u struck", coco_disk_fdc(&s_g[1].m)->drv[0].cyl,
          (unsigned)s_g[1].m.pcm.drive.clicks);

    s_phase = 1;
    bool motor_seen = false;
    for (int i = 0; i < 2; i++) guest_type(&s_g[i], "cls:dir\r");
    int f;
    for (f = 0; f < 60 * 10; f++) {
        fields(1);
        motor_seen |= s_g[1].m.pcm.drive.motor;
        if (rows_have(&s_g[1], "OK") && !coco_disk_busy(&s_g[1].m)) break;
    }
    unsigned cyl = coco_disk_fdc(&s_g[1].m)->drv[0].cyl;
    CHECK(cyl == 17, "DIR's head on the directory's track: %u", cyl);
    CHECK(s_g[1].m.pcm.drive.clicks == 17, "every step of the way struck, 0 to 17: %u",
          (unsigned)s_g[1].m.pcm.drive.clicks);
    CHECK(motor_seen, "the motor on while DIR ran");
    CHECK(s_loud >= 2000, "the steps heard: %d", (int)s_loud);
    CHECK(s_loud_motor > 0, "the motor heard between steps: %d", (int)s_loud_motor);
    printf("  DIR: %u steps struck, loudest %d, motor %d, %d fields\n",
           (unsigned)s_g[1].m.pcm.drive.clicks, (int)s_loud, (int)s_loud_motor, f);

    /* RS-DOS stops the motor a while after (its timer in the field's
     * IRQ), and then the two machines agree again. */
    s_phase = 2;
    int stop;
    for (stop = 0; stop < 60 * 10 && s_g[1].m.pcm.drive.motor; stop++) fields(1);
    CHECK(!s_g[1].m.pcm.drive.motor, "RS-DOS stopped the motor, %d fields on", stop);
    fields(150);
    s_phase = 0;
    s_diff_idle = false;
    fields(30);
    CHECK(!s_diff_idle, "after it: the samples of the machine without the sounds");

    if (s_wav) {
        wav_out_header(s_wav, s_wav_n);
        fclose(s_wav);
        printf("  wrote %s, %u samples\n", wav, (unsigned)s_wav_n);
    }
    for (int i = 0; i < 2; i++) host_disk_free(&s_disk[i]);
    return 0;
}

int main(int argc, char **argv) {
    const char *wav = NULL, *sounds = NULL;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--wav") == 0) wav = argv[i + 1];
        if (strcmp(argv[i], "--sounds") == 0) sounds = argv[i + 1];
        if (strcmp(argv[i], "--calib") == 0) {
            char p[512];
            snprintf(p, sizeof p, "%s/motor.wav", argv[i + 1]);
            calib_write(p, false);
            snprintf(p, sizeof p, "%s/steps.wav", argv[i + 1]);
            calib_write(p, true);
            return 0;
        }
    }
    if (test_voice()) return 1;
    if (test_recorded()) return 1;
    if (test_machine()) return 1;

    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11) ||
        !guest_have_rom(ROM_COCO_DISK11)) {
        printf("SKIP: Disk BASIC needs bas13, extbas11 and disk11 in %s\n", dir);
        if (test_failures) TEST_DONE();
        return TEST_SKIP_CODE;
    }
    if (test_disk_basic(wav, sounds)) return 1;
    TEST_DONE();
}
