/* test_ssc.c — the Speech/Sound Cartridge in a CoCo (ssc.h, design.md
 * §12.9).
 *
 * Without ROMs: the mix (ssc_mix), silence at the middle; speech at
 * the AL2's typical level (an RMS of 188) moved 32 levels a unit, twice
 * the first weights' 16, as the owner asked (2026-11-25); the AY at
 * half its scale likewise; rising with each input, odd about the middle,
 * without a step, and short of both rails at the inputs' extremes, the
 * AL2's loudest peak (2,962) bent below its straight line.
 *
 * With Color BASIC, Extended BASIC and the cartridge's two ROMs
 * (pic-7040-510.bin, sp0256-al2.bin) in the ROM directory; skipped
 * without them:
 *   - the manual's own program, typed into BASIC: the multiplexer to the
 *     cartridge, then "I CAN TALK " a byte at a time, each after BUSY
 *     reads high, and CR. The cartridge takes every byte, the SP0256
 *     speaks the firmware's allophones, the status shows it speaking
 *     and then not, and the speech is heard; the same program with the
 *     multiplexer left on the DAC (the control) speaks unheard;
 *   - $FF7D, 1 then 0, resets the cartridge mid-sentence: silence;
 *   - in a Multi-Pak beside Disk BASIC, the cartridge in slot 2 and the
 *     disk controller in slot 4 with the switch at it, as The Interbank
 *     Incident wants them: Disk BASIC starts, and the cartridge speaks;
 *   - a save state taken mid-sentence (snapshot.h, version 3), loaded
 *     into a machine doing something else: the two run on alike, the
 *     cartridge's every register and sample; a state with the
 *     cartridge's clock moved is refused, and one from a machine without
 *     the cartridge will not load into one with it;
 *   - the idle 7040's repeated passes skipped and the standing-by
 *     SP0256's samples taken at once end where stepping every one does.
 *
 * Planted one at a time (2026-11-25), each failed: a lost noise register
 * and a lost RAM in the save state; a skip 8 cycles long; a settled
 * pitch pulse a sample long; and a pass with an interrupt in it taken
 * for a cycle (the bug the skipping first had); in the mix (the same
 * day, later), the first weights and a hard clip in place of the knee.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "guest.h"
#include "snap_util.h"
#include "test_util.h"

static guest_t g;
static uint8_t pic[TMS7000_ROM_SIZE], al2[SP0256_ROM_SIZE];

static bool load(const char *dir, const char *name, uint8_t *dst, size_t len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(dst, 1, len, f);
    fclose(f);
    return got == len;
}

static void build_ssc(void) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = 64;
    cfg.cart = CART_SSC;
    coco_init(&g.m, &cfg);
    coco_load_rom(&g.m, 0xA000u, guest_rom_image(ROM_COCO_BAS13), romset_images[ROM_COCO_BAS13].size);
    coco_load_rom(&g.m, 0x8000u, guest_rom_image(ROM_COCO_EXTBAS11),
                  romset_images[ROM_COCO_EXTBAS11].size);
    coco_load_ssc_roms(&g.m, pic, al2);
    coco_power_on(&g.m);
    keymatrix_init(&g.k);
    g.ok_ticks = 0;
    g.after_field = NULL;
}

/* Fields drained, the samples' spread over them: what was heard. */
static int field_spread(void) {
    static int16_t buf[4096];
    int lo = 0, hi = 0;
    bool first = true;
    size_t n;
    while ((n = coco_audio_drain(&g.m, buf, 4096)) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (first || buf[i] < lo) lo = buf[i];
            if (first || buf[i] > hi) hi = buf[i];
            first = false;
        }
    }
    return hi - lo;
}

typedef struct {
    unsigned speaking_fields, heard_fields;
    uint32_t allophones;
} talk_t;

/* Type the manual's program, with the multiplexer's source `src`
 * (PIA0 CB2 written to $FF03: 63 sets the cartridge, 52 leaves the DAC),
 * run it, and watch `fields` fields. */
