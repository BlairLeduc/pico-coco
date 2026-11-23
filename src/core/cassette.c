/* cassette.c — tape phase 2: the signal (cassette.h, design.md §12.3). */

#include "cassette.h"

#include <string.h>

#include "cas.h"
#include "coco.h"
#include "hot.h"

/* ---- the walk -------------------------------------------------------------- */

/* The cycles a block's writer spends before BLKOUT's first $55, less the
 * 11 a data byte its sum takes (cassette.h), measured on the ROMs'
 * own saves (2026-11-16): by who wrote the file, which its name block
 * says, and by where the block stands. */
enum { W_ALONE, W_CSAVE, W_CSAVEM };
#define K_ALONE         81u   /* WRTLDR then BLKOUT, back to back         */
#define K_ALONE_EMPTY   76u   /* the same with no data to sum             */
#define K_SUM           11u

/* A block's place in the image: its first $55 at `p`. */
static bool block_at(const coco_tape_t *t, uint32_t p, uint8_t *type, uint8_t *n) {
    if (t->len - p < 6u || t->buf[p] != CAS_LEADER_BYTE || t->buf[p + 1u] != CAS_SYNC) return false;
    if (t->len - p - 5u < t->buf[p + 3u]) return false;   /* cut short: bytes as they are */
    *type = t->buf[p + 2u];
    *n = t->buf[p + 3u];
    return true;
}

/* Does a block with data follow the one at `p`, n bytes long? */
static bool more_data(const coco_tape_t *t, uint32_t p, uint8_t n) {
    uint8_t type, n2;
    uint32_t q = p + 6u + n;   /* past its last $55, if it has one */
    if (q > t->len) return false;
    return q < t->len && block_at(t, q, &type, &n2) && type == CAS_BLOCK_DATA && n2 > 0;
}

/* The cycles before the block at `p`, after a leader or after a block. */
static uint32_t block_gap(const cassette_t *c, const coco_tape_t *t, uint32_t p, bool after_leader) {
    uint8_t type = 0, n = 0;
    (void)block_at(t, p, &type, &n);
    uint32_t sum = K_SUM * n;
    if (after_leader && (type == CAS_BLOCK_NAME || c->writer == W_ALONE || n == 0))
        return (n ? K_ALONE : K_ALONE_EMPTY) + sum;
    if (c->writer == W_CSAVEM) {
        if (type == CAS_BLOCK_EOF) return 117u;
        return 106u + (after_leader ? 16u : 0u) + (more_data(t, p, n) ? 0u : 6u) + sum;
    }
    /* CSAVE's, and any other writer's blocks that follow one another. */
    if (type == CAS_BLOCK_EOF) return 107u;
    return 105u + (after_leader ? 15u : 0u) + (n < 255u ? 4u : 0u) + sum;
}

/* The next byte into hand, with the cycles that follow its last bit;
 * false at the tape's end. */
static bool fetch(coco_t *m) {
    cassette_t *c = &m->cas;
    coco_tape_t *t = &m->tape;
    c->have = false;
    c->span_end = false;
    if (!t->inserted || t->pos >= t->len) return false;

    uint8_t type, n;
    if (c->idx == 0 && block_at(t, t->pos, &type, &n)) {
        c->idx = 1;
        c->blk_n = n;
        c->blk_at = 0;
        if (type == CAS_BLOCK_NAME) {
            /* Who wrote the file: its type and its ASCII flag. */
            const uint8_t *d = &t->buf[t->pos + 4u];
            bool ascii = n > 9u && d[9] != 0;
            uint8_t ftype = n > 8u ? d[8] : 1u;
            c->writer = ascii || ftype == 1u ? W_ALONE : ftype == 2u ? W_CSAVEM : W_CSAVE;
        }
    }
    c->byte = t->buf[t->pos++];
    c->bit = 0;
    c->low = false;
    c->have = true;

    if (c->idx) {
        /* $55, sync, type, length, data, checksum, $55. */
        unsigned at = c->blk_at++, last = 5u + c->blk_n;
        if (at == 0)                 c->gap = 25u;
        else if (at <= 2u)           c->gap = 27u;
        else if (at == 3u)           c->gap = c->blk_n ? 34u : 32u;
        else if (at < 3u + c->blk_n) c->gap = 38u;
        else if (at == 3u + c->blk_n) c->gap = 36u;
        else if (at == 4u + c->blk_n) c->gap = 18u;
        if (at < last) return true;
        c->idx = 0;
        if (t->pos < t->len && block_at(t, t->pos, &type, &n)) c->gap = block_gap(c, t, t->pos, false);
        else c->span_end = true;   /* a leader's silence, or the end */
        return true;
    }
    /* A leader's byte, or one of no block. */
    if (t->pos < t->len && block_at(t, t->pos, &type, &n)) c->gap = block_gap(c, t, t->pos, true);
    else c->gap = 33u;
    return true;
}

