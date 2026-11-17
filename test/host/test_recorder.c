/* test_recorder.c — the tape's recorder and player with no ROM, so it
 * runs in CI (design.md §12.3; cassette.h).
 *
 * The DAC is driven as the writer drives it, a rise and a fall a bit by
 * the writer's counts, through PIA1 with the relay closed:
 *
 *  - a block with its leader is kept as the trap writes one, and the
 *    player's walk of what was kept gives the same bits back;
 *  - a block whose checksum is wrong is not kept, and is counted, which
 *    no ROM's writer can show;
 *  - a block cut short by the relay, or by a gap no tone has, is not
 *    kept; the control, the same bits whole, is;
 *  - with the relay open, or fast loading on, nothing is recorded;
 *  - PA0 read at a tick is the player's level at that tick: low to the
 *    cycle before the first rise, high from it, low again a high half
 *    on, and the player's place is held while the relay is open.
 */

#include <string.h>

#include "cas.h"
#include "coco.h"
#include "test_util.h"

static coco_t m;
static uint8_t tape[4096], rom[0x2000];

static void machine(bool fast) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    memset(rom, 0x12, sizeof rom);
    coco_load_rom(&m, 0xA000, rom, sizeof rom);
    coco_power_on(&m);
    coco_tape_attach(&m, tape, sizeof tape);
    coco_cassette_set_fast(&m, fast);
    /* PIA1 port A's top six bits outputs, the DAC; then its data. */
    coco_slow_write(&m, 0xFF21u, 0x00u);
    coco_slow_write(&m, 0xFF20u, 0xFCu);
    coco_slow_write(&m, 0xFF21u, 0x34u);
}

static void relay(bool on) {
    coco_slow_write(&m, 0xFF21u, on ? 0x3Cu : 0x34u);
}

static void wait_cycles(uint32_t n) {
    m.cpu.cycles += n;
}

/* One bit as the writer sends it: up, its high half, down, its low. */
static void send_bit(bool one, uint32_t extra) {
    coco_slow_write(&m, 0xFF20u, 0x80u);
    wait_cycles(one ? CAS_HIGH_1 : CAS_HIGH_0);
    coco_slow_write(&m, 0xFF20u, 0x00u);
    wait_cycles((one ? CAS_LOW_1 : CAS_LOW_0) + extra);
}

static void send_byte(uint8_t b) {
    for (unsigned i = 0; i < 8u; i++) send_bit((b >> i) & 1u, i == 7u ? 30u : 0u);
}

/* Leader bytes, then a block of `n` data bytes, its checksum off by
 * `wrong`, then the $55 BLKOUT ends with. `stop_at`, if not 0, is the
 * byte of the block after which to stop. */
static void send_block(unsigned leader, uint8_t type, const uint8_t *data, uint8_t n, uint8_t wrong,
                       unsigned stop_at) {
    for (unsigned i = 0; i < leader; i++) send_byte(CAS_LEADER_BYTE);
    uint8_t blk[2u + 255u + 2u], k = 0;
    blk[k++] = CAS_SYNC;
    blk[k++] = type;
    blk[k++] = n;
    unsigned at = 3;
    memcpy(&blk[at], data, n);
    at += n;
    blk[at++] = (uint8_t)(cas_checksum(type, n, data) + wrong);
    blk[at++] = CAS_LEADER_BYTE;
    for (unsigned i = 0; i < at; i++) {
        if (stop_at && i == stop_at) return;
        send_byte(blk[i]);
    }
}