static talk_t talk(int src_ff03, int fields) {
    char line[80];
    guest_type(&g, "10 X=&HFF00:Y=&HFF7E\r");
    snprintf(line, sizeof line, "20 POKE X+1,52:POKE X+3,%d:POKE X+35,60\r", src_ff03);
    guest_type(&g, line);
    guest_type(&g, "30 A$=\"I CAN TALK \"\r");
    guest_type(&g, "40 FOR I=1 TO LEN(A$)\r");
    guest_type(&g, "50 IF (PEEK(Y) AND 128)=0 THEN 50\r");
    guest_type(&g, "60 POKE Y,ASC(MID$(A$,I,1)):NEXT I\r");
    guest_type(&g, "70 IF (PEEK(Y) AND 128)=0 THEN 70\r");
    guest_type(&g, "80 POKE Y,13\r");
    (void)field_spread();
    talk_t t = { 0, 0, 0 };
    uint32_t a0 = g.m.port.ssc.sp.allophones;
    guest_type(&g, "RUN\r");
    for (int f = 0; f < fields; f++) {
        guest_fields(&g, 1);
        if (!g.m.port.ssc.sp.sby) t.speaking_fields++;
        if (field_spread() > 1000) t.heard_fields++;
    }
    t.allophones = g.m.port.ssc.sp.allophones - a0;
    return t;
}

static int test_basic(void) {
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot with the cartridge in the port");
    CHECK(bus_read(&g.m, 0xFF7E) == 0xFF, "the idle status reads %02X, not $FF", bus_read(&g.m, 0xFF7E));
    talk_t t = talk(63, 240);
    CHECK(guest_find_row(&g.m, "OK", 6) >= 0, "the program did not end at OK");
    CHECK(g.m.port.ssc.busy == false, "BUSY still set after the program");
    CHECK(t.allophones >= 10u, "only %u allophones spoken for I CAN TALK", t.allophones);
    CHECK(t.speaking_fields > 30u && t.speaking_fields < 200u, "speaking for %u fields", t.speaking_fields);
    CHECK(g.m.port.ssc.sp.sby, "still speaking 240 fields on");
    CHECK(t.heard_fields > 30u, "heard in only %u fields", t.heard_fields);
    printf("basic: I CAN TALK, %u allophones, speaking %u fields, heard in %u\n", t.allophones,
           t.speaking_fields, t.heard_fields);

    /* The control: the multiplexer left on the DAC. */
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot");
    talk_t c = talk(52, 240);
    CHECK(c.allophones >= 10u, "the control spoke only %u allophones", c.allophones);
    CHECK(c.heard_fields == 0, "speech heard in %u fields with the multiplexer on the DAC", c.heard_fields);

    /* RESET through $FF7D mid-sentence: the speech stops. */
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot");
    guest_type(&g, "POKE &HFF01,52:POKE &HFF03,63:POKE &HFF23,60\r");
    const char *text = "I CAN TALK ";
    for (const char *p = text; *p; p++) {
        while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
        bus_write(&g.m, 0xFF7E, (uint8_t)*p);
        guest_fields(&g, 1);
    }
    while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
    bus_write(&g.m, 0xFF7E, 13);
    unsigned wait = 0;
    while (g.m.port.ssc.sp.sby && wait < 120) { guest_fields(&g, 1); wait++; }
    CHECK(!g.m.port.ssc.sp.sby, "never started speaking");
    guest_fields(&g, 10);
    uint32_t spoken = g.m.port.ssc.allophones;
    bus_write(&g.m, 0xFF7D, 1);
    bus_write(&g.m, 0xFF7D, 0);
    /* The firmware starts with one short pause (allophone 0), then
     * stands by. */
    wait = 0;
    while (!g.m.port.ssc.sp.sby && wait < 30) { guest_fields(&g, 1); wait++; }
    CHECK(g.m.port.ssc.sp.sby, "still speaking 30 fields after $FF7D's reset");
    CHECK(g.m.port.ssc.allophones - spoken <= 2u, "%u allophones loaded after the reset",
          g.m.port.ssc.allophones - spoken);
    (void)field_spread();
    guest_fields(&g, 30);
    CHECK(field_spread() < 300, "sound after $FF7D's reset");
    CHECK(bus_read(&g.m, 0xFF7E) == 0xFF, "the status after reset reads %02X", bus_read(&g.m, 0xFF7E));
    return 0;
}