/* The cycles to the next change, and past it. */
static bool step(coco_t *m, uint32_t *cycles) {
    cassette_t *c = &m->cas;
    if (!c->have) {
        /* Between spans: the silence, then the first byte's rise. */
        if (!fetch(m)) return false;
        *cycles = CAS_SILENCE;
        return true;
    }
    bool one = (c->byte >> c->bit) & 1u;
    if (!c->low) {
        c->low = true;
        *cycles = one ? CAS_HIGH_1 : CAS_HIGH_0;
        return true;
    }
    uint32_t low = one ? CAS_LOW_1 : CAS_LOW_0;
    c->low = false;
    if (++c->bit < 8u) {
        *cycles = low + ((c->byte >> c->bit) & 1u);
        return true;
    }
    /* The byte's end: its caller's cycles, and the next byte's rise, or
     * a silence first, or nothing more. */
    uint32_t gap = c->gap;
    bool span_end = c->span_end;
    if (!fetch(m)) return false;
    if (span_end) {
        *cycles = CAS_SILENCE;
        return true;
    }
    *cycles = low + (c->byte & 1u) + gap;
    return true;
}

bool cassette_walk(coco_t *m, uint32_t *ticks) {
    uint32_t cycles;
    if (!step(m, &cycles)) return false;
    *ticks = cycles * CAS_CYCLE_TICKS;
    return true;
}

/* ---- the player ------------------------------------------------------------- */

static void play_to(coco_t *m, uint64_t t) {
    cassette_t *c = &m->cas;
    while (c->playing && t >= c->next) {
        c->level = !c->level;
        c->edges++;
        uint32_t cycles;
        if (!step(m, &cycles)) {
            c->playing = false;
            c->ended = true;
            c->level = false;
            c->left = 0;
            c->end_reads = 0;
            return;
        }
        c->next += (uint64_t)cycles * CAS_CYCLE_TICKS;
    }
}

/* Run or hold, from the relay and the deck. */
static void update(coco_t *m, uint64_t t) {
    cassette_t *c = &m->cas;
    bool run = !c->fast && c->motor && m->tape.inserted && !c->ended;
    if (run && !c->playing) {
        if (!c->have && c->left == 0) {
            /* From a byte's start: the silence, and then its rise. */
            uint32_t cycles;
            c->level = false;
            if (!step(m, &cycles)) {
                c->ended = true;
                c->end_reads = 0;
                return;
            }
            c->left = cycles * CAS_CYCLE_TICKS;
        }
        c->next = t + c->left;
        c->playing = true;
    } else if (!run && c->playing) {
        /* A span the ROM wrote in leaves the player where it was. */
        if (!c->rec_wrote) {
            play_to(m, t);
            if (!c->playing) return;
            c->left = (uint32_t)(c->next - t);
        }
        c->playing = false;
    }
}

bool cassette_level(coco_t *m, uint64_t t) {
    cassette_t *c = &m->cas;
    /* Not in a span the ROM is writing in: CASOFF reads PIA1 on its
     * way to opening the relay. */
    if (c->playing) {
        if (!c->rec_wrote) play_to(m, t);
    } else if (c->ended && c->motor && !c->fast && m->tape.op == TAPE_NONE &&
             ++c->end_reads == CAS_END_READS) {
        /* The reader waits on a tape that has run out: the port's to
         * rewind, or to give up on (tape.h). */
        c->end_reads = 0;
        m->tape.op = TAPE_END;
        m->tape.requests++;
        mc6809_set_halt(&m->cpu, true);
    }
    return c->level;
}

void cassette_moved(coco_t *m) {
    cassette_t *c = &m->cas;
    bool was = c->playing;
    c->playing = false;
    c->have = false;
    c->idx = 0;
    c->writer = W_ALONE;
    c->level = false;
    c->left = 0;
    c->ended = false;
    c->end_reads = 0;
    if (was || c->motor) update(m, coco_ticks(m));
}

void coco_cassette_set_fast(coco_t *m, bool fast) {
    cassette_t *c = &m->cas;
    if (fast == c->fast) return;
    uint64_t t = coco_ticks(m);
    if (c->playing) update(m, t);
    c->fast = fast;
    cassette_moved(m);
    coco_tape_page(m);
}

bool coco_cassette_running(const coco_t *m) {
    return !m->cas.fast && m->cas.motor;
}

/* ---- the recorder ------------------------------------------------------------ *
 * The DAC's crossings of its middle, timed as the ROM's reader times
 * them: the high half is a 1's or a 0's by the middle of the two, and
 * anything much longer than a 0's is no tone. Bits are shifted in from
 * bit 0 until the byte is the sync, as BLKIN finds it; then the type,
 * the length, the data and the checksum. */

