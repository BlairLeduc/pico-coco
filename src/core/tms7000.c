/* tms7000.c — the TMS7040 (tms7000.h, design.md §12.9).
 *
 * Every opcode's length, cycle count and status bits are Table 6-3 of
 * TI's TMS7000 Family Data Manual (SPND001C). Where MAME 0.289 differs,
 * the manual is taken and the difference noted here: NOP is 4 cycles (MAME
 * 5), and BTJOP and BTJZP one more than ANDP (MAME two more). The undefined opcodes are not in the manual; MAME runs them as
 * 5-cycle NOPs, and so does this, counting them (tms7000_t.illegal).
 */

#include "tms7000.h"

#include <string.h>

#include "hot.h"

#define ST_C TMS7000_ST_C
#define ST_N TMS7000_ST_N
#define ST_Z TMS7000_ST_Z
#define ST_I TMS7000_ST_I

/* IOCNT0's flags (write 1 to clear) and enables. */
#define IO_FLAGS 0x2Au
#define IO_KEEP  0xD5u
#define IO_INT2_FLAG 0x08u

/* Timer 1's control: start, and the external source (not modelled). */
#define T1_START  0x80u
#define T1_SOURCE 0x40u
#define T1_PRE    0x1Fu
#define T1_CLOCK  8u     /* cycles a timer clock: fosc/16 over fosc/2 */

/* ---- status ------------------------------------------------------------ */

/* C from bit 8 of a 9-bit result, N and Z from its low byte. */
static inline void set_cnz(tms7000_t *c, unsigned t) {
    c->st = (uint8_t)((c->st & 0x1Fu) | ((t >> 1) & 0x80u) | (t & 0x80u ? ST_N : 0) |
                      ((t & 0xFFu) ? 0 : ST_Z));
}

/* N and Z from a byte, C cleared. */
static inline void set_nz0(tms7000_t *c, uint8_t t) {
    set_cnz(c, t);
}

/* A subtraction's: C set when there was no borrow (Table 6-3: C is the
 * carry out of d + NOT s + 1). t is d - s - borrow as a 16-bit value. */
static inline void set_sub(tms7000_t *c, unsigned t) {
    c->st = (uint8_t)((c->st & 0x1Fu) | ((t & 0x100u) ? 0 : ST_C) | (t & 0x80u ? ST_N : 0) |
                      ((t & 0xFFu) ? 0 : ST_Z));
}

/* ---- the peripheral file ---------------------------------------------- */

/* An external interrupt line held high keeps its flag set (tms7000.h). */
static void reflag(tms7000_t *c) {
    if (c->line[TMS7000_INT1]) c->iocnt0 |= 0x02u;
    if (c->line[TMS7000_INT3]) c->iocnt0 |= 0x20u;
}

static void timer_start(tms7000_t *c) {
    c->t1_dec = c->t1_reload;
    c->t1_pre = c->t1_ctl & T1_PRE;
    c->t1_phase = 0;
}

static uint8_t pf_read(tms7000_t *c, uint8_t n) {
    switch (n) {
    case 0x00: return c->iocnt0;
    case 0x02: return c->t1_dec;
    case 0x03: return c->t1_capture;
    case 0x04: return tms7000_port_in(c, TMS7000_PORT_A);          /* input only */
    case 0x05: return c->ddr[0];
    case 0x06: return c->latch[1];                                 /* output only */
    case 0x08: {
        uint8_t in = tms7000_port_in(c, TMS7000_PORT_C);
        return (uint8_t)((in & ~c->ddr[2]) | (c->latch[2] & c->ddr[2]));
    }
    case 0x09: return c->ddr[2];
    case 0x0A: {
        uint8_t in = tms7000_port_in(c, TMS7000_PORT_D);
        return (uint8_t)((in & ~c->ddr[3]) | (c->latch[3] & c->ddr[3]));
    }
    case 0x0B: return c->ddr[3];
    default:   return 0;
    }
}

