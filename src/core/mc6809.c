/* mc6809.c — Motorola MC6809E interpreter (design.md §7).
 *
 * Switch-dispatched with explicit cycle accounting, in the family's style
 * (EL §3.2): one switch per opcode page, the indexed postbyte decoded by
 * one inlined function that adds its own cycles, and each flag rule
 * written once in a small helper.
 *
 * Written from the MC6809E datasheet. Where the datasheet leaves a flag
 * undefined the core does what XRoar does, since XRoar is the oracle it
 * is held to (D2, §7.4), and each such choice is marked "undefined:" in a
 * comment. XRoar's own choices there come from measurements of real
 * chips (hoglet67, tlindner), which it cites.
 *
 * Bus behaviour inside an instruction is not modelled (§7.1): no dummy
 * reads, no dead cycles' addresses. Every read and write that reaches
 * memory is made, in the chip's order, so a read-modify-write on a PIA
 * reads before it writes, CLR included, and a 16-bit store writes its high
 * byte first.
 */

#include "mc6809.h"

#include "bus.h"
#include "coco.h"
#include "hot.h"

#define CC_C MC6809_CC_C
#define CC_V MC6809_CC_V
#define CC_Z MC6809_CC_Z
#define CC_N MC6809_CC_N
#define CC_I MC6809_CC_I
#define CC_H MC6809_CC_H
#define CC_F MC6809_CC_F
#define CC_E MC6809_CC_E

#define INLINE static inline __attribute__((always_inline))

/* Cycles per opcode, page 1, before the indexed postbyte's extra cycles,
 * a long branch taken, and PSH/PUL's and RTI's registers. Zero marks the
 * prefixes $10 and $11, which go to their own pages. SYNC and CWAI count
 * up to the start of their wait, and HCF ($14, $15, $CD) to the start of
 * its lock. The undocumented opcodes' counts are XRoar's, held by the
 * oracle (D12). */
static const uint8_t cycles_p1[256] = {
/*      x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF */
/*0x*/   6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  3,  6,
/*1x*/   0,  0,  2,  2,  1,  1,  5,  9,  3,  2,  3,  2,  3,  2,  8,  6,
/*2x*/   3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
/*3x*/   4,  4,  4,  4,  5,  5,  5,  5,  4,  5,  3,  6, 18, 11, 19, 19,
/*4x*/   2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
/*5x*/   2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
/*6x*/   6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  3,  6,
/*7x*/   7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  4,  7,
/*8x*/   2,  2,  2,  4,  2,  2,  2,  2,  2,  2,  2,  2,  4,  7,  3,  3,
/*9x*/   4,  4,  4,  6,  4,  4,  4,  4,  4,  4,  4,  4,  6,  7,  5,  5,
/*Ax*/   4,  4,  4,  6,  4,  4,  4,  4,  4,  4,  4,  4,  6,  7,  5,  5,
/*Bx*/   5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  5,  7,  8,  6,  6,
/*Cx*/   2,  2,  2,  4,  2,  2,  2,  2,  2,  2,  2,  2,  3,  1,  3,  3,
/*Dx*/   4,  4,  4,  6,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,
/*Ex*/   4,  4,  4,  6,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,
/*Fx*/   5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,
};

/* Page 1's undocumented opcodes, and the two prefixes (D12). */
static bool p1_documented(uint8_t op) {
    switch (op) {
    case 0x01: case 0x02: case 0x05: case 0x0B:
    case 0x10: case 0x11: case 0x14: case 0x15: case 0x18: case 0x1B:
    case 0x38: case 0x3E:
    case 0x41: case 0x42: case 0x45: case 0x4B: case 0x4E:
    case 0x51: case 0x52: case 0x55: case 0x5B: case 0x5E:
    case 0x61: case 0x62: case 0x65: case 0x6B:
    case 0x71: case 0x72: case 0x75: case 0x7B:
    case 0x87: case 0x8F: case 0xC7: case 0xCD: case 0xCF:
        return false;
    default:
        return true;
    }
}

/* Pages 2 and 3, the prefix's cycle included. */
static const uint8_t cycles_p2[256] = {
    [0x21] = 5, [0x22] = 5, [0x23] = 5, [0x24] = 5, [0x25] = 5,
    [0x26] = 5, [0x27] = 5, [0x28] = 5, [0x29] = 5, [0x2A] = 5,
    [0x2B] = 5, [0x2C] = 5, [0x2D] = 5, [0x2E] = 5, [0x2F] = 5,
    [0x3F] = 20,
    [0x83] = 5, [0x8C] = 5, [0x8E] = 4,
    [0x93] = 7, [0x9C] = 7, [0x9E] = 6, [0x9F] = 6,
    [0xA3] = 7, [0xAC] = 7, [0xAE] = 6, [0xAF] = 6,
    [0xB3] = 8, [0xBC] = 8, [0xBE] = 7, [0xBF] = 7,
    [0xCE] = 4,
    [0xDE] = 6, [0xDF] = 6,
    [0xEE] = 6, [0xEF] = 6,
    [0xFE] = 7, [0xFF] = 7,
};

static const uint8_t cycles_p3[256] = {
    [0x3F] = 20,
    [0x83] = 5, [0x8C] = 5,
    [0x93] = 7, [0x9C] = 7,
    [0xA3] = 7, [0xAC] = 7,
    [0xB3] = 8, [0xBC] = 8,
};

bool mc6809_documented(uint16_t op) {
    switch (op >> 8) {
    case 0x00: return p1_documented((uint8_t)op);
    case 0x10: return cycles_p2[op & 0xFFu] != 0;
    case 0x11: return cycles_p3[op & 0xFFu] != 0;
    default:   return false;
    }
}

/* ---- state ----------------------------------------------------------- */

void mc6809_init(mc6809_t *c) {
    *c = (mc6809_t){0};
}

/* The interrupt lines as the chip's input latch would take them: NMI only
 * once armed (§7.1). */
INLINE uint8_t lines(const mc6809_t *c) {
    return (uint8_t)((c->irq ? MC6809_LINE_IRQ : 0u) |
                     (c->firq ? MC6809_LINE_FIRQ : 0u) |
                     ((c->nmi_pending && c->nmi_armed) ? MC6809_LINE_NMI : 0u));
}

void COCO_HOT1(mc6809_lines_late)(mc6809_t *c) {
    if (!c->stale) {
        c->active = lines(c);
        c->stale = true;
        mc6809_attn(c);
    }
}

void COCO_HOT1(mc6809_attn)(mc6809_t *c) {
    c->attn = (uint8_t)((c->irq | c->firq) != 0 || c->nmi_pending ||
                        c->halt || c->wait != MC6809_RUNNING || c->stale);
}

void COCO_HOT1(mc6809_set_irq)(mc6809_t *c, uint8_t source, bool asserted) {
    if (asserted) c->irq |= source;
    else          c->irq = (uint8_t)(c->irq & ~source);
    mc6809_attn(c);
}

void COCO_HOT1(mc6809_set_firq)(mc6809_t *c, uint8_t source, bool asserted) {
    if (asserted) c->firq |= source;
    else          c->firq = (uint8_t)(c->firq & ~source);
    mc6809_attn(c);
}

