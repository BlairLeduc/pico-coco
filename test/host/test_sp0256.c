/* test_sp0256.c — the SP0256 (sp0256.h, design.md §12.9).
 *
 * Without any ROM of the user's: a mask ROM of the test's own, an
 * allophone of one LOADALL and a return, held to the manual's account of
 * the chip: ALD takes the allophone and drops LRQ until the sequencer
 * picks it up; SBY falls with ALD and rises when the sequencer halts; a
 * block lasts its repeat count of pitch periods of PR + 1 samples, its
 * first sample the amplitude's pulse through the filter, A x 2^E; a
 * second allophone written while LRQ is low is dropped; the pitch
 * interpolated; JSR's return to the byte after it.
 *
 * With sp0256-al2.bin in the ROM directory and MAME's sp0256.cpp fetched
 * into reference/mame (tools/mame/fetch-mame.sh), the oracle: every one
 * of the AL2's 64 allophones, alone and run on into the next, ours
 * against MAME's on every sample and every LRQ and SBY. MAME's file is
 * compiled here through a shim (mame_shim/emu.h) as a tool (D2, D8).
 * Skipped without either; without MAME's file the test is built without
 * the oracle (PICO_COCO_MAME_SP0256 undefined).
 *
 * Planted one at a time (2026-11-24), each failed: a delta not
 * sign-extended, the noise's taps, LRQ not raised as an allophone is
 * taken (the oracle and the own ROM), the pitch not interpolated, JSR's
 * return not rounded to a byte (the own ROM alone: the AL2's allophones
 * never interpolate the pitch, and return from byte boundaries). One
 * coefficient of the quantisation table off by one passed: the AL2 never
 * uses it.
 */

#include <stdio.h>
#include <string.h>

#include "guest.h"
#include "sp0256.h"
#include "test_util.h"
#ifdef PICO_COCO_MAME_SP0256
#include "mame_sp0256.h"
#endif

static uint8_t rom[SP0256_ROM_SIZE];
static sp0256_t sp;

/* Bits into the ROM, least significant first, from byte `at`. */
static unsigned bitpos;
static void put_bits(unsigned v, unsigned n) {
    for (unsigned i = 0; i < n; i++, bitpos++)
        if (v >> i & 1u) rom[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7u));
}

static int test_own(void) {
    /* Allophone 0 at $1000: LOADALL (fetched as 8, GI's 0001) repeat 3,
     * mode 00: AM PR B0 F0 ... B5 F5, eight bits each; then RTS, halting
     * (immediate 0, opcode 0). AM = %011 00101: 5 << 3 = 40. PR = 9: a
     * pulse every 10 samples. The coefficients 0: the filter passes the
     * pulse alone. */
    memset(rom, 0, sizeof rom);
    bitpos = 0;
    put_bits(3, 4);        /* the repeat count                 */
    put_bits(8, 4);        /* LOADALL                          */
    put_bits(0x65, 8);     /* AM                               */
    put_bits(9, 8);        /* PR                               */
    for (int i = 0; i < 12; i++) put_bits(0, 8);
    put_bits(0, 4);        /* RTS to 0: halt                   */
    put_bits(0, 4);

    sp0256_reset(&sp);
    CHECK(sp.lrq && sp.sby, "after RESET: LRQ %d SBY %d, not both high", sp.lrq, sp.sby);
    sp0256_ald(&sp, 0);
    CHECK(!sp.lrq && !sp.sby, "after ALD: LRQ %d SBY %d, not both low", sp.lrq, sp.sby);
    sp0256_ald(&sp, 5);
    CHECK(sp.ald == 0, "a second ALD while LRQ was low was taken: %u", sp.ald);

    int16_t s[64];
    for (int i = 0; i < 64; i++) s[i] = sp0256_sample(&sp, rom);
    CHECK(sp.allophones == 1, "%u allophones taken, not 1", sp.allophones);
    /* Three periods of ten, pulses at 0, 10, 20 of 40; then the halted
     * chip, SBY high, LRQ high. */
    CHECK(s[0] == 40 && s[10] == 40 && s[20] == 40, "pulses %d %d %d, not 40 at 0, 10 and 20", s[0], s[10], s[20]);
    unsigned others = 0;
    for (int i = 1; i < 30; i++) if (i % 10 && s[i]) others++;
    CHECK(others == 0, "%u samples between the pulses not 0", others);
    CHECK(sp.sby && sp.lrq, "after the allophone: SBY %d LRQ %d, not both high", sp.sby, sp.lrq);
    printf("own ROM: a LOADALL of 3 periods of 10 samples, pulses of 40, then SBY\n");

    /* Interpolation: SETMODE 01 (immediate 8), then LOADALL repeat 3 with
     * IA 0 and IP 2: each pulse adds 2 to PR, so the pulses fall at 0,
     * 10, 22 rather than 0, 10, 20. */
    memset(rom, 0, sizeof rom);
    bitpos = 0;
    put_bits(8, 4); put_bits(1, 4);              /* SETMODE, mode 01   */
    put_bits(3, 4); put_bits(8, 4);              /* LOADALL repeat 3   */
    put_bits(0x65, 8); put_bits(9, 8);
    for (int i = 0; i < 12; i++) put_bits(0, 8);
    put_bits(0, 8); put_bits(2, 8);              /* IA 0, IP 2         */
    put_bits(0, 8);                              /* RTS: halt          */
    sp0256_reset(&sp);
    sp0256_ald(&sp, 0);
    for (int i = 0; i < 40; i++) s[i] = sp0256_sample(&sp, rom);
    CHECK(s[0] == 40 && s[10] == 40 && s[22] == 40 && s[20] == 0,
          "interpolated pulses at 0, 10, 22: %d %d %d (and %d at 20)", s[0], s[10], s[22], s[20]);

    /* JSR returns to the byte after it. LOAD_4 (fetched as 2) repeat 1,
     * 37 bits of fields, leaves the JSR at bit 45; its return is bit 61,
     * rounded up to 64. At $1010 a LOADALL of AM 48 and a return; at
     * byte 8 a LOADALL of AM 56 and a halt. AM is a mantissa and an
 * exponent, (AM & 31) << (AM >> 5). */
    memset(rom, 0, sizeof rom);
    bitpos = 0;
    put_bits(1, 4); put_bits(2, 4);              /* LOAD_4 repeat 1    */
    put_bits(0x19, 6); put_bits(4, 8);           /* AM $64: 4 << 3 = 32, PR 4 */
    put_bits(0, 4); put_bits(0, 6); put_bits(0, 7); put_bits(0, 6);
    put_bits(0, 4); put_bits(0xD, 4); put_bits(0x08, 8);   /* JSR $1010 */
    bitpos = 64;
    put_bits(1, 4); put_bits(8, 4);              /* LOADALL repeat 1   */
    put_bits(0x67, 8); put_bits(4, 8);           /* AM 7 << 3 = 56     */
    for (int i = 0; i < 12; i++) put_bits(0, 8);
    put_bits(0, 8);                              /* RTS: halt          */
    bitpos = 128;
    put_bits(1, 4); put_bits(8, 4);              /* LOADALL repeat 1   */
    put_bits(0x66, 8); put_bits(4, 8);           /* AM 6 << 3 = 48     */
    for (int i = 0; i < 12; i++) put_bits(0, 8);
    put_bits(0, 8);                              /* RTS: back          */
    sp0256_reset(&sp);
    sp0256_ald(&sp, 0);
    int16_t seen[3] = { 0, 0, 0 };
    unsigned n_seen = 0;
    for (int i = 0; i < 60 && n_seen < 3; i++) {
        int16_t v = sp0256_sample(&sp, rom);
        if (v) seen[n_seen++] = v;
    }
    CHECK(n_seen == 3 && seen[0] == 32 && seen[1] == 48 && seen[2] == 56,
          "JSR: pulses %d %d %d, not 32, 48 from the subroutine, then 56 after it", seen[0], seen[1], seen[2]);
    printf("own ROM: the pitch interpolated, and JSR's return to the next byte\n");
    return 0;
}