static void pf_write(tms7000_t *c, uint8_t n, uint8_t v) {
    switch (n) {
    case 0x00:
        c->iocnt0 = (uint8_t)((c->iocnt0 & (~v & IO_FLAGS)) | (v & IO_KEEP));
        reflag(c);
        break;
    case 0x02:
        c->t1_reload = v;
        break;
    case 0x03:
        /* Bit 7, 0 or 1 before, starts the timer from its reloads; 0
         * freezes it where it is (the manual's §3.7.3). */
        c->t1_ctl = v;
        if (v & T1_START) timer_start(c);
        break;
    case 0x06:
        tms7000_port_out(c, TMS7000_PORT_B, v);
        c->latch[1] = v;
        break;
    case 0x08:
    case 0x0A: {
        unsigned p = n == 0x08u ? TMS7000_PORT_C : TMS7000_PORT_D;
        tms7000_port_out(c, p, (uint8_t)(v & c->ddr[p]));
        c->latch[p] = v;
        break;
    }
    case 0x09: c->ddr[2] = v; break;
    case 0x0B: c->ddr[3] = v; break;
    default:   break;   /* port A and its direction, and the undecoded */
    }
}

/* ---- memory ------------------------------------------------------------ */

static inline uint8_t mem_read(tms7000_t *c, const uint8_t *rom, uint16_t a) {
    if (a < 0x100u) return c->rf[a];
    if (a >= TMS7000_ROM_BASE) return rom[a - TMS7000_ROM_BASE];
    if (a < 0x200u) return pf_read(c, (uint8_t)a);
    return 0;
}

static inline void mem_write(tms7000_t *c, uint16_t a, uint8_t v) {
    if (a < 0x100u) c->rf[a] = v;
    else if (a < 0x200u) pf_write(c, (uint8_t)a, v);
}

uint8_t tms7000_peek(const tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a) {
    if (a < 0x100u) return c->rf[a];
    if (a >= TMS7000_ROM_BASE) return rom[a - TMS7000_ROM_BASE];
    if (a == 0x100u) return c->iocnt0;
    return 0;
}

static inline uint8_t imm8(tms7000_t *c, const uint8_t *rom) {
    return mem_read(c, rom, c->pc++);
}

static inline uint16_t imm16(tms7000_t *c, const uint8_t *rom) {
    uint16_t hi = imm8(c, rom);
    return (uint16_t)(hi << 8 | imm8(c, rom));
}

/* A register pair: Rn-1 the high byte, Rn the low. */
static inline uint16_t rp_read(const tms7000_t *c, uint8_t r) {
    return (uint16_t)(c->rf[(uint8_t)(r - 1u)] << 8 | c->rf[r]);
}

static inline void rp_write(tms7000_t *c, uint8_t r, uint16_t v) {
    c->rf[(uint8_t)(r - 1u)] = (uint8_t)(v >> 8);
    c->rf[r] = (uint8_t)v;
}

static inline uint16_t mem16(tms7000_t *c, const uint8_t *rom, uint16_t a) {
    uint16_t hi = mem_read(c, rom, a);
    return (uint16_t)(hi << 8 | mem_read(c, rom, (uint16_t)(a + 1u)));
}

static inline void push8(tms7000_t *c, uint8_t v) { c->rf[++c->sp] = v; }
static inline uint8_t pull8(tms7000_t *c) { return c->rf[c->sp--]; }

static inline void push16(tms7000_t *c, uint16_t v) {
    push8(c, (uint8_t)(v >> 8));
    push8(c, (uint8_t)v);
}

static inline uint16_t pull16(tms7000_t *c) {
    uint16_t lo = pull8(c);
    return (uint16_t)(pull8(c) << 8 | lo);
}

/* ---- the timer --------------------------------------------------------- */

static void timer_run(tms7000_t *c, uint32_t n) {
    if ((c->t1_ctl & (T1_START | T1_SOURCE)) != T1_START) return;
    uint32_t ph = c->t1_phase + n;
    while (ph >= T1_CLOCK) {
        ph -= T1_CLOCK;
        /* The prescaler through zero decrements the timer; both through
         * zero together interrupt and reload (§3.7.7). */
        if (c->t1_pre-- == 0) {
            c->t1_pre = c->t1_ctl & T1_PRE;
            if (c->t1_dec-- == 0) {
                c->t1_dec = c->t1_reload;
                c->iocnt0 |= IO_INT2_FLAG;
            }
        }
    }
    c->t1_phase = (uint8_t)ph;
}