static int test_mpi(void) {
    cart_kind_t kinds[COCO_MPI_SLOTS] = { CART_NONE, CART_SSC, CART_NONE, CART_RSDOS };
    guest_build_mpi(&g, 64, kinds, 3, NULL, NULL);
    CHECK(coco_load_ssc_roms(&g.m, pic, al2), "the cartridge's ROMs did not fit beside Disk BASIC");
    coco_power_on(&g.m);
    CHECK(guest_boot(&g), "Disk BASIC did not boot beside the cartridge");
    CHECK(guest_find_row(&g.m, "DISK EXTENDED COLOR BASIC", 0) >= 0, "not Disk BASIC's banner");
    talk_t t = talk(63, 240);
    CHECK(t.allophones >= 10u && t.heard_fields > 30u,
          "in slot 2 beside Disk BASIC: %u allophones, heard in %u fields", t.allophones, t.heard_fields);
    printf("multi-pak: Disk BASIC in slot 4, the cartridge in slot 2: %u allophones, heard in %u fields\n",
           t.allophones, t.heard_fields);
    return 0;
}

static guest_t h;
static mem_t snap;
static int16_t pcm_a[200000], pcm_b[200000];

static size_t drain_all(coco_t *m, int16_t *dst, size_t max) {
    size_t n = 0, k;
    while ((k = coco_audio_drain(m, dst + n, max - n)) > 0) n += k;
    return n;
}