void mc6809_set_nmi(mc6809_t *c, bool level) {
    if (level && !c->nmi_line) c->nmi_pending = true;   /* the edge */
    c->nmi_line = level;
    mc6809_attn(c);
}

void mc6809_set_halt(mc6809_t *c, bool asserted) {
    c->halt = asserted;
    mc6809_attn(c);
}

/* ---- memory ---------------------------------------------------------- */

#define RD(a)    bus_read(m, (uint16_t)(a))
#define WR(a, v) bus_write(m, (uint16_t)(a), (uint8_t)(v))

/* A data access, with the cycle of the instruction it falls on. Counted
 * back from the instruction's end, whose count is known by the time of
 * the access, in the cycle sequences of the datasheet as XRoar models
 * them: an 8-bit read or a store is the last cycle; a word's two bytes
 * are the last two, or the two before a dead cycle for ADDD, SUBD and the
 * compares; read-modify-write reads two cycles before it writes, and TST
 * reads two before its end (§7.2). */
#define RDA(a, at)    bus_read_at(m, (uint16_t)(a), (at))
#define WRA(a, v, at) bus_write_at(m, (uint16_t)(a), (uint8_t)(v), (at))

INLINE uint8_t fetch8(coco_t *m, mc6809_t *c) {
    return RD(c->pc++);
}

INLINE uint16_t fetch16(coco_t *m, mc6809_t *c) {
    uint16_t hi = fetch8(m, c);
    return (uint16_t)((hi << 8) | fetch8(m, c));
}

INLINE uint16_t rd16(coco_t *m, uint16_t a) {
    uint16_t hi = RD(a);
    return (uint16_t)((hi << 8) | RD(a + 1u));
}

/* `last` is MC6809_IO_LAST when the second byte is the last cycle. */
INLINE uint16_t rd16_at(coco_t *m, uint16_t a, uint32_t at, uint32_t last) {
    uint16_t hi = RDA(a, at);
    return (uint16_t)((hi << 8) | RDA(a + 1u, (at + 1u) | last));
}

INLINE void wr16_at(coco_t *m, uint16_t a, uint16_t v, uint32_t at) {
    WRA(a, v >> 8, at);
    WRA(a + 1u, v, (at + 1u) | MC6809_IO_LAST);
}

#define LAST(n) ((n) | MC6809_IO_LAST)

/* Stacks grow down; a word goes low byte first, so it lies high byte at
 * the lower address. */
INLINE void push8(coco_t *m, uint16_t *sp, uint8_t v) {
    *sp = (uint16_t)(*sp - 1u);
    WR(*sp, v);
}

INLINE void push16(coco_t *m, uint16_t *sp, uint16_t v) {
    push8(m, sp, (uint8_t)v);
    push8(m, sp, (uint8_t)(v >> 8));
}

INLINE uint8_t pull8(coco_t *m, uint16_t *sp) {
    uint8_t v = RD(*sp);
    *sp = (uint16_t)(*sp + 1u);
    return v;
}

INLINE uint16_t pull16(coco_t *m, uint16_t *sp) {
    uint16_t hi = pull8(m, sp);
    return (uint16_t)((hi << 8) | pull8(m, sp));
}

INLINE uint16_t get_d(const mc6809_t *c) {
    return (uint16_t)((c->a << 8) | c->b);
}

INLINE void set_d(mc6809_t *c, uint16_t v) {
    c->a = (uint8_t)(v >> 8);
    c->b = (uint8_t)v;
}

/* ---- flags ----------------------------------------------------------- */

INLINE void nz8(mc6809_t *c, uint8_t r) {
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | ((r & 0x80u) ? CC_N : 0u) |
                      (r == 0u ? CC_Z : 0u));
}

INLINE void nz16(mc6809_t *c, uint16_t r) {
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | ((r & 0x8000u) ? CC_N : 0u) |
                      (r == 0u ? CC_Z : 0u));
}

/* Loads, stores and the logical operations: N, Z, V cleared. */
INLINE uint8_t ld8(mc6809_t *c, uint8_t v) {
    c->cc = (uint8_t)(c->cc & ~CC_V);
    nz8(c, v);
    return v;
}

INLINE uint16_t ld16(mc6809_t *c, uint16_t v) {
    c->cc = (uint8_t)(c->cc & ~CC_V);
    nz16(c, v);
    return v;
}

/* Undefined: H after a subtraction or compare; left alone. */
INLINE uint8_t sub8(mc6809_t *c, uint8_t a, uint8_t b, unsigned borrow) {
    unsigned r = (unsigned)a - b - borrow;
    c->cc = (uint8_t)((c->cc & ~(CC_V | CC_C)) |
                      ((((a ^ b) & (a ^ r)) & 0x80u) ? CC_V : 0u) |
                      ((r & 0x100u) ? CC_C : 0u));
    nz8(c, (uint8_t)r);
    return (uint8_t)r;
}

INLINE uint8_t add8(mc6809_t *c, uint8_t a, uint8_t b, unsigned carry) {
    unsigned r = (unsigned)a + b + carry;
    c->cc = (uint8_t)((c->cc & ~(CC_H | CC_V | CC_C)) |
                      (((a ^ b ^ r) & 0x10u) ? CC_H : 0u) |
                      (((~(a ^ b) & (a ^ r)) & 0x80u) ? CC_V : 0u) |
                      ((r & 0x100u) ? CC_C : 0u));
    nz8(c, (uint8_t)r);
    return (uint8_t)r;
}

INLINE uint16_t sub16(mc6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a - b;
    c->cc = (uint8_t)((c->cc & ~(CC_V | CC_C)) |
                      ((((a ^ b) & (a ^ r)) & 0x8000u) ? CC_V : 0u) |
                      ((r & 0x10000u) ? CC_C : 0u));
    nz16(c, (uint16_t)r);
    return (uint16_t)r;
}

INLINE uint16_t add16(mc6809_t *c, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    c->cc = (uint8_t)((c->cc & ~(CC_V | CC_C)) |
                      (((~(a ^ b) & (a ^ r)) & 0x8000u) ? CC_V : 0u) |
                      ((r & 0x10000u) ? CC_C : 0u));
    nz16(c, (uint16_t)r);
    return (uint16_t)r;
}

INLINE void set_c(mc6809_t *c, bool on) {
    c->cc = (uint8_t)((c->cc & ~CC_C) | (on ? CC_C : 0u));
}

INLINE void set_v(mc6809_t *c, bool on) {
    c->cc = (uint8_t)((c->cc & ~CC_V) | (on ? CC_V : 0u));
}

/* The unary group: memory, A and B alike. */
INLINE uint8_t op_neg(mc6809_t *c, uint8_t v) { return sub8(c, 0, v, 0); }

INLINE uint8_t op_com(mc6809_t *c, uint8_t v) {
    v = ld8(c, (uint8_t)~v);
    c->cc |= CC_C;
    return v;
}