/* ---- the operations ---------------------------------------------------- */

enum {
    OP_MOV = 2, OP_AND, OP_OR, OP_XOR, OP_BTJO, OP_BTJZ, OP_ADD, OP_ADC,
    OP_SUB, OP_SBB, OP_MPY, OP_CMP, OP_DAC, OP_DSB
};

static uint8_t bcd_add(tms7000_t *c, uint8_t d, uint8_t s, unsigned carry) {
    unsigned lo = (d & 0x0Fu) + (s & 0x0Fu) + carry;
    unsigned sum = (unsigned)d + s + carry;
    if (lo > 9u) sum += 0x06u;
    bool cy = sum > 0x99u;
    if (cy) sum += 0x60u;
    set_nz0(c, (uint8_t)sum);
    if (cy) c->st |= ST_C;
    return (uint8_t)sum;
}

static uint8_t bcd_sub(tms7000_t *c, uint8_t d, uint8_t s, unsigned borrow) {
    int lo = (int)(d & 0x0Fu) - (int)(s & 0x0Fu) - (int)borrow;
    int diff = (int)d - (int)s - (int)borrow;
    if (lo < 0) diff -= 0x06;
    bool br = (int)d - (int)s - (int)borrow < 0;
    if (br) diff -= 0x60;
    set_nz0(c, (uint8_t)diff);
    if (!br) c->st |= ST_C;
    return (uint8_t)diff;
}

/* A dual-operand operation on d (the destination) and s; returns whether
 * it writes the result, which it leaves in *out. A Bit Test and Jump
 * leaves its condition in *out, 1 to jump. */
static bool dual(tms7000_t *c, unsigned op, uint8_t d, uint8_t s, uint8_t *out) {
    unsigned t;
    switch (op) {
    case OP_MOV: *out = s; set_nz0(c, s); return true;
    case OP_AND: *out = d & s; set_nz0(c, *out); return true;
    case OP_OR:  *out = d | s; set_nz0(c, *out); return true;
    case OP_XOR: *out = d ^ s; set_nz0(c, *out); return true;
    case OP_BTJO: t = d & s; set_nz0(c, (uint8_t)t); *out = t != 0; return false;
    case OP_BTJZ: t = (uint8_t)~d & s; set_nz0(c, (uint8_t)t); *out = t != 0; return false;
    case OP_ADD: t = (unsigned)d + s; set_cnz(c, t); *out = (uint8_t)t; return true;
    case OP_ADC: t = (unsigned)d + s + ((c->st & ST_C) ? 1u : 0u); set_cnz(c, t); *out = (uint8_t)t; return true;
    case OP_SUB: t = ((unsigned)d - s) & 0xFFFFu; set_sub(c, t); *out = (uint8_t)t; return true;
    case OP_SBB:
        t = ((unsigned)d - s - ((c->st & ST_C) ? 0u : 1u)) & 0xFFFFu;
        set_sub(c, t);
        *out = (uint8_t)t;
        return true;
    case OP_CMP: t = ((unsigned)d - s) & 0xFFFFu; set_sub(c, t); return false;
    case OP_MPY:
        t = (unsigned)d * s;
        c->rf[0] = (uint8_t)(t >> 8);   /* always into A:B */
        c->rf[1] = (uint8_t)t;
        set_nz0(c, (uint8_t)(t >> 8));
        return false;
    case OP_DAC: *out = bcd_add(c, d, s, (c->st & ST_C) ? 1u : 0u); return true;
    case OP_DSB: *out = bcd_sub(c, d, s, (c->st & ST_C) ? 0u : 1u); return true;
    default: return false;
    }
}

/* The extra cycles beyond the addressing mode's (Table 6-3). */
static inline uint32_t dual_extra(unsigned op) {
    if (op == OP_MPY) return 39u;
    if (op == OP_DAC || op == OP_DSB) return 2u;
    return 0;
}

/* A single-operand operation on v (CLR to RLC, Table 6-3); returns the
 * result. XCHB and DJNZ are their callers'. */
