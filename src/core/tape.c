/* tape.c — Color BASIC's cassette routines served by a trap (tape.h). */

#include "tape.h"

#include <string.h>

#include "bus.h"
#include "coco.h"
#include "romset.h"

/* ---- Color BASIC's addresses (tape.h), the same in 1.1 and 1.3 -------- */

#define PC_BLKIN      0xA70Fu   /* after ORCC and the blink: LDX <$7E       */
#define PC_BLKIN_SUM  0xA73Du   /* SUBA <$80: the checksum compared         */
#define PC_BLKIN_BAD  0xA744u   /* LDA #2: a byte that did not store        */
#define PC_CSRDON     0xA780u   /* after ORCC and CASON: CLR <$82           */
#define PC_CSRDON_END 0xA794u   /* STA <$84                                 */
#define PC_DELAY      0xA7D1u   /* CASON's LDX <$8A                         */
#define PC_DELAY_END  0xA7D7u   /* its RTS                                  */
#define PC_WRTLDR     0xA7DCu   /* after ORCC and CASON: LDX <$92           */
#define PC_WRTLDR_END 0xA7E0u   /* LEAX -1,X after the last byte            */
#define PC_BLKOUT     0xA7F6u   /* after ORCC: LDB <$7D                     */
#define PC_WRITER_END 0xA85Au   /* the byte writer's PULS A,PC              */
#define WRITER_TABLE  0xA85Cu   /* its DAC values, to $A87F                 */
#define WRITER_Y_END  0xA880u

/* The direct page's cassette variables (Color BASIC). */
#define V_BLKTYP  0x7Cu
#define V_BLKLEN  0x7Du
#define V_CBUFAD  0x7Eu
#define V_CCKSUM  0x80u
#define V_CSRERR  0x81u
#define V_COUNT   0x82u
#define V_DACVAL  0x85u
#define V_LDRLEN  0x92u
#define V_NAMELEN 0x1D1u   /* the name CLOAD was given: its length...  */
#define V_NAME    0x1D2u   /* ...and its eight characters               */

#define OP_NOP    0x12u

/* ---- guest access ----------------------------------------------------- */

static uint8_t rom_at(const coco_t *m, uint16_t a) {
    return m->rom[a - 0x8000u];
}

static uint16_t peek16(coco_t *m, uint16_t a) {
    return (uint16_t)((bus_read(m, a) << 8) | bus_read(m, (uint16_t)(a + 1u)));
}

/* Carry on at `pc`, a ROM address: its opcode is what this fetch reads,
 * and its operands follow (tape.h). */
static uint8_t resume(coco_t *m, uint16_t pc) {
    m->cpu.pc = (uint16_t)(pc + 1u);
    return rom_at(m, pc);
}

/* Wait for the port at the trap point `pc` (tape.h): a NOP now, then the
 * HALT line until coco_tape_resume or coco_tape_give_up. */
static uint8_t stall(coco_t *m, uint16_t pc, tape_op_t op) {
    coco_tape_t *t = &m->tape;
    t->op = op;
    t->op_pc = pc;
    t->requests++;
    m->cpu.pc = pc;
    mc6809_set_halt(&m->cpu, true);
    return OP_NOP;
}

/* The byte writer's state after writing `byte` (tape.h): the last bit's
 * DAC values from its table, a 1 stepping two entries at a time. */
static void writer_tail(coco_t *m, uint8_t byte) {
    bool one = (byte & 0x80u) != 0;
    uint8_t next = rom_at(m, one ? WRITER_TABLE + 0x22u : WRITER_TABLE + 0x23u);
    uint8_t dac  = rom_at(m, one ? WRITER_TABLE + 0x20u : WRITER_TABLE + 0x22u);
    bus_write(m, V_DACVAL, next);
    bus_write(m, 0xFF20u, dac);
    mc6809_t *c = &m->cpu;
    c->a = byte;
    c->b = 0;
    c->y = WRITER_Y_END;
    c->cc = (uint8_t)((c->cc & ~MC6809_CC_N) | MC6809_CC_Z | MC6809_CC_V | MC6809_CC_C);
}

/* A save goes on at the tape's end; a save with the deck empty starts a
 * new tape, which the port names from its first file. */
static void append(coco_tape_t *t, const uint8_t *src, uint32_t n, uint8_t fill) {
    if (!t->inserted) {
        t->inserted = true;
        t->len = t->pos = t->saved = 0;
    }
    if (n > t->cap - t->len) {
        t->overflow = true;
        return;
    }
    if (src) memcpy(&t->buf[t->len], src, n);
    else     memset(&t->buf[t->len], fill, n);
    t->len += n;
}