#define REC_ONE_MAX   ((CAS_HIGH_1 + CAS_HIGH_0) / 2u * CAS_CYCLE_TICKS)
#define REC_HIGH_MAX  ((CAS_HIGH_0 + CAS_HIGH_1) * CAS_CYCLE_TICKS)
#define REC_LEAD_MAX  4096u   /* leader bytes kept before a block */

enum { R_SEEK, R_BLOCK };

static void rec_append(coco_t *m, const uint8_t *src, uint32_t n, uint8_t fill) {
    coco_tape_t *t = &m->tape;
    if (!t->buf) return;
    if (!t->inserted) {
        t->inserted = true;
        t->len = t->pos = t->saved = 0;
        cassette_moved(m);
    }
    if (n > t->cap - t->len) {
        t->overflow = true;
        return;
    }
    if (src) memcpy(&t->buf[t->len], src, n);
    else     memset(&t->buf[t->len], fill, n);
    t->len += n;
}

static void rec_bit(coco_t *m, bool one) {
    cassette_t *c = &m->cas;
    c->rec_wrote = true;
    if (c->rec_state == R_SEEK) {
        c->rec_shift = (uint8_t)((c->rec_shift >> 1) | (one ? 0x80u : 0u));
        c->rec_lead++;
        if (c->rec_shift == CAS_SYNC && c->rec_lead >= 8u) {
            c->rec_state = R_BLOCK;
            c->rec_got = 0;
            c->rec_want = 3u;
            c->rec_nbit = 0;
        }
        return;
    }
    c->rec_shift = (uint8_t)((c->rec_shift >> 1) | (one ? 0x80u : 0u));
    if (++c->rec_nbit < 8u) return;
    c->rec_nbit = 0;
    c->rec_blk[c->rec_got++] = c->rec_shift;
    if (c->rec_got == 2u) c->rec_want = (uint16_t)(3u + c->rec_blk[1]);
    if (c->rec_got < c->rec_want) return;

    /* A whole block: kept if its checksum is right, with the leader
     * before it, the sync's own bits apart. */
    uint8_t sum = 0;
    for (unsigned i = 0; i + 1u < c->rec_got; i++) sum = (uint8_t)(sum + c->rec_blk[i]);
    uint32_t lead = (c->rec_lead - 8u) / 8u;
    if (lead > REC_LEAD_MAX) lead = REC_LEAD_MAX;
    uint32_t room = m->tape.inserted ? m->tape.cap - m->tape.len : m->tape.cap;
    if (sum == c->rec_blk[c->rec_got - 1u] && lead + 1u + c->rec_got <= room) {
        static const uint8_t sync = CAS_SYNC;
        rec_append(m, NULL, lead, CAS_LEADER_BYTE);
        rec_append(m, &sync, 1, 0);
        rec_append(m, c->rec_blk, c->rec_got, 0);
        c->rec_blocks++;
        m->tape.blocks_out++;
        c->rec_kept = true;
    } else {
        if (sum == c->rec_blk[c->rec_got - 1u]) m->tape.overflow = true;
        c->rec_errors++;
    }
    c->rec_state = R_SEEK;
    c->rec_lead = 0;
    c->rec_shift = 0;
}

/* A block in progress is not kept. */
static void rec_cut(cassette_t *c) {
    if (c->rec_state == R_BLOCK) c->rec_errors++;
    c->rec_state = R_SEEK;
    c->rec_lead = 0;
    c->rec_shift = 0;
}

void COCO_HOT1(cassette_dac)(coco_t *m, uint8_t dac, uint64_t t) {
    cassette_t *c = &m->cas;
    bool level = dac >= 0x80u;
    if (level == c->rec_level) return;
    c->rec_level = level;
    if (c->fast || !c->motor) return;
    if (level) {
        c->rec_rise = t;
        return;
    }
    uint64_t high = t - c->rec_rise;
    if (!c->rec_rise || high >= REC_HIGH_MAX) rec_cut(c);
    else rec_bit(m, high < REC_ONE_MAX);
}

void cassette_motor(coco_t *m, bool on, uint64_t t) {
    cassette_t *c = &m->cas;
    if (on == c->motor) return;
    if (!on) {
        c->motor = false;
        update(m, t);
        /* The $55 after the last block kept, which BLKOUT wrote. */
        if (c->rec_state == R_SEEK && c->rec_kept && c->rec_lead >= 8u)
            rec_append(m, NULL, c->rec_lead / 8u, CAS_LEADER_BYTE);
        rec_cut(c);
        c->rec_kept = c->rec_wrote = false;
        return;
    }
    c->motor = true;
    c->rec_rise = 0;
    c->rec_kept = c->rec_wrote = false;
    c->end_reads = 0;
    update(m, t);
}