enum { SO_DEC = 2, SO_INC, SO_INV, SO_CLR, SO_XCHB, SO_SWAP, SO_DJNZ = 0xA,
       SO_RR = 0xC, SO_RRC, SO_RL, SO_RLC };

static uint8_t single(tms7000_t *c, unsigned op, uint8_t v) {
    unsigned t, cy = (c->st & ST_C) ? 1u : 0u;
    switch (op) {
    case SO_DEC: t = ((unsigned)v - 1u) & 0xFFFFu; set_sub(c, t); return (uint8_t)t;
    case SO_INC: t = (unsigned)v + 1u; set_cnz(c, t); return (uint8_t)t;
    case SO_INV: t = (uint8_t)~v; set_nz0(c, (uint8_t)t); return (uint8_t)t;
    case SO_CLR: set_nz0(c, 0); return 0;
    case SO_SWAP:
        /* Four rotates left: C is the original bit 4. */
        t = (unsigned)(v >> 4 | v << 4);
        set_cnz(c, t);
        return (uint8_t)t;
    case SO_RR:  t = (unsigned)(v >> 1) | (unsigned)(v & 1u) << 7 | (unsigned)(v & 1u) << 8; break;
    case SO_RRC: t = (unsigned)(v >> 1) | cy << 7 | (unsigned)(v & 1u) << 8; break;
    case SO_RL:  t = (unsigned)v << 1 | (unsigned)(v >> 7); break;
    case SO_RLC: t = (unsigned)v << 1 | cy; break;
    default: return v;
    }
    set_cnz(c, t);
    return (uint8_t)t;
}

/* ---- the interpreter --------------------------------------------------- */

static inline uint32_t jump_rel(tms7000_t *c, const uint8_t *rom, bool take) {
    int8_t off = (int8_t)imm8(c, rom);
    if (!take) return 0;
    c->pc = (uint16_t)(c->pc + off);
    return 2u;
}

