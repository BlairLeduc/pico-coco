/* test_smartwatch.c — the DS1216 SmartWatch at $FF78-$FF7F
 * (smartwatch.h, design.md §12.7).
 *
 * The calendar against dates worked out apart from it; then the chip
 * through the machine's own reads, as clock_tc3 drives it: the key a
 * bit a read of $FF78 or $FF79, then 64 bits read at $FF7C, or written.
 * The controls: not fitted, a wrong bit, and a read part-way through the
 * key, none of which opens it. test_os9 boots the module itself.
 */

#include <string.h>

#include "bus.h"
#include "coco.h"
#include "smartwatch.h"
#include "test_util.h"

static coco_t m;

static void send_byte(uint8_t b) {
    for (unsigned i = 0; i < 8u; i++) (void)bus_read(&m, (uint16_t)(0xFF78u | ((b >> i) & 1u)));
}

static void send_key(void) {
    static const uint8_t key[8] = { 0xC5, 0x3A, 0xA3, 0x5C, 0xC5, 0x3A, 0xA3, 0x5C };
    for (unsigned i = 0; i < 8u; i++) send_byte(key[i]);
}

/* 64 bits read at $FF7C, D0 each. */
static void read_regs(uint8_t r[8]) {
    for (unsigned i = 0; i < 8u; i++) {
        r[i] = 0;
        for (unsigned b = 0; b < 8u; b++) r[i] |= (uint8_t)((bus_read(&m, 0xFF7Cu) & 1u) << b);
    }
}

static bool regs_are(const uint8_t r[8], uint8_t s, uint8_t mi, uint8_t h, uint8_t dow, uint8_t d,
                     uint8_t mo, uint8_t y) {
    return r[0] == 0 && r[1] == s && r[2] == mi && r[3] == h && r[4] == dow && r[5] == d &&
           r[6] == mo && r[7] == y;
}