static int test_snapshot(void) {
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot");
    guest_type(&g, "POKE &HFF01,52:POKE &HFF03,63:POKE &HFF23,60\r");
    for (const char *p = "I CAN TALK "; *p; p++) {
        while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
        bus_write(&g.m, 0xFF7E, (uint8_t)*p);
        guest_fields(&g, 1);
    }
    while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
    bus_write(&g.m, 0xFF7E, 13);
    guest_fields(&g, 40);   /* mid-sentence */
    CHECK(!g.m.port.ssc.sp.sby, "not speaking when the state was taken");
    CHECK(mem_save(&snap, &g.m, NULL) == SNAP_OK, "the save with the cartridge speaking");

    /* The second machine, the same port, a different sentence under way. */
    h = g;
    coco_copy(&h.m, &g.m);
    bus_write(&h.m, 0xFF7D, 1);
    bus_write(&h.m, 0xFF7D, 0);
    for (int f = 0; f < 5; f++) coco_run_field(&h.m);
    for (const char *p = "HELLO THERE, WORLD.\r"; *p; p++) {
        while (!(bus_read(&h.m, 0xFF7E) & 0x80)) coco_run_field(&h.m);
        bus_write(&h.m, 0xFF7E, (uint8_t)*p);
        coco_run_field(&h.m);
    }
    for (int f = 0; f < 20; f++) coco_run_field(&h.m);
    CHECK(memcmp(h.m.port.ssc.ram, g.m.port.ssc.ram, SSC_RAM_SIZE) != 0,
          "the second machine's cartridge RAM is the first's: the load proves nothing of it");
    CHECK(mem_check(&snap, &h.m, NULL, NULL) == SNAP_OK, "check refused the state");
    CHECK(mem_load(&snap, &h.m) == SNAP_OK, "load refused the state");
    /* A load starts the sound's sample grid again at its tick (snapshot.h):
     * the first machine takes its own state back too, so the two grids
     * stand alike. */
    CHECK(mem_load(&snap, &g.m) == SNAP_OK, "the first machine refused its own state");
    /* The DC blocker's memory is not state either: off, for the sum. */
    g.m.pcm.dc_block = h.m.pcm.dc_block = false;
    (void)drain_all(&g.m, pcm_a, 200000);
    (void)drain_all(&h.m, pcm_b, 200000);
    size_t na = 0, nb = 0;
    for (int f = 0; f < 120; f++) {
        coco_run_field(&g.m);
        coco_run_field(&h.m);
        na += drain_all(&g.m, pcm_a + na, 200000 - na);
        nb += drain_all(&h.m, pcm_b + nb, 200000 - nb);
    }
    const ssc_t *a = &g.m.port.ssc, *b = &h.m.port.ssc;
    CHECK(a->cpu.pc == b->cpu.pc && a->cpu.cycles == b->cpu.cycles && a->cpu.st == b->cpu.st &&
          memcmp(a->cpu.rf, b->cpu.rf, sizeof a->cpu.rf) == 0, "the TMS7040s parted after the load");
    CHECK(memcmp(a->ram, b->ram, sizeof a->ram) == 0, "the cartridges' RAMs parted");
    CHECK(a->sp.samples == b->sp.samples && a->sp.out == b->sp.out && a->allophones == b->allophones,
          "the SP0256s parted: %u/%u samples", a->sp.samples, b->sp.samples);
    CHECK(na == nb && na > 1000u && memcmp(pcm_a, pcm_b, na * sizeof pcm_a[0]) == 0,
          "the sound parted after the load (%zu and %zu samples)", na, nb);
    printf("snapshot: restored mid-sentence, the same 120 fields on: %u allophones, %zu samples\n",
           a->allophones, na);

    /* The control: the cartridge's clock moved a long way is refused. */
    CHECK(mem_save(&snap, &g.m, NULL) == SNAP_OK, "save");
    unsigned off = SNAP_HEADER_LEN + SNAP_STATE_LEN + SNAP_PORT_LEN;
    snap.buf[off + 1] ^= 0x01u;           /* the PC: any value will do */
    mem_recrc(&snap);
    CHECK(mem_check(&snap, &h.m, NULL, NULL) == SNAP_OK, "a changed PC was refused");
    CHECK(mem_save(&snap, &g.m, NULL) == SNAP_OK, "save");
    /* cycle0, after the CPU's 256 + 30 bytes and the board's 2 KB + 6. */
    unsigned c0 = off + 1 + 2 + 1 + 1 + 256 + 7 + 4 + 4 + 3 + 1 + 8 + 8 + 4 + SSC_RAM_SIZE + 4 + 1 + 1;
    snap.buf[c0 + 3] ^= 0x10u;
    mem_recrc(&snap);
    CHECK(mem_check(&snap, &h.m, NULL, NULL) == SNAP_NOT_SNAPSHOT, "a cartridge's clock far off was taken");

    /* A machine without the cartridge: its state will not load here. */
    guest_build(&h, 64, true);
    for (int f = 0; f < 30; f++) coco_run_field(&h.m);
    CHECK(mem_save(&snap, &h.m, NULL) == SNAP_OK, "save without the cartridge");
    CHECK(mem_check(&snap, &g.m, NULL, NULL) == SNAP_OTHER_MACHINE,
          "a state without the cartridge would load into a machine with one");
    return 0;
}

/* The idle 7040's passes skipped and the standing-by SP0256's samples
 * taken at once (ssc.c) end where stepping every one does: two machines,
 * one exact, through idling, speech and idling again. */
static guest_t e;
static int test_skip(void) {
    build_ssc();
    e = g;
    coco_copy(&e.m, &g.m);
    e.m.port.ssc.exact = true;
    for (int pass = 0; pass < 2; pass++) {
        coco_t *m = pass ? &e.m : &g.m;
        for (int f = 0; f < 600; f++) coco_run_field(m);
        bus_write(m, 0xFF01, 0x34); bus_write(m, 0xFF03, 0x3F); bus_write(m, 0xFF23, 0x3C);
        for (const char *p = "SPEAK AND SKIP.\r"; *p; p++) {
            while (!(bus_read(m, 0xFF7E) & 0x80)) coco_run_field(m);
            bus_write(m, 0xFF7E, (uint8_t)*p);
            coco_run_field(m);
        }
        for (int f = 0; f < 600; f++) coco_run_field(m);
    }
    const ssc_t *a = &g.m.port.ssc, *b = &e.m.port.ssc;
    CHECK(a->skipped > 1000u && b->skipped == 0, "skipped %u passes, and %u exact", a->skipped, b->skipped);
    CHECK(a->cpu.pc == b->cpu.pc && a->cpu.cycles == b->cpu.cycles &&
          a->cpu.instructions == b->cpu.instructions && a->cpu.t1_dec == b->cpu.t1_dec &&
          a->cpu.t1_pre == b->cpu.t1_pre && a->cpu.t1_phase == b->cpu.t1_phase &&
          memcmp(a->cpu.rf, b->cpu.rf, sizeof a->cpu.rf) == 0,
          "skipping parted the 7040 from stepping: PC %04X/%04X, %llu/%llu instructions", a->cpu.pc,
          b->cpu.pc, (unsigned long long)a->cpu.instructions, (unsigned long long)b->cpu.instructions);
    CHECK(memcmp(&a->sp, &b->sp, sizeof a->sp) == 0 && a->allophones == b->allophones && a->allophones > 8u,
          "skipping parted the SP0256: %u/%u samples", a->sp.samples, b->sp.samples);
    CHECK(g.m.sound_level == e.m.sound_level, "the sound bus parted");
    printf("skip: %u passes skipped, the same 7040, SP0256 and %u allophones as stepping\n",
           a->skipped, a->allophones);
    return 0;
}