static uint32_t execute(tms7000_t *c, const uint8_t *rom) {
    uint8_t op = imm8(c, rom);
    unsigned hi = op >> 4, lo = op & 0x0Fu;
    uint8_t d, s, r, out = 0;
    uint32_t cyc;
    c->instructions++;

    /* The dual-operand block, $12-$7F: the low nibble the operation, the
     * high the operands (Table 6-3). */
    if (hi >= 1u && hi <= 7u && lo >= 2u) {
        uint8_t dst;
        switch (hi) {
        case 1: s = c->rf[imm8(c, rom)]; dst = 0; cyc = 8; break;           /* Rn,A   */
        case 2: s = imm8(c, rom); dst = 0; cyc = 7; break;                  /* %n,A   */
        case 3: s = c->rf[imm8(c, rom)]; dst = 1; cyc = 8; break;           /* Rn,B   */
        case 4: s = c->rf[imm8(c, rom)]; dst = imm8(c, rom); cyc = 10; break;   /* Rn,Rn */
        case 5: s = imm8(c, rom); dst = 1; cyc = 7; break;                  /* %n,B   */
        case 6: s = c->rf[1]; dst = 0; cyc = 5; break;                      /* B,A    */
        default: s = imm8(c, rom); dst = imm8(c, rom); cyc = 9; break;      /* %n,Rn  */
        }
        d = c->rf[dst];
        if (dual(c, lo, d, s, &out)) {
            c->rf[dst] = out;
        } else if (lo == OP_BTJO || lo == OP_BTJZ) {
            cyc += 2u + jump_rel(c, rom, out != 0);
        }
        return cyc + dual_extra(lo);
    }

    /* The peripheral file's dual operations, MOVP and the P forms. */
    if ((op >= 0x82u && op <= 0x87u) || (op >= 0x92u && op <= 0x97u) || (op >= 0xA2u && op <= 0xA7u)) {
        unsigned fn = lo;   /* MOV, AND, OR, XOR, BTJO, BTJZ */
        if (hi == 8u) { s = c->rf[0]; cyc = 10; }
        else if (hi == 9u) { s = c->rf[1]; cyc = 9; }
        else { s = imm8(c, rom); cyc = 11; }
        r = imm8(c, rom);
        d = pf_read(c, r);   /* read even for MOVP, as MAME's does */
        /* BTJOP and BTJZP take one cycle more than ANDP (Table 6-3;
         * MAME 0.289 gives them two). */
        if (dual(c, fn, d, s, &out)) pf_write(c, r, out);
        else cyc += 1u + jump_rel(c, rom, out != 0);
        return cyc;
    }

    /* The single-operand block on A ($B2-$BF), B ($C2-$CF) and Rn
     * ($D2-$DF), with PUSH, POP and DECD among them. */
    if (hi >= 0xBu && hi <= 0xDu && lo >= 2u) {
        uint8_t reg = hi == 0xBu ? 0 : hi == 0xCu ? 1 : imm8(c, rom);
        uint32_t base = hi == 0xDu ? 2u : 0u;
        uint8_t v = c->rf[reg];
        switch (lo) {
        case 0x6:   /* XCHB */
            c->rf[reg] = c->rf[1];
            c->rf[1] = v;
            set_nz0(c, c->rf[reg]);
            return 6u + base;
        case 0x7:   /* SWAP */
            c->rf[reg] = single(c, SO_SWAP, v);
            return 8u + base;
        case 0x8:   /* PUSH */
            push8(c, v);
            set_nz0(c, v);
            return 6u + base;
        case 0x9:   /* POP */
            v = pull8(c);
            c->rf[reg] = v;
            set_nz0(c, v);
            return 6u + base;
        case 0xA: { /* DJNZ */
            uint8_t t = (uint8_t)(v - 1u);
            c->rf[reg] = t;
            return 7u + base + jump_rel(c, rom, t != 0);
        }
        case 0xB: { /* DECD: C cleared on $0000 to $FFFF; N and Z from the
                     * high byte, as MAME has it (Table 6-3 says R R R) */
            uint16_t t = (uint16_t)(rp_read(c, reg) - 1u);
            rp_write(c, reg, t);
            c->st = (uint8_t)((c->st & 0x1Fu) | (t == 0xFFFFu ? 0 : ST_C) | (t & 0x8000u ? ST_N : 0) |
                              ((t & 0xFF00u) ? 0 : ST_Z));
            return 9u + base;
        }
        default:
            c->rf[reg] = single(c, lo, v);
            return 5u + base;
        }
    }

    switch (op) {
    case 0x00: return 4;                                    /* NOP          */
    case 0x01:                                              /* IDLE         */
        c->pc--;
        c->idle = true;
        return 6;
    case 0x05: c->st |= ST_C | ST_N | ST_Z | ST_I; return 5;          /* EINT */
    case 0x06: c->st &= (uint8_t)~(ST_C | ST_N | ST_Z | ST_I); return 5;   /* DINT */
    case 0x07: c->st = (uint8_t)((c->st & ~ST_N) | ST_C | ST_Z); return 5; /* SETC */
    case 0x08: c->st = pull8(c) & 0xF0u; return 6;          /* POP ST       */
    case 0x09: c->rf[1] = c->sp; return 6;                  /* STSP         */
    case 0x0A: c->pc = pull16(c); return 7;                 /* RETS         */
    case 0x0B:                                              /* RETI         */
        c->pc = pull16(c);
        c->st = pull8(c) & 0xF0u;
        return 9;
    case 0x0D: c->sp = c->rf[1]; return 5;                  /* LDSP         */
    case 0x0E: push8(c, c->st); return 6;                   /* PUSH ST      */

    case 0x80: d = pf_read(c, imm8(c, rom)); c->rf[0] = d; set_nz0(c, d); return 9;   /* MOVP Pn,A */
    case 0x91: d = pf_read(c, imm8(c, rom)); c->rf[1] = d; set_nz0(c, d); return 8;   /* MOVP Pn,B */

    case 0x88: { uint16_t v = imm16(c, rom); rp_write(c, imm8(c, rom), v); set_nz0(c, (uint8_t)(v >> 8)); return 15; }
    case 0x98: { uint16_t v = rp_read(c, imm8(c, rom)); rp_write(c, imm8(c, rom), v); set_nz0(c, (uint8_t)(v >> 8)); return 14; }
    case 0xA8: { uint16_t v = (uint16_t)(imm16(c, rom) + c->rf[1]); rp_write(c, imm8(c, rom), v); set_nz0(c, (uint8_t)(v >> 8)); return 17; }

    /* The extended instructions: @label ($8x), *Rn ($9x), @label(B) ($Ax). */
    case 0x8A: case 0x9A: case 0xAA:
    case 0x8B: case 0x9B: case 0xAB:
    case 0x8C: case 0x9C: case 0xAC:
    case 0x8D: case 0x9D: case 0xAD:
    case 0x8E: case 0x9E: case 0xAE: {
        uint16_t ea;
        uint32_t add;
        if (hi == 8u) { ea = imm16(c, rom); add = 1; }
        else if (hi == 9u) { ea = rp_read(c, imm8(c, rom)); add = 0; }
        else { ea = (uint16_t)(imm16(c, rom) + c->rf[1]); add = 3; }
        switch (lo) {
        case 0xA: d = mem_read(c, rom, ea); c->rf[0] = d; set_nz0(c, d); return 10u + add;   /* LDA  */
        case 0xB: d = c->rf[0]; mem_write(c, ea, d); set_nz0(c, d); return 10u + add;        /* STA  */
        case 0xC: c->pc = ea; return 9u + add;                                               /* BR   */
        case 0xD: set_sub(c, ((unsigned)c->rf[0] - mem_read(c, rom, ea)) & 0xFFFFu); return 11u + add;   /* CMPA */
        default:  push16(c, c->pc); c->pc = ea; return 13u + add;                            /* CALL */
        }
    }

    case 0xB0: set_nz0(c, c->rf[0]); return 6;              /* TSTA / CLRC  */
    case 0xB1: c->rf[0] = c->rf[1]; set_nz0(c, c->rf[0]); return 5;   /* undocumented: MOV B,A */
    case 0xC0: c->rf[1] = c->rf[0]; set_nz0(c, c->rf[1]); return 6;   /* MOV A,B   */
    case 0xC1: set_nz0(c, c->rf[1]); return 6;              /* TSTB         */
    case 0xD0: c->rf[imm8(c, rom)] = c->rf[0]; set_nz0(c, c->rf[0]); return 8;   /* MOV A,Rn */
    case 0xD1: c->rf[imm8(c, rom)] = c->rf[1]; set_nz0(c, c->rf[1]); return 7;   /* MOV B,Rn */

    case 0xE0: return 5u + jump_rel(c, rom, true);                              /* JMP */
    case 0xE1: return 5u + jump_rel(c, rom, (c->st & ST_N) != 0);               /* JN  */
    case 0xE2: return 5u + jump_rel(c, rom, (c->st & ST_Z) != 0);               /* JZ  */
    case 0xE3: return 5u + jump_rel(c, rom, (c->st & ST_C) != 0);               /* JC  */
    case 0xE4: return 5u + jump_rel(c, rom, !(c->st & (ST_Z | ST_N)));          /* JP  */
    case 0xE5: return 5u + jump_rel(c, rom, !(c->st & ST_N));                   /* JPZ */
    case 0xE6: return 5u + jump_rel(c, rom, !(c->st & ST_Z));                   /* JNZ */
    case 0xE7: return 5u + jump_rel(c, rom, !(c->st & ST_C));                   /* JNC */

    default:
        if (op >= 0xE8u) {                                  /* TRAP 23 to 0 */
            push16(c, c->pc);
            c->pc = mem16(c, rom, (uint16_t)(0xFF00u | (uint8_t)(op << 1)));
            return 14;
        }
        c->illegal++;
        return 5;
    }
}

