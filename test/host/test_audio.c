/* test_audio.c — the sound bus, against a model of it (design.md §10,
 * §17 M8).
 *
 * A 6809 program in a ROM of the test's own writes the pins that reach
 * the bus, from a random script: the DAC's port and its direction, the
 * single bit and its direction, PIA1 CB2 enabling the multiplexer, PIA0
 * CA2 and CB2 choosing its source, and an Orchestra-90's two DACs
 * (§12.10), fitted in every third script and absent from the rest, at
 * random gaps, now and then a long one. Every sample the machine makes is held to the independent model
 * (sound_model.h) run on the pins the program left after each
 * instruction, to 1 LSB with the DC blocker off; the model with every
 * record a cycle late must not match. No ROM of the user's is needed, so
 * CI runs it all. test_audio_rom does the same with BASIC's own sounds.
 *
 * Then the parts alone: the sample count after any run, across the tick
 * count's 32-bit wrap; the DC blocker coming to rest at 0 from either
 * level; and PIA1 PB1 reading the bus back.
 */

#include <stdlib.h>
#include <string.h>

#include "sound_model.h"
#include "test_util.h"

static coco_t g_m;
static int16_t core_out[SND_MAX_SAMPLES];
static size_t n_core;

/* ---- the program ------------------------------------------------------ */

/* At $A000: LDS #$7F00; LDX #table; then for each entry (an address, a
 * byte, a 16-bit count) store the byte and count the count down, until
 * an address of 0; then wait at DONE. The CPU leaves RESET with IRQ and
 * FIRQ masked, so no interrupt is ever taken. */
#define PROG_DONE 0xA018u
#define TABLE_AT  0xA100u
static const uint8_t prog[] = {
    0x10, 0xCE, 0x7F, 0x00,   /* A000 LDS  #$7F00          */
    0x8E, 0xA1, 0x00,         /* A004 LDX  #TABLE          */
    0xEE, 0x81,               /* A007 LDU  ,X++   (LOOP)   */
    0x27, 0x0D,               /* A009 BEQ  DONE            */
    0xE6, 0x80,               /* A00B LDB  ,X+             */
    0xE7, 0xC4,               /* A00D STB  ,U              */
    0x10, 0xAE, 0x81,         /* A00F LDY  ,X++            */
    0x31, 0x3F,               /* A012 LEAY -1,Y   (WAIT)   */
    0x26, 0xFC,               /* A014 BNE  WAIT            */
    0x20, 0xEF,               /* A016 BRA  LOOP            */
    0x20, 0xFE,               /* A018 BRA  DONE   (DONE)   */
};

static uint8_t rom[COCO_ROM_HALF];
static size_t  table_len;

static uint32_t rng_state = 12345u;
static uint32_t rnd(uint32_t n) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) % n;
}

static void table_add(uint16_t a, uint8_t v, uint16_t count) {
    uint8_t *t = &rom[TABLE_AT - 0xA000u + table_len];
    t[0] = (uint8_t)(a >> 8);
    t[1] = (uint8_t)a;
    t[2] = v;
    t[3] = (uint8_t)(count >> 8);
    t[4] = (uint8_t)count;
    table_len += 5;
}

/* A control register's byte: the data register or the DDR, C1 never
 * enabled, C2 an input or an output set by hand (the pulse modes are not
 * modelled, coco.c). */
static uint8_t random_cr(void) {
    uint8_t v = (uint8_t)(rnd(2) ? 0x04u : 0x00u) | (uint8_t)(rnd(2) ? 0x02u : 0x00u);
    switch (rnd(4)) {
    case 0:  break;                               /* C2 an input           */
    case 1:  v |= (uint8_t)(rnd(2) ? 0x08u : 0); break;   /* ... its IRQ bit */
    default: v |= (uint8_t)(0x30u | (rnd(2) ? 0x08u : 0)); break;  /* set by hand */
    }
    return v;
}

/* A script: the pins started where BASIC leaves them, the multiplexer on
 * the DAC; then `n` random writes, the DAC's most often. */