/* ---- the routines ----------------------------------------------------- */

static uint8_t blkin(coco_t *m, uint16_t pc) {
    coco_tape_t *t = &m->tape;
    if (!t->inserted) return stall(m, pc, TAPE_FIND);
    (void)bus_read(m, 0xFF20u);

    uint32_t i = t->pos;
    while (i < t->len && t->buf[i] != CAS_SYNC) i++;
    if (t->len - i < 3u || t->len - i - 3u < (uint32_t)t->buf[i + 2u] + 1u) {
        t->pos = t->len;
        return stall(m, pc, TAPE_END);
    }
    uint8_t type = t->buf[i + 1u], n = t->buf[i + 2u];
    bus_write(m, V_BLKTYP, type);
    bus_write(m, V_BLKLEN, n);
    uint8_t sum = (uint8_t)(type + n);
    uint16_t x = peek16(m, V_CBUFAD);
    t->pos = i + 3u;
    bus_write(m, V_COUNT, 0);
    for (unsigned k = 0; k < n; k++) {
        uint8_t v = t->buf[t->pos++];
        bus_write(m, x, v);
        uint8_t r = bus_read(m, x);
        x++;
        if (r != v) {
            bus_write(m, V_CCKSUM, sum);
            bus_write(m, V_CSRERR, (uint8_t)(n - k));
            m->cpu.a = v;
            m->cpu.x = x;
            /* The ROM's CMPA leaves C, the borrow; what it sets besides,
             * its LDA and STA set again. */
            if (v < r) m->cpu.cc |= MC6809_CC_C;
            else       m->cpu.cc &= (uint8_t)~MC6809_CC_C;
            t->errors++;
            return resume(m, PC_BLKIN_BAD);
        }
        sum = (uint8_t)(sum + v);
    }
    bus_write(m, V_CCKSUM, sum);
    bus_write(m, V_CSRERR, 0);
    m->cpu.a = t->buf[t->pos++];
    m->cpu.x = x;
    t->blocks_in++;
    return resume(m, PC_BLKIN_SUM);
}

static uint8_t csrdon(coco_t *m, uint16_t pc) {
    coco_tape_t *t = &m->tape;
    if (!t->inserted) {
        uint8_t n = bus_read(m, V_NAMELEN);
        t->want_len = n < CAS_NAME_LEN ? n : CAS_NAME_LEN;
        for (unsigned i = 0; i < CAS_NAME_LEN; i++)
            t->want[i] = bus_read(m, (uint16_t)(V_NAME + i));
        return stall(m, pc, TAPE_FIND);
    }
    if (t->pos >= t->len) return stall(m, pc, TAPE_END);
    (void)bus_read(m, 0xFF20u);
    bus_write(m, V_COUNT, 0xA0u);
    m->cpu.a = 0xA0u;
    m->cpu.cc = (uint8_t)(m->cpu.cc & ~MC6809_CC_C);
    return resume(m, PC_CSRDON_END);
}

static uint8_t wrtldr(coco_t *m) {
    coco_tape_t *t = &m->tape;
    uint32_t n = peek16(m, V_LDRLEN);
    if (n == 0) n = 0x10000u;   /* LEAX -1 from zero: 65,536 bytes */
    append(t, NULL, n, CAS_LEADER_BYTE);
    m->cpu.x = 1;
    writer_tail(m, CAS_LEADER_BYTE);
    return resume(m, PC_WRTLDR_END);
}

static uint8_t blkout(coco_t *m) {
    coco_tape_t *t = &m->tape;
    uint8_t type = bus_read(m, V_BLKTYP), n = bus_read(m, V_BLKLEN);
    uint16_t x = peek16(m, V_CBUFAD);
    uint8_t blk[6u + 255u];   /* $55 $3C type length data sum $55 */
    uint8_t sum = (uint8_t)(type + n);
    /* The ROM reads the data twice, to sum it and to write it. */
    for (unsigned k = 0; k < n; k++) sum = (uint8_t)(sum + bus_read(m, (uint16_t)(x + k)));
    blk[0] = CAS_LEADER_BYTE;
    blk[1] = CAS_SYNC;
    blk[2] = type;
    blk[3] = n;
    for (unsigned k = 0; k < n; k++) blk[4u + k] = bus_read(m, (uint16_t)(x + k));
    blk[4u + n] = sum;
    blk[5u + n] = CAS_LEADER_BYTE;
    append(t, blk, 6u + n, 0);
    t->blocks_out++;

    bus_write(m, V_CCKSUM, sum);
    bus_write(m, V_CSRERR, 0);
    m->cpu.x = (uint16_t)(x + n);
    /* The last byte's writer pushed A, which its PULS takes back. */
    m->cpu.s = (uint16_t)(m->cpu.s - 1u);
    bus_write(m, m->cpu.s, CAS_LEADER_BYTE);
    writer_tail(m, CAS_LEADER_BYTE);
    return resume(m, PC_WRITER_END);
}