/* ---- interrupts -------------------------------------------------------- */

/* The level due now, or -1: the I bit set, a level enabled with its flag
 * up, INT1 first. */
static inline int due(const tms7000_t *c) {
    if (!(c->st & ST_I)) return -1;
    uint8_t io = c->iocnt0;
    for (unsigned l = 0; l < 3u; l++)
        if (((io >> (2u * l)) & 3u) == 3u) return (int)l;
    return -1;
}

uint32_t tms7000_enter(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], unsigned level) {
    uint32_t cyc = 19;
    c->entries++;
    c->iocnt0 &= (uint8_t)~(0x02u << (2u * level));
    reflag(c);
    if (c->idle) {
        c->pc++;
        c->idle = false;
        cyc = 17;
    }
    push8(c, c->st);
    push16(c, c->pc);
    c->st = 0;
    c->pc = mem16(c, rom, (uint16_t)(0xFFFCu - 2u * level));
    c->cycles += cyc;
    timer_run(c, cyc);
    return cyc;
}

uint32_t tms7000_step_no_irq(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]) {
    uint32_t cyc = execute(c, rom);
    c->cycles += cyc;
    timer_run(c, cyc);
    return cyc;
}

uint32_t COCO_HOT2(tms7000_step)(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]) {
    int l = due(c);
    if (l >= 0) return tms7000_enter(c, rom, (unsigned)l);
    return tms7000_step_no_irq(c, rom);
}