/* The mix, from its inputs alone. */
static int test_mix(void) {
    const int32_t mid = (int32_t)SSC_SP_MID;
    CHECK(ssc_mix(0, 0) == SSC_SP_MID, "silence mixed to %u", ssc_mix(0, 0));
    int32_t body = (int32_t)ssc_mix(0, 188) - mid;
    CHECK(body >= 188 * 32 - 2 && body <= 188 * 32, "speech's body moved %d levels", body);
    int32_t ay = (int32_t)ssc_mix(AY_LEVEL_MAX / 4u, 0) - mid;
    CHECK(ay >= 11400 && ay <= 11500, "the AY at a quarter of its scale moved %d levels", ay);

    uint32_t last = ssc_mix(0, -8192);
    int bad_rise = 0, bad_odd = 0, big_step = 0;
    for (int32_t sp = -8191; sp <= 8191; sp++) {
        uint32_t v = ssc_mix(0, sp);
        if (v < last) bad_rise++;
        if (v - last > 33u) big_step++;
        if ((int32_t)v - mid != mid - (int32_t)ssc_mix(0, -sp)) bad_odd++;
        last = v;
    }
    CHECK(bad_rise == 0, "the mix fell %d times as speech rose", bad_rise);
    CHECK(big_step == 0, "the mix stepped %d times", big_step);
    CHECK(bad_odd == 0, "the mix not odd about the middle at %d samples", bad_odd);
    last = 0;
    for (uint32_t a = 0; a <= AY_LEVEL_MAX; a += 7u) {
        uint32_t v = ssc_mix(a, 0);
        if (v < last) bad_rise++;
        last = v;
    }
    CHECK(bad_rise == 0, "the mix fell %d times as the AY rose", bad_rise);

    uint32_t hi = ssc_mix(AY_LEVEL_MAX, 8191), lo = ssc_mix(0, -8192);
    CHECK(hi < SSC_LEVEL_MAX && lo > 0, "the mix reached a rail: %u to %u", lo, hi);
    int32_t peak = (int32_t)ssc_mix(0, 2962) - mid;
    CHECK(peak > (int32_t)SSC_KNEE && peak < 2962 * 32 && peak < (int32_t)SSC_HALF - 2000,
          "the loudest allophone moved %d levels", peak);
    printf("mix: speech's body %d levels, the AY at a quarter %d, the loudest peak %d of %u\n", body,
           ay, peak, SSC_HALF);
    return 0;
}

int main(void) {
    if (test_mix()) return 1;
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: Color BASIC 1.3 and Extended 1.1 not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (!load(dir, "pic-7040-510.bin", pic, sizeof pic) || !load(dir, "sp0256-al2.bin", al2, sizeof al2)) {
        printf("skip: pic-7040-510.bin and sp0256-al2.bin not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (test_basic()) return 1;
    if (test_snapshot()) return 1;
    if (test_skip()) return 1;
    if (guest_have_rom(ROM_COCO_DISK11) && test_mpi()) return 1;
    TEST_DONE();
}