static void make_script(unsigned n) {
    memset(rom, 0xFF, sizeof rom);
    memcpy(rom, prog, sizeof prog);
    rom[COCO_ROM_HALF - 2] = 0xA0;   /* RESET at $BFFE, read at $FFFE */
    rom[COCO_ROM_HALF - 1] = 0x00;
    table_len = 0;
    table_add(0xFF21, 0x30, 1);   /* PIA1 A: the DDR */
    table_add(0xFF20, 0xFC, 1);   /* the DAC's six bits out */
    table_add(0xFF21, 0x34, 1);
    table_add(0xFF01, 0x34, 1);   /* PIA0 CA2 and CB2 low: the DAC */
    table_add(0xFF03, 0x34, 1);
    table_add(0xFF23, 0x3C, 1);   /* PIA1 CB2 high: the multiplexer on */
    size_t room = (COCO_ROM_HALF - 0x20u - (TABLE_AT - 0xA000u)) / 5u - 1u;
    if (n > room - table_len / 5u) n = (unsigned)(room - table_len / 5u);
    for (unsigned i = 0; i < n; i++) {
        uint16_t count = (uint16_t)(rnd(400) ? 1u + rnd(1500) : 20000u + rnd(45000));
        uint16_t a;
        uint8_t v;
        switch (rnd(40)) {
        case 0:  a = 0xFF21; v = random_cr(); break;
        case 1:  a = 0xFF22; v = (uint8_t)rnd(256); break;
        case 2:  a = 0xFF23; v = random_cr(); break;
        case 3:  a = 0xFF01; v = random_cr(); break;
        case 4:  a = 0xFF03; v = random_cr(); break;
        case 5:  case 6: a = 0xFF23; v = rnd(3) ? 0x3Cu : 0x34u; break;
        case 7:  case 8: a = 0xFF22; v = rnd(2) ? 0x02u : 0x00u; break;
        case 9:  case 10: a = 0xFF01; v = 0x34; break;   /* back to the DAC */
        case 11: case 12: a = 0xFF03; v = 0x34; break;
        case 13: a = 0xFF21; v = 0x34; break;
        case 14: case 15: a = 0xFF7A; v = (uint8_t)rnd(256); break;   /* an Orchestra-90's */
        case 16: case 17: a = 0xFF7B; v = (uint8_t)rnd(256); break;
        default: a = 0xFF20; v = (uint8_t)rnd(256); break;
        }
        table_add(a, v, count);
    }
    table_add(0x0000, 0, 0);
}

/* Power on the program's machine and log its pins from tick 0: with an
 * Orchestra-90 fitted by the configuration, or with the port empty, where
 * its DACs' writes reach nothing. */
static void start(bool orch) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    if (orch) cfg.cart = CART_ORCH90;
    coco_init(&g_m, &cfg);
    coco_load_rom(&g_m, 0xA000u, rom, sizeof rom);
    coco_power_on(&g_m);
    g_m.pcm.dc_block = false;
    snd_log_start(&g_m);
    n_core = 0;
}

static void drain(void) {
    n_core += coco_audio_drain(&g_m, core_out + n_core, SND_MAX_SAMPLES - n_core);
}

/* Fields until the program reaches DONE, and a few after. */
static void run_to_done(void) {
    for (int f = 0; f < 200000 && g_m.cpu.pc != PROG_DONE; f++) {
        coco_run_field(&g_m);
        drain();
    }
    for (int f = 0; f < 3; f++) {
        coco_run_field(&g_m);
        drain();
    }
}

static int max_diff(size_t n, size_t *where) {
    int d = 0;
    for (size_t i = 0; i < n; i++) {
        int e = abs(core_out[i] - snd_model_out[i]);
        if (e > d) {
            d = e;
            *where = i;
        }
    }
    return d;
}

static int test_scripts(void) {
    size_t samples = 0, edges = 0;
    unsigned orch = 0;
    for (int s = 0; s < 24; s++) {
        make_script(s < 4 ? 40u : 1500u);
        start(s % 3 == 2);
        run_to_done();
        CHECK(g_m.cpu.pc == PROG_DONE, "script %d never finished", s);
        CHECK(snd_lost == 0, "script %d: %zu records lost", s, snd_lost);
        CHECK(g_m.pcm.overflow == 0, "script %d: the pcm overflowed", s);
        size_t n = snd_model_render(0, coco_ticks(&g_m), g_m.pcm.num, g_m.pcm.den, 0);
        CHECK(n == n_core, "script %d: the model made %zu samples and the core %zu", s, n, n_core);
        if (n > n_core) n = n_core;
        size_t at = 0;
        int d = max_diff(n, &at);
        CHECK(d <= 1, "script %d: sample %zu differs by %d (core %d, model %d)", s, at, d,
              core_out[at], snd_model_out[at]);
        samples += n;
        edges += g_m.sound_edges;
        if (s % 3 == 2) {
            orch += g_m.port.slot[0].u.orch.left | g_m.port.slot[0].u.orch.right ? 1u : 0u;
        } else {
            CHECK(g_m.port.slot[0].kind == CART_NONE, "script %d: a cartridge in an empty port", s);
        }

        /* The control: every record a cycle late. */
        if (s == 5) {
            size_t m = snd_model_render(0, coco_ticks(&g_m), g_m.pcm.num, g_m.pcm.den, 16);
            if (m > n_core) m = n_core;
            int dl = max_diff(m, &at);
            CHECK(dl > 1, "records a cycle late made no difference (%d): the check is blind", dl);
            printf("control, a cycle late: a sample differs by %d\n", dl);
        }
    }
    CHECK(edges > 5000u, "only %zu changes of level: the scripts barely move the bus", edges);
    CHECK(orch >= 6u, "the Orchestra-90's DACs were left at 0 in %u of 8 scripts", 8u - orch);
    printf("scripts: %zu samples, %zu changes of level, every sample equal to the model\n",
           samples, edges);
    return 0;
}