uint32_t COCO_HOT2(tms7000_run)(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t until) {
    uint64_t start = c->cycles;
    c->stop = false;
    while (c->cycles < until && !c->stop) tms7000_step(c, rom);
    return (uint32_t)(c->cycles - start);
}

uint64_t tms7000_timer_horizon(const tms7000_t *c) {
    if ((c->t1_ctl & (T1_START | T1_SOURCE)) != T1_START) return UINT64_MAX;
    /* The prescaler to zero, then the decrementer's every P + 1 clocks
     * (timer_run): pre + 1 + dec (P + 1) clocks to the flag. */
    uint64_t clocks = (uint64_t)c->t1_pre + 1u + (uint64_t)c->t1_dec * ((c->t1_ctl & T1_PRE) + 1u);
    return clocks * T1_CLOCK - c->t1_phase;
}

void tms7000_skip(tms7000_t *c, uint64_t cycles, uint64_t insns) {
    c->cycles += cycles;
    c->instructions += insns;
    if ((c->t1_ctl & (T1_START | T1_SOURCE)) != T1_START) return;
    uint64_t ph = c->t1_phase + cycles;
    uint64_t k = ph / T1_CLOCK;
    c->t1_phase = (uint8_t)(ph % T1_CLOCK);
    if (k <= c->t1_pre) {
        c->t1_pre = (uint8_t)(c->t1_pre - k);
        return;
    }
    k -= (uint64_t)c->t1_pre + 1u;
    uint64_t p1 = (c->t1_ctl & T1_PRE) + 1u;
    uint64_t steps = 1u + k / p1;
    c->t1_pre = (uint8_t)((c->t1_ctl & T1_PRE) - k % p1);
    c->t1_dec = (uint8_t)(c->t1_dec - steps);   /* short of underflow (tms7000.h) */
}

void tms7000_set_line(tms7000_t *c, unsigned level, bool high) {
    if (level != TMS7000_INT1 && level != TMS7000_INT3) return;
    bool was = c->line[level];
    c->line[level] = high;
    if (high && !was) {
        c->iocnt0 |= (uint8_t)(0x02u << (2u * level));
        if (level == TMS7000_INT3) c->t1_capture = c->t1_dec;   /* §3.7.3.1 */
    } else if (!high) {
        c->iocnt0 &= (uint8_t)~(0x02u << (2u * level));
    }
}

/* ---- power and RESET ---------------------------------------------------- */

void tms7000_init(tms7000_t *c) {
    memset(c, 0, sizeof *c);
    c->ddr[1] = 0xFFu;   /* port B is output only */
}

void tms7000_reset(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]) {
    if (c->idle) {
        c->pc++;
        c->idle = false;
    }
    /* While RESET is held: port B high, the directions of C and D to
     * inputs, their latches high (MAME's device_reset, as the manual's
     * reset table gives them). */
    pf_write(c, 0x06, 0xFF);
    pf_write(c, 0x09, 0x00);
    pf_write(c, 0x0B, 0x00);
    pf_write(c, 0x08, 0xFF);
    pf_write(c, 0x0A, 0xFF);
    /* As it rises: the status and IOCNT0, SP at $FF, and TRAP 0. */
    c->st = 0;
    pf_write(c, 0x00, 0x00);
    c->sp = 0xFFu;
    push16(c, c->pc);
    c->pc = mem16(c, rom, 0xFFFEu);
    c->cycles += 17;
    timer_run(c, 17);
}