int main(void) {
    /* ---- the calendar ------------------------------------------------- */
    {
        smartwatch_time_t t = { 2000, 1, 1, 0, 0, 0 };
        CHECK(smartwatch_seconds(&t) == 0u, "2000-01-01 is 0");
        smartwatch_time_t a = { 2026, 11, 17, 13, 45, 30 }, b = { 2024, 2, 29, 23, 59, 59 },
                          c = { 2059, 12, 31, 23, 59, 59 }, e = { 1900, 1, 1, 0, 0, 0 },
                          f = { 1999, 12, 31, 23, 59, 0 };
        CHECK(smartwatch_seconds(&a) == 848238330, "2026-11-17 13:45:30");
        CHECK(smartwatch_seconds(&b) == 762566399, "2024-02-29, a leap day");
        CHECK(smartwatch_seconds(&c) == SMARTWATCH_LAST, "2059-12-31, the last");
        CHECK(smartwatch_seconds(&e) == SMARTWATCH_FIRST, "1900-01-01, the first");
        CHECK(smartwatch_seconds(&f) == -60, "1999-12-31 23:59, before 2000");
        unsigned bad = 0;
        for (int64_t s = SMARTWATCH_FIRST; s <= SMARTWATCH_LAST; s += 86400 * 13 + 3607) {
            smartwatch_time_t u;
            smartwatch_civil(s, &u);
            if (smartwatch_seconds(&u) != s) bad++;
        }
        CHECK(bad == 0, "civil and back, every thirteenth day, 1900 to 2059: %u wrong", bad);
    }

    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    uint8_t r[8];

    /* ---- not fitted: the bus, and the key does nothing ----------------- */
    send_key();
    CHECK(bus_read(&m, 0xFF7Cu) == 0xFFu && bus_read(&m, 0xFF78u) == 0xFFu,
          "not fitted: $FF78-$FF7F read the bus");

    m.clock.fitted = true;
    smartwatch_set_host(&m.clock, 848238330u);   /* 2026-11-17 13:45:30, a Tuesday */

    /* ---- shut, it reads the bus ----------------------------------------- */
    read_regs(r);
    CHECK(!memcmp(r, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8), "shut: D0 is the bus's");

    /* ---- the key, then the time --------------------------------------- */
    send_key();
    CHECK(m.clock.open, "the key opens it");
    CHECK((bus_read(&m, 0xFF7Cu) & 0xFEu) == 0xFEu, "D7-D1 are the bus's");
    m.clock.bit = 0;   /* that read was the first bit: start again */
    read_regs(r);
    /* The year as years since 1900, as OS-9 Level One 2.01 keeps it:
     * 126, tens digit 12, $C6 (smartwatch.h). */
    CHECK(regs_are(r, 0x30, 0x45, 0x13, 3, 0x17, 0x11, 0xC6),
          "the registers: %02X %02X %02X %02X %02X %02X %02X %02X", r[0], r[1], r[2], r[3], r[4],
          r[5], r[6], r[7]);
    CHECK(!m.clock.open && bus_read(&m, 0xFF7Cu) == 0xFFu, "64 bits, and it shuts");

    /* ---- the controls: a wrong bit, a read in the key ------------------ */
    send_byte(0xC5);
    send_byte(0x3B);   /* one bit wrong */
    for (unsigned i = 2; i < 8u; i++) send_byte(i & 1u ? 0x3A : (i & 2u ? 0xA3 : 0xC5));
    CHECK(!m.clock.open, "a wrong bit in the key: shut");
    send_byte(0xC5); send_byte(0x3A); send_byte(0xA3); send_byte(0x5C);
    (void)bus_read(&m, 0xFF7Du);
    send_byte(0xC5); send_byte(0x3A); send_byte(0xA3); send_byte(0x5C);
    CHECK(!m.clock.open, "a read half-way through the key: shut");
    send_key();
    CHECK(m.clock.open, "the whole key again: open");
    read_regs(r);

    /* ---- a time written, kept as an offset ------------------------------ */
    send_key();
    static const uint8_t set[8] = { 0x00, 0x05, 0x04, 0x03, 0x01, 0x02, 0x01, 0x99 };
    for (unsigned i = 0; i < 8u; i++) send_byte(set[i]);   /* 1999-01-02 03:04:05 */
    CHECK(!m.clock.open && m.clock.sets == 1u, "64 bits written: a set");
    smartwatch_set_host(&m.clock, 848238330u + 10u);
    send_key();
    read_regs(r);
    CHECK(regs_are(r, 0x15, 0x04, 0x03, 7, 0x02, 0x01, 0x99),
          "the set time, 10 s on: %02X %02X %02X %02X %02X %02X %02X %02X", r[0], r[1], r[2], r[3],
          r[4], r[5], r[6], r[7]);

    /* A written time that is not a date is not taken. */
    send_key();
    static const uint8_t junk[8] = { 0x00, 0x75, 0x04, 0x03, 0x01, 0x02, 0x01, 0x99 };
    for (unsigned i = 0; i < 8u; i++) send_byte(junk[i]);
    CHECK(m.clock.sets == 1u, "75 seconds: not taken");

    /* 2026 as 2.01's SETIME writes it, $C6, and read back so. */
    send_key();
    static const uint8_t y2026[8] = { 0x00, 0x00, 0x00, 0x12, 0x03, 0x01, 0x01, 0xC6 };
    for (unsigned i = 0; i < 8u; i++) send_byte(y2026[i]);
    send_key();
    read_regs(r);
    CHECK(m.clock.sets == 2u && regs_are(r, 0x00, 0x00, 0x12, 5, 0x01, 0x01, 0xC6),
          "2026-01-01 12:00 set as $C6, read back: %02X %02X %02X %02X %02X %02X %02X %02X", r[0],
          r[1], r[2], r[3], r[4], r[5], r[6], r[7]);

    TEST_DONE();
}