/* LSR, ROR and ASR leave V alone; undefined: H after ASR, left alone. */
INLINE uint8_t op_lsr(mc6809_t *c, uint8_t v) {
    set_c(c, v & 1u);
    v = (uint8_t)(v >> 1);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_ror(mc6809_t *c, uint8_t v) {
    uint8_t cin = (uint8_t)((c->cc & CC_C) ? 0x80u : 0u);
    set_c(c, v & 1u);
    v = (uint8_t)((v >> 1) | cin);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_asr(mc6809_t *c, uint8_t v) {
    set_c(c, v & 1u);
    v = (uint8_t)((v >> 1) | (v & 0x80u));
    nz8(c, v);
    return v;
}

/* ASL and ROL: V is bit 7 xor bit 6 of the operand. Undefined: H after
 * ASL, left alone. */
INLINE uint8_t op_asl(mc6809_t *c, uint8_t v) {
    set_c(c, v & 0x80u);
    set_v(c, ((v >> 7) ^ (v >> 6)) & 1u);
    v = (uint8_t)(v << 1);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_rol(mc6809_t *c, uint8_t v) {
    uint8_t cin = (uint8_t)(c->cc & CC_C);
    set_c(c, v & 0x80u);
    set_v(c, ((v >> 7) ^ (v >> 6)) & 1u);
    v = (uint8_t)((v << 1) | cin);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_dec(mc6809_t *c, uint8_t v) {
    v--;
    set_v(c, v == 0x7Fu);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_inc(mc6809_t *c, uint8_t v) {
    v++;
    set_v(c, v == 0x80u);
    nz8(c, v);
    return v;
}

INLINE uint8_t op_tst(mc6809_t *c, uint8_t v) { return ld8(c, v); }

INLINE uint8_t op_clr(mc6809_t *c, uint8_t v) {
    (void)v;
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
    return 0;
}

/* The undocumented forms (D12), as XRoar has them from hoglet67's
 * measurements: NGC, NEG with the carry in, $x2; DEC that sets C when it
 * borrows, $xB; CLR that leaves C, $4E and $5E. */
INLINE uint8_t op_ngc(mc6809_t *c, uint8_t v) { return sub8(c, 0, v, c->cc & CC_C); }

INLINE uint8_t op_xdec(mc6809_t *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1u);
    c->cc = (uint8_t)((c->cc & ~(CC_V | CC_C)) | (r == 0x7Fu ? CC_V : 0u) | (v == 0 ? CC_C : 0u));
    nz8(c, r);
    return r;
}

INLINE uint8_t op_xclr(mc6809_t *c, uint8_t v) {
    (void)v;
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_V)) | CC_Z);
    return 0;
}

/* DAA: C is set when the high digit is corrected and never cleared.
 * Undefined: V, which XRoar takes from hoglet67's measurements as bit 7
 * of the result xor the new C. */
INLINE uint8_t op_daa(mc6809_t *c, uint8_t a) {
    unsigned adj = 0;
    if ((a & 0x0Fu) > 9u || (c->cc & CC_H)) adj |= 0x06u;
    if (a > 0x99u || (c->cc & CC_C))        adj |= 0x60u;
    unsigned r = a + adj;
    if (r & 0x100u) c->cc |= CC_C;
    nz8(c, (uint8_t)r);
    set_v(c, ((r >> 7) ^ c->cc) & 1u);
    return (uint8_t)r;
}

INLINE bool branch_taken(const mc6809_t *c, uint8_t op) {
    uint8_t cc = c->cc;
    bool n = (cc & CC_N) != 0, z = (cc & CC_Z) != 0;
    bool v = (cc & CC_V) != 0, cy = (cc & CC_C) != 0;
    bool t;
    switch ((op >> 1) & 7u) {
    case 0:  t = true;            break;  /* BRA, BRN   */
    case 1:  t = !(cy || z);      break;  /* BHI, BLS   */
    case 2:  t = !cy;             break;  /* BCC, BCS   */
    case 3:  t = !z;              break;  /* BNE, BEQ   */
    case 4:  t = !v;              break;  /* BVC, BVS   */
    case 5:  t = !n;              break;  /* BPL, BMI   */
    case 6:  t = (n == v);        break;  /* BGE, BLT   */
    default: t = !z && (n == v);  break;  /* BGT, BLE   */
    }
    return (op & 1u) ? !t : t;
}

/* ---- trap ------------------------------------------------------------ */

static void COCO_HOT1(undocumented)(mc6809_t *c, uint16_t pc0, uint16_t op) {
    c->undoc_count++;
    c->undoc_pc = pc0;
    c->undoc_op = op;
}

/* ---- addressing ------------------------------------------------------ */

/* The indexed postbyte: the effective address, with the mode's cycles
 * added to *cyc. Undocumented postbytes are decoded as the chip decodes
 * them and counted (D12): $x7 as $x6, $xA as PC | $FF, $xE as $FFFF,
 * indirect ,R+ and ,-R, extended without indirection, and extended
 * indirect with register bits set. */
/* PICO_COCO_IDX_OUTLINE, the default: one copy of the decoder, called, in
 * place of one inlined into each indexed opcode. On the RP2350 at tier 2,
 * 34 KiB of interpreter in SRAM against 88, for 6-8 % more host cycles an
 * instruction (M2's gate, design.md §7.5). */
#if PICO_COCO_IDX_OUTLINE
static uint16_t __attribute__((noinline)) COCO_HOT2(ea_indexed)(coco_t *m, mc6809_t *c,
        uint32_t *cyc, uint16_t pc0, uint16_t op) {
#else
INLINE uint16_t ea_indexed(coco_t *m, mc6809_t *c, uint32_t *cyc,
                           uint16_t pc0, uint16_t op) {
#endif
    uint8_t pb = fetch8(m, c);
    uint16_t *r;
    switch ((pb >> 5) & 3u) {
    case 0:  r = &c->x; break;
    case 1:  r = &c->y; break;
    case 2:  r = &c->u; break;
    default: r = &c->s; break;
    }

    if (!(pb & 0x80u)) {                          /* n5,R */
        *cyc += 1u;
        return (uint16_t)(*r + (uint16_t)((pb & 0x0Fu) - (pb & 0x10u)));
    }

    uint16_t ea;
    bool undoc = false;
    switch (pb & 0x0Fu) {
    case 0x0: ea = *r; *r = (uint16_t)(*r + 1u); *cyc += 2u; undoc = (pb & 0x10u) != 0; break;
    case 0x1: ea = *r; *r = (uint16_t)(*r + 2u); *cyc += 3u; break;
    case 0x2: *r = (uint16_t)(*r - 1u); ea = *r; *cyc += 2u; undoc = (pb & 0x10u) != 0; break;
    case 0x3: *r = (uint16_t)(*r - 2u); ea = *r; *cyc += 3u; break;
    case 0x4: ea = *r; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); *cyc += 1u; break;
    case 0x7: undoc = true; __attribute__((fallthrough));
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); *cyc += 1u; break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch8(m, c)); *cyc += 1u; break;
    case 0x9: ea = (uint16_t)(*r + fetch16(m, c)); *cyc += 4u; break;
    case 0xA: ea = (uint16_t)(c->pc | 0xFFu); *cyc += 3u; undoc = true; break;
    case 0xB: ea = (uint16_t)(*r + get_d(c)); *cyc += 4u; break;
    case 0xC: { int8_t o = (int8_t)fetch8(m, c); ea = (uint16_t)(c->pc + o); *cyc += 1u; break; }
    case 0xD: { uint16_t o = fetch16(m, c); ea = (uint16_t)(c->pc + o); *cyc += 5u; break; }
    case 0xE: ea = 0xFFFFu; *cyc += 4u; undoc = true; break;
    default:  ea = fetch16(m, c); *cyc += 2u; undoc = (pb & 0x70u) != 0x10u; break;
    }

    /* Auto-increment and -decrement on S arm NMI, as any load of S. */
    if (r == &c->s && (pb & 0x0Eu) <= 0x02u) c->nmi_armed = true;

    if (pb & 0x10u) {                             /* indirect */
        ea = rd16(m, ea);
        *cyc += 3u;
    }
    if (__builtin_expect(undoc, 0)) undocumented(c, pc0, op);
    return ea;
}