#ifdef PICO_COCO_MAME_SP0256
/* Allophones fed whenever both chips have LRQ high, `n` samples. */
static int run_both(const uint8_t *al, size_t n_al, unsigned samples, const char *what) {
    sp0256_reset(&sp);
    mame_sp0256_init(rom);
    size_t k = 0;
    for (unsigned i = 0; i < samples; i++) {
        int lrq = mame_sp0256_lrq();
        CHECK(lrq == sp.lrq, "%s, sample %u: LRQ %d, MAME's %d", what, i, sp.lrq, lrq);
        CHECK(mame_sp0256_sby() == sp.sby, "%s, sample %u: SBY %d, MAME's %d", what, i, sp.sby,
              mame_sp0256_sby());
        if (k < n_al && lrq) {
            sp0256_ald(&sp, al[k]);
            mame_sp0256_ald(al[k]);
            k++;
        }
        int16_t a = mame_sp0256_sample(), b = sp0256_sample(&sp, rom);
        CHECK(a == (int16_t)(b * 4), "%s, sample %u: %d, MAME's %d / 4", what, i, b, a);
    }
    return 0;
}

static int test_oracle(void) {
    unsigned total = 0;
    for (unsigned a = 0; a < 64u; a++) {
        char what[32];
        snprintf(what, sizeof what, "allophone %u", a);
        uint8_t one[1] = { (uint8_t)a };
        if (run_both(one, 1, 4000, what)) return 1;
        uint8_t two[3] = { (uint8_t)a, (uint8_t)((a + 21u) & 63u), 4 };
        snprintf(what, sizeof what, "allophones %u, %u, 4", a, two[1]);
        if (run_both(two, 3, 8000, what)) return 1;
        total += 12000;
    }
    /* "COLOR COMPUTER" from the cartridge's manual. */
    static const uint8_t cc[] = { 8, 15, 45, 51, 4, 4, 4, 8, 15, 16, 9, 49, 31, 13, 51, 4 };
    if (run_both(cc, sizeof cc, 30000, "COLOR COMPUTER")) return 1;
    total += 30000;
    printf("oracle: %u samples, every one, and LRQ and SBY, as MAME's\n", total);
    return 0;
}
#endif

int main(void) {
    if (test_own()) return 1;
    const char *dir;
    guest_find_roms(&dir);
    char path[1024];
    snprintf(path, sizeof path, "%s/sp0256-al2.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("oracle: skip, no %s\n", path);
        TEST_DONE();
    }
    size_t got = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(got == sizeof rom, "%s: %zu bytes, not 2 KB", path, got);
#ifdef PICO_COCO_MAME_SP0256
    if (test_oracle()) return 1;
#else
    printf("oracle: skip, no MAME sp0256.cpp in reference/mame (tools/mame/fetch-mame.sh)\n");
#endif
    TEST_DONE();
}