int main(void) {
    static const uint8_t data[5] = { 'H', 'E', 'L', 'L', 'O' };

    /* ---- a block kept, and played back ------------------------------- */
    {
        machine(false);
        relay(true);
        wait_cycles(1000);
        send_block(20, CAS_BLOCK_DATA, data, 5, 0, 0);
        const coco_tape_t *t = &m.tape;
        CHECK(t->inserted && t->len == 20u + 4u + 5u && m.cas.rec_blocks == 1 &&
                  m.cas.rec_errors == 0,
              "a block as it is sent: %u bytes kept, %u blocks, %u errors", t->len,
              m.cas.rec_blocks, m.cas.rec_errors);
        relay(false);
        CHECK(t->len == 20u + 4u + 5u + 1u && tape[t->len - 1u] == CAS_LEADER_BYTE,
              "the $55 after it is kept when the relay opens: %u bytes", t->len);
        cas_block_t b;
        CHECK(cas_next_block(tape, t->len, 0, &b) && b.at == 20u && b.sum_ok && b.len == 5 &&
                  memcmp(&tape[b.data], data, 5) == 0 && tape[19] == CAS_LEADER_BYTE,
              "the image: twenty $55s, the block, its data");

        /* The walk of it: the silence, then every byte's sixteen halves,
         * the high half a 1's or a 0's by its bit. */
        coco_tape_rewind(&m);
        uint32_t ticks;
        unsigned n = 0, bad = 0;
        CHECK(cassette_walk(&m, &ticks) && ticks == CAS_SILENCE * CAS_CYCLE_TICKS,
              "the walk begins with the silence");
        while (cassette_walk(&m, &ticks)) {
            if (n % 2u == 0) {
                bool one = (tape[n / 16u] >> (n / 2u % 8u)) & 1u;
                bad += ticks != (one ? CAS_HIGH_1 : CAS_HIGH_0) * CAS_CYCLE_TICKS;
            }
            n++;
        }
        CHECK(bad == 0 && n == t->len * 16u - 1u, "the walk: %u halves of %u bytes, %u wrong", n,
              t->len, bad);
    }

    /* ---- a wrong checksum ------------------------------------------- */
    {
        machine(false);
        relay(true);
        send_block(20, CAS_BLOCK_DATA, data, 5, 1, 0);
        relay(false);
        CHECK(m.tape.len == 0 && m.cas.rec_blocks == 0 && m.cas.rec_errors == 1,
              "a wrong checksum: %u bytes kept, %u errors", m.tape.len, m.cas.rec_errors);
        /* A good block after it in the same span is kept. */
        relay(true);
        send_block(20, CAS_BLOCK_DATA, data, 5, 1, 0);
        send_block(4, CAS_BLOCK_DATA, data, 5, 0, 0);
        relay(false);
        CHECK(m.cas.rec_blocks == 1 && m.cas.rec_errors == 2 && m.tape.len == 5u + 4u + 5u + 1u,
              "a good block after a bad one: %u bytes, %u blocks", m.tape.len, m.cas.rec_blocks);
    }

    /* ---- cut short ---------------------------------------------------- */
    {
        machine(false);
        relay(true);
        send_block(20, CAS_BLOCK_DATA, data, 5, 0, 6);   /* into its data */
        relay(false);
        CHECK(m.tape.len == 0 && !m.tape.inserted && m.cas.rec_errors == 1,
              "the relay opened in a block: %u bytes kept, %u errors", m.tape.len,
              m.cas.rec_errors);

        /* A gap no tone has, part-way: the block is given up there, so
         * that the whole one after it is found and kept. */
        machine(false);
        relay(true);
        send_block(20, CAS_BLOCK_DATA, data, 5, 0, 6);
        coco_slow_write(&m, 0xFF20u, 0x80u);
        wait_cycles(5000);
        coco_slow_write(&m, 0xFF20u, 0x00u);
        send_block(4, CAS_BLOCK_DATA, data, 5, 0, 0);
        relay(false);
        CHECK(m.cas.rec_blocks == 1 && m.cas.rec_errors == 1 && m.tape.len == 4u + 4u + 5u + 1u,
              "a gap in a block, then a whole one: %u blocks, %u bytes kept, %u errors",
              m.cas.rec_blocks, m.tape.len, m.cas.rec_errors);
    }

    /* ---- the relay open, and fast loading ----------------------------- */
    {
        machine(false);
        send_block(20, CAS_BLOCK_DATA, data, 5, 0, 0);
        CHECK(m.tape.len == 0 && m.cas.rec_blocks == 0, "recorded with the relay open");
        machine(true);
        relay(true);
        send_block(20, CAS_BLOCK_DATA, data, 5, 0, 0);
        relay(false);
        CHECK(m.tape.len == 0 && m.cas.rec_blocks == 0, "recorded with fast loading on");
    }

    /* ---- PA0, to the cycle ---------------------------------------------- */
    {
        machine(false);
        memset(tape, 0, sizeof tape);
        tape[0] = 0x01u;   /* a 1, then 0s */
        coco_tape_insert(&m, 8);
        #define PA0() (coco_slow_read(&m, 0xFF20u) & 1u)
        CHECK(PA0() == 0, "PA0 is low with the relay open");
        relay(true);
        wait_cycles(CAS_SILENCE - 1u);
        CHECK(PA0() == 0, "PA0 is low a cycle before the first rise");
        wait_cycles(1);
        CHECK(PA0() == 1, "PA0 is high at the first rise");
        wait_cycles(CAS_HIGH_1 - 1u);
        CHECK(PA0() == 1, "PA0 is high a cycle before a 1's fall");
        wait_cycles(1);
        CHECK(PA0() == 0, "PA0 is low at a 1's fall");
        wait_cycles(CAS_LOW_1 - 1u);
        CHECK(PA0() == 0, "PA0 is low a cycle before the next bit, a 0");
        wait_cycles(1);
        CHECK(PA0() == 1, "PA0 is high at the next bit's rise");
        /* The relay open part-way up a 0's high half: a stopped tape
         * gives no signal, however long, and the rest of the half is
         * played when it closes. */
        wait_cycles(100);
        relay(false);
        wait_cycles(100000);
        CHECK(PA0() == 0 && !m.cas.playing, "PA0 is low with the tape stopped");
        relay(true);
        CHECK(PA0() == 1, "the level it stopped at, when it starts again");
        wait_cycles(CAS_HIGH_0 - 100u - 1u);
        CHECK(PA0() == 1, "the rest of the high half, less a cycle");
        wait_cycles(1);
        CHECK(PA0() == 0, "its fall, where the stop left it");
        #undef PA0
    }

    TEST_DONE();
}