/* ---- TFR and EXG ----------------------------------------------------- */

/* A register as TFR's and EXG's source. Mixed sizes are undefined in the
 * datasheet; the chip as measured (tlindner, which XRoar follows) reads
 * an 8-bit accumulator as $FF in the high byte, CC and DP as themselves in
 * both bytes, and an unused code as $FFFF. */
INLINE uint16_t xfr_src(const mc6809_t *c, unsigned code) {
    switch (code) {
    case 0x0: return get_d(c);
    case 0x1: return c->x;
    case 0x2: return c->y;
    case 0x3: return c->u;
    case 0x4: return c->s;
    case 0x5: return c->pc;
    case 0x8: return (uint16_t)(0xFF00u | c->a);
    case 0x9: return (uint16_t)(0xFF00u | c->b);
    case 0xA: return (uint16_t)((c->cc << 8) | c->cc);
    case 0xB: return (uint16_t)((c->dp << 8) | c->dp);
    default:  return 0xFFFFu;
    }
}

/* EXG reads its destination differently: CC and DP as $FF in the high
 * byte, like the accumulators. */
INLINE uint16_t xfr_dst_old(const mc6809_t *c, unsigned code) {
    switch (code) {
    case 0xA: return (uint16_t)(0xFF00u | c->cc);
    case 0xB: return (uint16_t)(0xFF00u | c->dp);
    default:  return xfr_src(c, code);
    }
}

INLINE void xfr_set(mc6809_t *c, unsigned code, uint16_t v) {
    switch (code) {
    case 0x0: set_d(c, v); break;
    case 0x1: c->x = v; break;
    case 0x2: c->y = v; break;
    case 0x3: c->u = v; break;
    case 0x4: c->s = v; c->nmi_armed = true; break;
    case 0x5: c->pc = v; break;
    case 0x8: c->a = (uint8_t)v; break;
    case 0x9: c->b = (uint8_t)v; break;
    case 0xA: c->cc = (uint8_t)v; break;
    case 0xB: c->dp = (uint8_t)v; break;
    default: break;
    }
}

/* Both 16-bit (0-5) or both 8-bit (8-B): anything else is undefined. */
INLINE bool xfr_documented(uint8_t pb) {
    unsigned s = pb >> 4, d = pb & 0x0Fu;
    return (s <= 5u && d <= 5u) || (s >= 8u && s <= 0xBu && d >= 8u && d <= 0xBu);
}

/* ---- interrupts ------------------------------------------------------ */

/* The whole state, E set: an IRQ, an NMI, an SWI, CWAI. */
/* Every register, as an interrupt stacks them, E as it stands: the
 * undocumented $3E, $103E and $113E (D12). */
INLINE void push_entire(coco_t *m, mc6809_t *c) {
    push16(m, &c->s, c->pc);
    push16(m, &c->s, c->u);
    push16(m, &c->s, c->y);
    push16(m, &c->s, c->x);
    push8(m, &c->s, c->dp);
    push8(m, &c->s, c->b);
    push8(m, &c->s, c->a);
    push8(m, &c->s, c->cc);
}

INLINE void stack_entire(coco_t *m, mc6809_t *c) {
    c->cc |= CC_E;
    push16(m, &c->s, c->pc);
    push16(m, &c->s, c->u);
    push16(m, &c->s, c->y);
    push16(m, &c->s, c->x);
    push8(m, &c->s, c->dp);
    push8(m, &c->s, c->b);
    push8(m, &c->s, c->a);
    push8(m, &c->s, c->cc);
}

/* FIRQ: PC and CC, E clear. */
INLINE void stack_fast(coco_t *m, mc6809_t *c) {
    c->cc = (uint8_t)(c->cc & ~CC_E);
    push16(m, &c->s, c->pc);
    push8(m, &c->s, c->cc);
}

/* Which lines CC lets through: NMI always, FIRQ unless F, IRQ unless I. */
INLINE uint8_t unmasked(uint8_t cc) {
    return (uint8_t)(MC6809_LINE_NMI | ((cc & CC_F) ? 0u : MC6809_LINE_FIRQ) |
                     ((cc & CC_I) ? 0u : MC6809_LINE_IRQ));
}

/* The vector of the highest-priority line in `take`, with its masks set;
 * the state is already stacked. Taking NMI consumes its edge, and the
 * next boundary decides on what was latched before the entry (stale). */
INLINE void vector_to(coco_t *m, mc6809_t *c, uint8_t take) {
    uint16_t vec;
    c->stale = true;
    if (take & MC6809_LINE_NMI) {
        c->nmi_pending = false;
        c->active = (uint8_t)(c->active & ~MC6809_LINE_NMI);
        c->cc |= CC_F | CC_I;
        vec = MC6809_VEC_NMI;
    } else if (take & MC6809_LINE_FIRQ) {
        c->cc |= CC_F | CC_I;
        vec = MC6809_VEC_FIRQ;
    } else {
        c->cc |= CC_I;
        vec = MC6809_VEC_IRQ;
    }
    c->pc = rd16(m, vec);
}

/* What comes before an instruction when attn is set: HALT, a wait, or an
 * interrupt. Returns the cycles taken, or 0 to go on and execute.
 *
 * `remaining` is how much of the slice is left. A HALT, or a wait whose
 * polls would change nothing, takes all of it: lines change only between
 * slices, so each skipped poll would have cost one cycle and seen what
 * the last one saw (§7.1, EL §3.4). A stepping caller passes 1 and gets
 * the polls one at a time; test_mc6809_behaviour holds the two equal. */