/* ---- the parts alone -------------------------------------------------- */

/* After any run the sample count is floor(ticks x den / num) (EL §6.1),
 * across the 32-bit wrap too. */
static int test_count(void) {
    static pcm_t p;
    const uint32_t starts[] = { 0u, 0xFFF00000u };
    for (unsigned i = 0; i < 2u; i++) {
        pcm_init(&p, starts[i], COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, 0, 0);
        CHECK(p.num == 91636352u && p.den == 234375u, "ticks a sample %u/%u", p.num, p.den);
        uint64_t total = 0, made = 0;
        int16_t buf[COCO_AUDIO_BUF_LEN];
        for (int k = 0; k < 20000; k++) {
            total += 1u + rnd(238944u);
            pcm_set_level(&p, starts[i] + (uint32_t)total, rnd(COCO_SOUND_LEVEL_MAX + 1u));
            made += pcm_drain(&p, buf, COCO_AUDIO_BUF_LEN);
        }
        uint64_t want = total * p.den / p.num;
        CHECK(made == want, "from $%08X: %llu samples after %llu ticks, not %llu", starts[i],
              (unsigned long long)made, (unsigned long long)total, (unsigned long long)want);
        CHECK(p.overflow == 0, "overflowed");
        if (i == 1) CHECK((uint64_t)starts[i] + total > 0xFFFFFFFFull, "the run did not wrap");
    }
    return 0;
}

/* The DC blocker: held high or held low, the output comes to rest at 0,
 * and a step swings no further than full scale. */
static int test_dc_block(void) {
    static pcm_t p;
    int16_t buf[COCO_AUDIO_BUF_LEN];
    for (int lv = 0; lv < 2; lv++) {
        pcm_init(&p, 0, COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, 0, 0);
        uint32_t t = 0;
        int peak = 0, last = 0;
        pcm_set_level(&p, 0, lv ? COCO_SOUND_LEVEL_MAX : 0);
        for (int f = 0; f < 240; f++) {   /* four seconds of fields */
            t += 238944u;
            pcm_advance(&p, t);
            size_t n = pcm_drain(&p, buf, COCO_AUDIO_BUF_LEN);
            for (size_t i = 0; i < n; i++) {
                if (abs(buf[i]) > peak) peak = abs(buf[i]);
                last = buf[i];
            }
        }
        CHECK(last == 0, "held %s, the output rests at %d, not 0", lv ? "high" : "low", last);
        CHECK(peak <= PCM_FULL_SCALE, "the step swung to %d", peak);
        if (lv) CHECK(peak > PCM_FULL_SCALE / 2, "the step barely moved the output (%d)", peak);
    }
    return 0;
}

/* PIA1 PB1, an input, reads the bus back, low at 0.3 of full scale or
 * more (XRoar's single_bit_feedback; coco.c). Here through the I/O page
 * by hand, with the multiplexer on the DAC: the DAC high, then low. */
static int test_feedback(void) {
    make_script(0);
    start(false);
    coco_run_field(&g_m);   /* past the first FS rise: the sound has fed PB1 */
    coco_slow_write(&g_m, 0xFF23, 0x34);   /* PIA1 B: data, CB2 low */
    coco_slow_write(&g_m, 0xFF21, 0x30);   /* PIA1 A: the DDR */
    coco_slow_write(&g_m, 0xFF20, 0xFC);
    coco_slow_write(&g_m, 0xFF21, 0x34);
    coco_slow_write(&g_m, 0xFF01, 0x34);   /* CA2 low */
    coco_slow_write(&g_m, 0xFF03, 0x34);   /* CB2 low: the DAC */
    coco_slow_write(&g_m, 0xFF23, 0x3C);   /* the multiplexer on */
    struct { uint8_t dac; bool low; } cases[] = {
        { 0xFC, true }, { 0x00, false }, { 0x40, false }, { 0x44, true },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        coco_slow_write(&g_m, 0xFF20, cases[i].dac);
        uint8_t pb = coco_slow_read(&g_m, 0xFF22);
        CHECK(((pb & 0x02u) == 0) == cases[i].low, "DAC $%02X: PB1 reads %s", cases[i].dac,
              (pb & 0x02u) ? "high" : "low");
    }
    /* The multiplexer off with the single bit an input: the bus holds. */
    coco_slow_write(&g_m, 0xFF20, 0xFC);
    uint32_t held = g_m.sound_level;
    coco_slow_write(&g_m, 0xFF23, 0x34);
    CHECK(g_m.sound_level == held, "the bus moved when nothing drove it: %u, not %u",
          g_m.sound_level, held);
    coco_slow_write(&g_m, 0xFF20, 0x00);
    CHECK(g_m.sound_level == held, "the DAC moved the bus with the multiplexer off");
    return 0;
}

int main(void) {
    if (test_count()) return 1;
    if (test_dc_block()) return 1;
    if (test_feedback()) return 1;
    if (test_scripts()) return 1;
    TEST_DONE();
}