/* ---- coco.c's --------------------------------------------------------- */

void tape_rom_loaded(coco_t *m, const uint8_t *image, size_t len) {
    rom_id_t id = romset_identify(image, len);
    m->tape.rom_ok = id == ROM_COCO_BAS13 || id == ROM_COCO_BAS11;
}

void tape_jump(coco_t *m, uint16_t target) {
    coco_tape_t *t = &m->tape;
    if (t->hot || !m->cfg.tape_trap || !t->rom_ok || !m->rom_present[1] || sam6883_ty(&m->sam))
        return;
    uint16_t at;
    switch (target) {
    case 0xA70Bu: at = PC_BLKIN;  break;
    case 0xA77Cu: at = PC_CSRDON; break;   /* through CASON's delay first */
    case 0xA7CAu: at = PC_DELAY;  break;
    case 0xA7D8u: at = PC_WRTLDR; break;
    case 0xA7F4u: at = PC_BLKOUT; break;
    default:      return;
    }
    t->hot = at;
    coco_tape_page(m);
}

static uint8_t serve(coco_t *m, uint16_t a) {
    coco_tape_t *t = &m->tape;
    if (a == PC_DELAY) {
        m->cpu.x = 0;
        m->cpu.cc |= MC6809_CC_Z;
        return resume(m, PC_DELAY_END);
    }
    if (!t->buf) return rom_at(m, a);   /* no deck: the ROM's own routines */
    switch (a) {
    case PC_BLKIN:  return blkin(m, a);
    case PC_CSRDON: return csrdon(m, a);
    case PC_WRTLDR: return wrtldr(m);
    case PC_BLKOUT: return blkout(m);
    default:        return rom_at(m, a);
    }
}

uint8_t tape_page_read(coco_t *m, uint16_t a) {
    /* Only the opcode fetch at a trap point is the trap: the PC is one
     * past the address while it is fetched. */
    if (m->cpu.pc != (uint16_t)(a + 1u)) return rom_at(m, a);
    uint8_t op = serve(m, a);
    /* Served, not stalled: the page is fast again. */
    if (a == m->tape.hot && m->tape.op == TAPE_NONE) {
        m->tape.hot = 0;
        coco_tape_page(m);
    }
    return op;
}

void tape_reset(coco_t *m) {
    m->tape.hot = 0;   /* map() follows */
    if (m->tape.op != TAPE_NONE) {
        m->tape.op = TAPE_NONE;
        mc6809_set_halt(&m->cpu, false);
    }
}

/* ---- the port's ------------------------------------------------------- */

void coco_tape_attach(coco_t *m, uint8_t *buf, uint32_t cap) {
    coco_tape_t *t = &m->tape;
    t->buf = buf;
    t->cap = buf ? cap : 0;
    coco_tape_eject(m);
}

void coco_tape_insert(coco_t *m, uint32_t len) {
    coco_tape_t *t = &m->tape;
    t->inserted = t->buf != NULL;
    t->len = t->saved = len <= t->cap ? len : t->cap;
    t->pos = 0;
    t->overflow = false;
}

void coco_tape_eject(coco_t *m) {
    coco_tape_t *t = &m->tape;
    t->inserted = false;
    t->len = t->pos = t->saved = 0;
    t->overflow = false;
}

void coco_tape_rewind(coco_t *m) {
    m->tape.pos = 0;
}

bool coco_tape_unsaved(const coco_t *m, uint32_t *from, uint32_t *to) {
    const coco_tape_t *t = &m->tape;
    if (!t->inserted || t->len <= t->saved) return false;
    *from = t->saved;
    *to = t->len;
    return true;
}

void coco_tape_saved(coco_t *m) {
    m->tape.saved = m->tape.len;
}

tape_op_t coco_tape_pending(const coco_t *m) {
    return m->tape.op;
}

void coco_tape_resume(coco_t *m) {
    m->tape.op = TAPE_NONE;
    mc6809_set_halt(&m->cpu, false);
}

void coco_tape_give_up(coco_t *m) {
    coco_reset(m);   /* tape_reset ends the request and the stall */
}