static uint32_t COCO_HOT1(attention)(coco_t *m, uint32_t remaining) {
    mc6809_t *c = &m->cpu;
    uint8_t now = lines(c);
    uint32_t all = remaining ? remaining : 1u;

    /* HALT: the CPU stands while the lines are latched each cycle. On
     * release the boundary takes what is asserted then. */
    if (c->halt) {
        if (all > 1u) c->active = now;
        else          c->active = c->latch;
        c->latch = now;
        c->cycles += all;
        return all;
    }

    /* HCF: locked, every line ignored, until RESET (D12). */
    if (c->wait == MC6809_HCF) {
        c->cycles += all;
        return all;
    }

    /* SYNC: each cycle samples the latch, then latches the lines. Any line
     * ends the wait, masked or not, two cycles after the poll that sees
     * it; an unmasked one is then taken at the boundary. */
    if (c->wait == MC6809_SYNC) {
        if (c->latch == 0 && now == 0) {
            c->cycles += all;
            return all;
        }
        uint8_t seen = c->latch;
        c->latch = now;
        if (seen) {
            c->wait = MC6809_RUNNING;
            c->active = seen;
            c->stale = true;
            mc6809_attn(c);
            c->cycles += 2u;
            return 2u;
        }
        c->cycles += 1u;
        return 1u;
    }

    /* CWAI: the state is stacked. Dispatch on what the last poll saw; if
     * nothing unmasked, poll another cycle. The vector costs four. */
    if (c->wait == MC6809_CWAI) {
        uint8_t take = c->active & unmasked(c->cc);
        if (take) {
            c->wait = MC6809_RUNNING;
            vector_to(m, c, take);
            mc6809_attn(c);
            c->cycles += 4u;
            return 4u;
        }
        if (c->active == c->latch && c->latch == now) {
            c->cycles += all;
            return all;
        }
        c->active = c->latch;
        c->latch = now;
        c->cycles += 1u;
        return 1u;
    }

    /* At an instruction boundary: NMI, then FIRQ, then IRQ, from the lines
     * now, or straight after an entry or SYNC from those latched before. */
    uint8_t avail = c->stale ? c->active : now;
    c->stale = false;
    uint8_t take = avail & unmasked(c->cc);
    if (!take) {
        mc6809_attn(c);
        return 0;
    }
    c->active = avail;
    uint32_t n;
    if (take & MC6809_LINE_NMI) {
        stack_entire(m, c);
        n = 19u;
    } else if (take & MC6809_LINE_FIRQ) {
        stack_fast(m, c);
        n = 10u;
    } else {
        stack_entire(m, c);
        n = 19u;
    }
    vector_to(m, c, take);
    mc6809_attn(c);
    c->cycles += n;
    return n;
}

void mc6809_reset(coco_t *m) {
    mc6809_t *c = &m->cpu;
    c->dp = 0;
    c->cc |= CC_F | CC_I;
    c->nmi_armed = false;
    c->nmi_pending = false;
    c->wait = MC6809_RUNNING;
    c->latch = c->active = 0;
    c->stale = false;
    c->pc = rd16(m, MC6809_VEC_RESET);
    c->cycles += 4u;
    mc6809_attn(c);
}

/* ---- the interpreter ------------------------------------------------- */

#define IMM8()  fetch8(m, c)
#define IMM16() fetch16(m, c)
#define DIR()   ((uint16_t)((c->dp << 8) | fetch8(m, c)))
#define EXT()   fetch16(m, c)
#define IDX()   ea_indexed(m, c, &cyc, pc0, op)

/* An 8-bit operand in each mode, at base + $00, $10, $20, $30. */
#define OP8(base, stmt)                                                  \
    case (base):        v = IMM8();        stmt; break;                  \
    case (base) + 0x10: ea = DIR(); v = RDA(ea, LAST(cyc)); stmt; break; \
    case (base) + 0x20: ea = IDX(); v = RDA(ea, LAST(cyc)); stmt; break; \
    case (base) + 0x30: ea = EXT(); v = RDA(ea, LAST(cyc)); stmt; break;

/* ADDD, SUBD and the compares: a dead cycle after the word. */
#define OP16(base, stmt)                                                 \
    case (base):        w = IMM16();          stmt; break;               \
    case (base) + 0x10: ea = DIR(); w = rd16_at(m, ea, cyc - 2u, 0); stmt; break; \
    case (base) + 0x20: ea = IDX(); w = rd16_at(m, ea, cyc - 2u, 0); stmt; break; \
    case (base) + 0x30: ea = EXT(); w = rd16_at(m, ea, cyc - 2u, 0); stmt; break;

/* The 16-bit loads: the word is the last two cycles. */
#define LD16(base, stmt)                                                 \
    case (base):        w = IMM16();          stmt; break;               \
    case (base) + 0x10: ea = DIR(); w = rd16_at(m, ea, cyc - 1u, MC6809_IO_LAST); stmt; break; \
    case (base) + 0x20: ea = IDX(); w = rd16_at(m, ea, cyc - 1u, MC6809_IO_LAST); stmt; break; \
    case (base) + 0x30: ea = EXT(); w = rd16_at(m, ea, cyc - 1u, MC6809_IO_LAST); stmt; break;

/* Stores, at base (direct) + $00, $10, $20. The value is read after the
 * address, so STX ,X++ stores the incremented X, as the chip does. */
#define ST8(base, reg)                                                   \
    case (base):        ea = DIR(); goto st8_##base;                     \
    case (base) + 0x10: ea = IDX(); goto st8_##base;                     \
    case (base) + 0x20: ea = EXT();                                      \
    st8_##base: WRA(ea, ld8(c, (reg)), LAST(cyc)); break;

#define ST16(base, reg)                                                  \
    case (base):        ea = DIR(); goto st16_##base;                    \
    case (base) + 0x10: ea = IDX(); goto st16_##base;                    \
    case (base) + 0x20: ea = EXT();                                      \
    st16_##base: wr16_at(m, ea, ld16(c, (reg)), cyc - 1u); break;

/* The unary group in memory, at $0n, $6n, $7n: read, then write. */
#define MEM(n, fn)                                                       \
    case (n):        ea = DIR(); goto mem_##n;                           \
    case 0x60 + (n): ea = IDX(); goto mem_##n;                           \
    case 0x70 + (n): ea = EXT();                                         \
    mem_##n: WRA(ea, fn(c, RDA(ea, cyc - 2u)), LAST(cyc)); break;

#define INH(n, fn)                                                       \
    case 0x40 + (n): c->a = fn(c, c->a); break;                          \
    case 0x50 + (n): c->b = fn(c, c->b); break;

/* The undocumented unary forms: counted, then as their documented twins. */
#define MEMU(n, fn)                                                      \
    case (n):        undocumented(c, pc0, op); ea = DIR(); goto memu_##n; \
    case 0x60 + (n): undocumented(c, pc0, op); ea = IDX(); goto memu_##n; \
    case 0x70 + (n): undocumented(c, pc0, op); ea = EXT();               \
    memu_##n: WRA(ea, fn(c, RDA(ea, cyc - 2u)), LAST(cyc)); break;

#define INHU(n, fn)                                                      \
    case 0x40 + (n): undocumented(c, pc0, op); c->a = fn(c, c->a); break; \
    case 0x50 + (n): undocumented(c, pc0, op); c->b = fn(c, c->b); break;

/* Pages 2 and 3, after `n` prefix bytes, the first of which chose the
 * page: each later one is a cycle and nothing else. Each returns the
 * instruction's cycles, or 0 when the opcode is not the page's, and page
 * 1's instruction runs instead, a cycle later for each prefix (D12, as
 * XRoar has it). An opcode XRoar has neither page give is a dead cycle. */
static uint32_t COCO_HOT2(page2)(coco_t *m, mc6809_t *c, uint16_t pc0, uint8_t lo, uint32_t n) {
    uint16_t op = (uint16_t)(0x1000u | lo);
    uint32_t cyc = cycles_p2[lo] ? cycles_p2[lo] + n - 1u : cycles_p1[lo] + n;
    uint16_t ea, w;

    switch (lo) {
    case 0x20:                                          /* LBRA, undocumented */
        undocumented(c, pc0, op);
        cyc = 5u + n - 1u;
        __attribute__((fallthrough));
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
    case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C:
    case 0x2D: case 0x2E: case 0x2F: {                  /* LBcc */
        uint16_t o = IMM16();
        if (branch_taken(c, lo)) {
            c->pc = (uint16_t)(c->pc + o);
            cyc++;
        }
        break;
    }

    case 0x3E:                                          /* SWI2's, E as it is */
        undocumented(c, pc0, op);
        push_entire(m, c);
        c->pc = rd16(m, MC6809_VEC_SWI2);
        cyc = 20u + n - 1u;
        break;

    case 0x3F:                                          /* SWI2 */
        stack_entire(m, c);
        c->pc = rd16(m, MC6809_VEC_SWI2);
        break;

    OP16(0x83, (void)sub16(c, get_d(c), w))             /* CMPD */
    OP16(0x8C, (void)sub16(c, c->y, w))                 /* CMPY */

    LD16(0x8E, c->y = ld16(c, w))                       /* LDY  */
    ST16(0x9F, c->y)                                    /* STY  */

    LD16(0xCE, { c->s = ld16(c, w); c->nmi_armed = true; })    /* LDS */
    ST16(0xDF, c->s)                                    /* STS  */

    case 0x8D:                                          /* LBRA, undocumented */
        undocumented(c, pc0, op);
        w = IMM16();
        c->pc = (uint16_t)(c->pc + w);
        cyc = 5u + n;
        break;

    OP16(0xC3, { undocumented(c, pc0, op); (void)add16(c, get_d(c), w); })   /* XADDD */

    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0xCC: case 0xDC: case 0xDD: case 0xEC: case 0xED: case 0xFC: case 0xFD:
        undocumented(c, pc0, op);
        cyc = n + 2u;
        break;

    default:
        return 0;
    }
    return cyc;
}

static uint32_t COCO_HOT2(page3)(coco_t *m, mc6809_t *c, uint16_t pc0, uint8_t lo, uint32_t n) {
    uint16_t op = (uint16_t)(0x1100u | lo);
    uint32_t cyc = cycles_p3[lo] ? cycles_p3[lo] + n - 1u : cycles_p1[lo] + n;
    uint16_t ea, w;

    switch (lo) {
    case 0x3E:                                          /* FIRQ's vector, E as it is */
        undocumented(c, pc0, op);
        push_entire(m, c);
        c->pc = rd16(m, MC6809_VEC_FIRQ);
        cyc = 20u + n - 1u;
        break;

    case 0x3F:                                          /* SWI3 */
        stack_entire(m, c);
        c->pc = rd16(m, MC6809_VEC_SWI3);
        break;

    OP16(0x83, (void)sub16(c, c->u, w))                 /* CMPU */
    OP16(0x8C, (void)sub16(c, c->s, w))                 /* CMPS */

    case 0x8D:                                          /* LBRA, undocumented */
        undocumented(c, pc0, op);
        w = IMM16();
        c->pc = (uint16_t)(c->pc + w);
        cyc = 5u + n;
        break;

    OP16(0xC3, { undocumented(c, pc0, op); (void)add16(c, (uint16_t)(c->u | 0xFF00u), w); })  /* XADDU */

    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x8E: case 0x9E: case 0x9F: case 0xAE: case 0xAF: case 0xBE: case 0xBF:
    case 0xCC: case 0xCE: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
    case 0xEC: case 0xED: case 0xEE: case 0xEF: case 0xFC: case 0xFD: case 0xFE: case 0xFF:
        undocumented(c, pc0, op);
        cyc = n + 2u;
        break;

    default:
        return 0;
    }
    return cyc;
}

INLINE uint32_t step(coco_t *m, uint32_t remaining) {
    mc6809_t *c = &m->cpu;

    if (__builtin_expect(c->attn, 0)) {
        uint32_t n = attention(m, remaining);
        if (n) return n;
    }

    uint16_t pc0 = c->pc;
    uint16_t op = fetch8(m, c);
    uint32_t cyc = cycles_p1[op];
    uint16_t ea, w;
    uint8_t  v;

dispatch:
    switch (op) {
    /* ---- the unary group ------------------------------------------- */
    MEM(0x00, op_neg)
    MEM(0x03, op_com)
    MEM(0x04, op_lsr)
    MEM(0x06, op_ror)
    MEM(0x07, op_asr)
    MEM(0x08, op_asl)
    MEM(0x09, op_rol)
    MEM(0x0A, op_dec)
    MEM(0x0C, op_inc)
    MEM(0x0F, op_clr)          /* reads first, as the chip does */

    case 0x0D: ea = DIR(); (void)op_tst(c, RDA(ea, cyc - 2u)); break;  /* TST: no write */
    case 0x6D: ea = IDX(); (void)op_tst(c, RDA(ea, cyc - 2u)); break;
    case 0x7D: ea = EXT(); (void)op_tst(c, RDA(ea, cyc - 2u)); break;

    case 0x0E: c->pc = DIR(); bus_jump(m, c->pc); break;   /* JMP */
    case 0x6E: c->pc = IDX(); bus_jump(m, c->pc); break;
    case 0x7E: c->pc = EXT(); bus_jump(m, c->pc); break;

    INH(0x0, op_neg)
    INH(0x3, op_com)
    INH(0x4, op_lsr)
    INH(0x6, op_ror)
    INH(0x7, op_asr)
    INH(0x8, op_asl)
    INH(0x9, op_rol)
    INH(0xA, op_dec)
    INH(0xC, op_inc)
    INH(0xD, op_tst)
    INH(0xF, op_clr)

    /* ---- undocumented, as the NMOS chip runs them (D12) ------------- */
    MEMU(0x01, op_neg)
    MEMU(0x02, op_ngc)
    MEMU(0x05, op_lsr)
    MEMU(0x0B, op_xdec)
    INHU(0x1, op_neg)
    INHU(0x2, op_ngc)
    INHU(0x5, op_lsr)
    INHU(0xB, op_xdec)
    INHU(0xE, op_xclr)

    case 0x14: case 0x15: case 0xCD:                  /* HCF: locked until RESET */
        undocumented(c, pc0, op);
        c->wait = MC6809_HCF;
        c->attn = 1;
        break;

    case 0x18: {                                      /* CC shifted under a mask */
        undocumented(c, pc0, op);
        uint8_t cc = (uint8_t)((c->cc & RD(c->pc)) << 1);   /* the operand is not consumed */
        c->cc = (uint8_t)(cc | ((cc >> 2) & 0x02u));
        break;
    }

    case 0x1B: undocumented(c, pc0, op); break;       /* NOP */
    case 0x38: undocumented(c, pc0, op); c->cc &= IMM8(); break;   /* ANDCC, a cycle longer */

    case 0x3E:                                        /* RESET's vector, E as it is */
        undocumented(c, pc0, op);
        push_entire(m, c);
        c->pc = rd16(m, MC6809_VEC_RESET);
        break;

    case 0x87: case 0xC7:                             /* a store, immediate: flags only */
        undocumented(c, pc0, op);
        (void)IMM8();
        c->cc = (uint8_t)((c->cc & ~(CC_Z | CC_V)) | CC_N);
        break;

    case 0x8F: case 0xCF: {                           /* STX, STU immediate: one byte */
        undocumented(c, pc0, op);
        uint16_t r = op == 0x8Fu ? c->x : c->u;
        (void)RD(c->pc);
        c->pc++;
        WR(c->pc, (uint8_t)r);
        c->pc++;
        c->cc = (uint8_t)((c->cc & ~(CC_Z | CC_V)) | CC_N);
        break;
    }

    /* ---- page 1's $1x ---------------------------------------------- */
    case 0x10: case 0x11: {                           /* the prefixes */
        uint32_t n = 1;
        uint8_t lo = fetch8(m, c);
        while (lo == 0x10u || lo == 0x11u) {
            n++;
            lo = fetch8(m, c);
        }
        uint16_t full = (uint16_t)(op << 8 | lo);
        uint32_t r = op == 0x10u ? page2(m, c, pc0, lo, n) : page3(m, c, pc0, lo, n);
        if (r) {
            if (n > 1u && mc6809_documented(full)) undocumented(c, pc0, full);
            cyc = r;
            break;
        }
        /* Page 1's instruction, a cycle later for each prefix; one that
         * is undocumented on page 1 too is counted there. */
        if (p1_documented(lo)) undocumented(c, pc0, full);
        op = lo;
        cyc = cycles_p1[lo] + n;
        goto dispatch;
    }

    case 0x12: break;                                 /* NOP */

    case 0x13:                                        /* SYNC */
        c->wait = MC6809_SYNC;
        c->latch = lines(c);
        c->attn = 1;
        break;

    case 0x16: w = IMM16(); c->pc = (uint16_t)(c->pc + w); break;   /* LBRA */

    case 0x17:                                        /* LBSR */
        w = IMM16();
        push16(m, &c->s, c->pc);
        c->pc = (uint16_t)(c->pc + w);
        bus_jump(m, c->pc);
        break;

    case 0x19: c->a = op_daa(c, c->a); break;         /* DAA */
    case 0x1A: c->cc |= IMM8(); break;                /* ORCC */
    case 0x1C: c->cc &= IMM8(); break;                /* ANDCC */

    case 0x1D:                                        /* SEX: V left alone */
        c->a = (c->b & 0x80u) ? 0xFFu : 0x00u;
        nz16(c, get_d(c));
        break;

    case 0x1E: {                                      /* EXG */
        uint8_t pb = IMM8();
        unsigned s = pb >> 4, d = pb & 0x0Fu;
        uint16_t from = xfr_src(c, s), to = xfr_dst_old(c, d);
        xfr_set(c, d, from);
        xfr_set(c, s, to);
        if (!xfr_documented(pb)) undocumented(c, pc0, op);
        break;
    }

    case 0x1F: {                                      /* TFR */
        uint8_t pb = IMM8();
        xfr_set(c, pb & 0x0Fu, xfr_src(c, pb >> 4));
        if (!xfr_documented(pb)) undocumented(c, pc0, op);
        break;
    }

    /* ---- short branches -------------------------------------------- */
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
    case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
        int8_t o = (int8_t)IMM8();
        if (branch_taken(c, (uint8_t)op)) c->pc = (uint16_t)(c->pc + o);
        break;
    }

    /* ---- $3x ------------------------------------------------------- */
    case 0x30: c->x = IDX();                          /* LEAX: Z only */
               c->cc = (uint8_t)((c->cc & ~CC_Z) | (c->x ? 0u : CC_Z)); break;
    case 0x31: c->y = IDX();                          /* LEAY: Z only */
               c->cc = (uint8_t)((c->cc & ~CC_Z) | (c->y ? 0u : CC_Z)); break;
    case 0x32: c->s = IDX(); c->nmi_armed = true; break;   /* LEAS */
    case 0x33: c->u = IDX(); break;                        /* LEAU */

    case 0x34: {                                      /* PSHS */
        uint8_t pb = IMM8();
        if (pb & 0x80u) { push16(m, &c->s, c->pc); cyc += 2u; }
        if (pb & 0x40u) { push16(m, &c->s, c->u);  cyc += 2u; }
        if (pb & 0x20u) { push16(m, &c->s, c->y);  cyc += 2u; }
        if (pb & 0x10u) { push16(m, &c->s, c->x);  cyc += 2u; }
        if (pb & 0x08u) { push8(m, &c->s, c->dp);  cyc++; }
        if (pb & 0x04u) { push8(m, &c->s, c->b);   cyc++; }
        if (pb & 0x02u) { push8(m, &c->s, c->a);   cyc++; }
        if (pb & 0x01u) { push8(m, &c->s, c->cc);  cyc++; }
        c->nmi_armed = true;
        break;
    }

    case 0x35: {                                      /* PULS */
        uint8_t pb = IMM8();
        if (pb & 0x01u) { c->cc = pull8(m, &c->s);  cyc++; }
        if (pb & 0x02u) { c->a  = pull8(m, &c->s);  cyc++; }
        if (pb & 0x04u) { c->b  = pull8(m, &c->s);  cyc++; }
        if (pb & 0x08u) { c->dp = pull8(m, &c->s);  cyc++; }
        if (pb & 0x10u) { c->x  = pull16(m, &c->s); cyc += 2u; }
        if (pb & 0x20u) { c->y  = pull16(m, &c->s); cyc += 2u; }
        if (pb & 0x40u) { c->u  = pull16(m, &c->s); cyc += 2u; }
        if (pb & 0x80u) { c->pc = pull16(m, &c->s); cyc += 2u; }
        c->nmi_armed = true;
        break;
    }

    case 0x36: {                                      /* PSHU */
        uint8_t pb = IMM8();
        if (pb & 0x80u) { push16(m, &c->u, c->pc); cyc += 2u; }
        if (pb & 0x40u) { push16(m, &c->u, c->s);  cyc += 2u; }
        if (pb & 0x20u) { push16(m, &c->u, c->y);  cyc += 2u; }
        if (pb & 0x10u) { push16(m, &c->u, c->x);  cyc += 2u; }
        if (pb & 0x08u) { push8(m, &c->u, c->dp);  cyc++; }
        if (pb & 0x04u) { push8(m, &c->u, c->b);   cyc++; }
        if (pb & 0x02u) { push8(m, &c->u, c->a);   cyc++; }
        if (pb & 0x01u) { push8(m, &c->u, c->cc);  cyc++; }
        break;
    }

    case 0x37: {                                      /* PULU */
        uint8_t pb = IMM8();
        if (pb & 0x01u) { c->cc = pull8(m, &c->u);  cyc++; }
        if (pb & 0x02u) { c->a  = pull8(m, &c->u);  cyc++; }
        if (pb & 0x04u) { c->b  = pull8(m, &c->u);  cyc++; }
        if (pb & 0x08u) { c->dp = pull8(m, &c->u);  cyc++; }
        if (pb & 0x10u) { c->x  = pull16(m, &c->u); cyc += 2u; }
        if (pb & 0x20u) { c->y  = pull16(m, &c->u); cyc += 2u; }
        if (pb & 0x40u) { c->s  = pull16(m, &c->u); cyc += 2u; c->nmi_armed = true; }
        if (pb & 0x80u) { c->pc = pull16(m, &c->u); cyc += 2u; }
        break;
    }

    case 0x39: c->pc = pull16(m, &c->s); break;       /* RTS */
    case 0x3A: c->x = (uint16_t)(c->x + c->b); break; /* ABX */

    case 0x3B:                                        /* RTI: by E */
        c->cc = pull8(m, &c->s);
        if (c->cc & CC_E) {
            c->a  = pull8(m, &c->s);
            c->b  = pull8(m, &c->s);
            c->dp = pull8(m, &c->s);
            c->x  = pull16(m, &c->s);
            c->y  = pull16(m, &c->s);
            c->u  = pull16(m, &c->s);
            cyc += 9u;
        }
        c->pc = pull16(m, &c->s);
        c->nmi_armed = true;
        break;

    case 0x3C:                                        /* CWAI */
        c->cc &= IMM8();
        stack_entire(m, c);
        c->wait = MC6809_CWAI;
        c->latch = c->active = lines(c);
        c->attn = 1;
        break;

    case 0x3D:                                        /* MUL: C is bit 7 */
        w = (uint16_t)(c->a * c->b);
        set_d(c, w);
        c->cc = (uint8_t)((c->cc & ~(CC_Z | CC_C)) |
                          (w ? 0u : CC_Z) | ((w & 0x80u) ? CC_C : 0u));
        break;

    case 0x3F:                                        /* SWI */
        stack_entire(m, c);
        c->cc |= CC_F | CC_I;
        c->pc = rd16(m, MC6809_VEC_SWI);
        break;

    /* ---- A and B arithmetic ---------------------------------------- */
    OP8(0x80, c->a = sub8(c, c->a, v, 0))                     /* SUBA */
    OP8(0x81, (void)sub8(c, c->a, v, 0))                      /* CMPA */
    OP8(0x82, c->a = sub8(c, c->a, v, c->cc & CC_C))          /* SBCA */
    OP8(0x84, c->a = ld8(c, c->a & v))                        /* ANDA */
    OP8(0x85, (void)ld8(c, c->a & v))                         /* BITA */
    OP8(0x86, c->a = ld8(c, v))                               /* LDA  */
    OP8(0x88, c->a = ld8(c, c->a ^ v))                        /* EORA */
    OP8(0x89, c->a = add8(c, c->a, v, c->cc & CC_C))          /* ADCA */
    OP8(0x8A, c->a = ld8(c, c->a | v))                        /* ORA  */
    OP8(0x8B, c->a = add8(c, c->a, v, 0))                     /* ADDA */

    OP8(0xC0, c->b = sub8(c, c->b, v, 0))                     /* SUBB */
    OP8(0xC1, (void)sub8(c, c->b, v, 0))                      /* CMPB */
    OP8(0xC2, c->b = sub8(c, c->b, v, c->cc & CC_C))          /* SBCB */
    OP8(0xC4, c->b = ld8(c, c->b & v))                        /* ANDB */
    OP8(0xC5, (void)ld8(c, c->b & v))                         /* BITB */
    OP8(0xC6, c->b = ld8(c, v))                               /* LDB  */
    OP8(0xC8, c->b = ld8(c, c->b ^ v))                        /* EORB */
    OP8(0xC9, c->b = add8(c, c->b, v, c->cc & CC_C))          /* ADCB */
    OP8(0xCA, c->b = ld8(c, c->b | v))                        /* ORB  */
    OP8(0xCB, c->b = add8(c, c->b, v, 0))                     /* ADDB */

    ST8(0x97, c->a)                                           /* STA  */
    ST8(0xD7, c->b)                                           /* STB  */

    /* ---- 16-bit ---------------------------------------------------- */
    OP16(0x83, set_d(c, sub16(c, get_d(c), w)))               /* SUBD */
    OP16(0xC3, set_d(c, add16(c, get_d(c), w)))               /* ADDD */
    OP16(0x8C, (void)sub16(c, c->x, w))                       /* CMPX */
    LD16(0xCC, set_d(c, ld16(c, w)))                          /* LDD  */
    LD16(0x8E, c->x = ld16(c, w))                             /* LDX  */
    LD16(0xCE, c->u = ld16(c, w))                             /* LDU  */

    ST16(0xDD, get_d(c))                                      /* STD  */
    ST16(0x9F, c->x)                                          /* STX  */
    ST16(0xDF, c->u)                                          /* STU  */

    case 0x8D: {                                              /* BSR  */
        int8_t o = (int8_t)IMM8();
        push16(m, &c->s, c->pc);
        c->pc = (uint16_t)(c->pc + o);
        bus_jump(m, c->pc);
        break;
    }
    case 0x9D: ea = DIR(); goto jsr;                          /* JSR  */
    case 0xAD: ea = IDX(); goto jsr;
    case 0xBD: ea = EXT();
    jsr:
        push16(m, &c->s, c->pc);
        c->pc = ea;
        bus_jump(m, ea);
        break;

    default:
        /* Every opcode has its case; the compiler wants the default. */
        undocumented(c, pc0, op);
        cyc = 2u;
        break;
    }

    c->cycles += cyc;
    c->instructions++;
    return cyc;
}

uint32_t COCO_HOT2(mc6809_run)(coco_t *m, uint32_t cycles) {
    /* The limit is a field, so the machine can end the run by zeroing it
     * (mc6809_stop): one load a step, measured cheaper than a flag. */
    uint32_t done = 0;
    m->cpu.run_limit = cycles;
    while (done < m->cpu.run_limit) done += step(m, m->cpu.run_limit - done);
    return done;
}

bool mc6809_fetches_next(const mc6809_t *c) {
    if (c->halt || c->wait != MC6809_RUNNING) return false;
    uint8_t avail = c->stale ? c->active : lines(c);
    return (avail & unmasked(c->cc)) == 0;
}

/* One step is a run of one cycle: the first step meets it. Not a second
 * copy of the interpreter, which the inlined step would otherwise make. */
uint32_t mc6809_step(coco_t *m) {
    return mc6809_run(m, 1u);
}
