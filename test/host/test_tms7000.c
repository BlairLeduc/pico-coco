/* test_tms7000.c — the TMS7040 (tms7000.h, design.md §12.9).
 *
 * Without any ROM of the user's (CI runs these):
 *   - every documented opcode's cycles, executed and held to Table 6-3 of
 *     TI's TMS7000 Family Data Manual (SPND001C), the branches taken and
 *     not;
 *   - the status bits of the arithmetic, from the manual's definitions:
 *     C as no borrow after a subtraction, DEC, DECD's C on $0000, SWAP's
 *     C, the rotates, MPY into A:B, DAC and DSB, BTJO and BTJZ;
 *   - the stack, CALL, RETS, TRAP's vectors, PUSH and POP ST;
 *   - Timer 1 interrupting every 8 (PR + 1) (TR + 1) cycles (§3.7.8),
 *     and INT3's flag held while its line is high.
 *
 * With pic-7040-510.bin in the ROM directory and MAME's trace of the
 * cartridge (tools/mame/trace-ssc.sh, out/mame/ssc/), the oracle: the
 * firmware replayed instruction by instruction against MAME 0.289's
 * TMS7040, given the same bytes, every PC equal; MAME's interrupts and
 * resets made where its trace shows them; at the end the PC, SP, status
 * and the whole register file equal MAME's. Skipped without either.
 *
 * Planted one at a time (2026-11-24), each failed: a subtraction's C
 * inverted, SWAP's C from bit 0, RRC without C in, the prescaler a count
 * short, DECD's Z from both bytes, BTJZ's NOT on the source, IOCNT0's
 * flags not cleared by a written 1, an interrupt leaving the status, and
 * RETS one byte past (the behaviour tests); JP ignoring N (the oracle
 * alone, at the firmware's first JP after a negative result).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"
#include "ssc.h"
#include "test_util.h"

static uint8_t rom[TMS7000_ROM_SIZE];
static ssc_t   s;

/* ---- a ROM of the test's own ------------------------------------------- */

static unsigned at;   /* where the next byte goes, from $F000 */

static void org(uint16_t a) { at = a - TMS7000_ROM_BASE; }
static void b(uint8_t v) { rom[at++] = v; }
static void vec(uint16_t where, uint16_t to) {
    rom[where - TMS7000_ROM_BASE] = (uint8_t)(to >> 8);
    rom[where - TMS7000_ROM_BASE + 1u] = (uint8_t)to;
}

static void fresh(void) {
    memset(rom, 0x00, sizeof rom);   /* NOPs */
    vec(0xFFFE, 0xF000);
    org(0xF000);
}

static void boot(void) {
    ssc_init(&s, rom, 0);
}

/* ---- cycles ----------------------------------------------------------- */

typedef struct {
    uint8_t op;
    uint8_t len;
    uint8_t cyc;       /* not taken, for a jump */
    uint8_t taken;     /* 0: not a jump */
} op_t;

/* Table 6-3, opcode by opcode (SPND001C pp. 6-10 to 6-15). */
static const op_t table[] = {
    /* ADC ADD AND CMP DAC DSB MOV MPY OR SBB SUB XOR: B,A Rn,A Rn,B Rn,Rn %,A %,B %,Rn */
    {0x69,1,5,0},{0x19,2,8,0},{0x39,2,8,0},{0x49,3,10,0},{0x29,2,7,0},{0x59,2,7,0},{0x79,3,9,0},
    {0x68,1,5,0},{0x18,2,8,0},{0x38,2,8,0},{0x48,3,10,0},{0x28,2,7,0},{0x58,2,7,0},{0x78,3,9,0},
    {0x63,1,5,0},{0x13,2,8,0},{0x33,2,8,0},{0x43,3,10,0},{0x23,2,7,0},{0x53,2,7,0},{0x73,3,9,0},
    {0x6D,1,5,0},{0x1D,2,8,0},{0x3D,2,8,0},{0x4D,3,10,0},{0x2D,2,7,0},{0x5D,2,7,0},{0x7D,3,9,0},
    {0x6E,1,7,0},{0x1E,2,10,0},{0x3E,2,10,0},{0x4E,3,12,0},{0x2E,2,9,0},{0x5E,2,9,0},{0x7E,3,11,0},
    {0x6F,1,7,0},{0x1F,2,10,0},{0x3F,2,10,0},{0x4F,3,12,0},{0x2F,2,9,0},{0x5F,2,9,0},{0x7F,3,11,0},
    {0x62,1,5,0},{0x12,2,8,0},{0x32,2,8,0},{0x42,3,10,0},{0x22,2,7,0},{0x52,2,7,0},{0x72,3,9,0},
    {0x6C,1,44,0},{0x1C,2,47,0},{0x3C,2,47,0},{0x4C,3,49,0},{0x2C,2,46,0},{0x5C,2,46,0},{0x7C,3,48,0},
    {0x64,1,5,0},{0x14,2,8,0},{0x34,2,8,0},{0x44,3,10,0},{0x24,2,7,0},{0x54,2,7,0},{0x74,3,9,0},
    {0x6B,1,5,0},{0x1B,2,8,0},{0x3B,2,8,0},{0x4B,3,10,0},{0x2B,2,7,0},{0x5B,2,7,0},{0x7B,3,9,0},
    {0x6A,1,5,0},{0x1A,2,8,0},{0x3A,2,8,0},{0x4A,3,10,0},{0x2A,2,7,0},{0x5A,2,7,0},{0x7A,3,9,0},
    {0x65,1,5,0},{0x15,2,8,0},{0x35,2,8,0},{0x45,3,10,0},{0x25,2,7,0},{0x55,2,7,0},{0x75,3,9,0},
    /* BTJO BTJZ */
    {0x66,2,7,9},{0x16,3,10,12},{0x36,3,10,12},{0x46,4,12,14},{0x26,3,9,11},{0x56,3,9,11},{0x76,4,11,13},
    {0x67,2,7,9},{0x17,3,10,12},{0x37,3,10,12},{0x47,4,12,14},{0x27,3,9,11},{0x57,3,9,11},{0x77,4,11,13},
    /* MOV A,B  A,Rd  B,Rd; MOVD; MOVP; ANDP ORP XORP; BTJOP BTJZP */
    {0xC0,1,6,0},{0xD0,2,8,0},{0xD1,2,7,0},
    {0x88,4,15,0},{0xA8,4,17,0},{0x98,3,14,0},
    {0x82,2,10,0},{0x92,2,9,0},{0xA2,3,11,0},{0x80,2,9,0},{0x91,2,8,0},
    {0x83,2,10,0},{0x93,2,9,0},{0xA3,3,11,0},{0x84,2,10,0},{0x94,2,9,0},{0xA4,3,11,0},
    {0x85,2,10,0},{0x95,2,9,0},{0xA5,3,11,0},
    {0x86,3,11,13},{0x96,3,10,12},{0xA6,4,12,14},{0x87,3,11,13},{0x97,3,10,12},{0xA7,4,12,14},
    /* CLR DEC INC INV RL RLC RR RRC on A, B, Rd */
    {0xB5,1,5,0},{0xC5,1,5,0},{0xD5,2,7,0},{0xB2,1,5,0},{0xC2,1,5,0},{0xD2,2,7,0},
    {0xB3,1,5,0},{0xC3,1,5,0},{0xD3,2,7,0},{0xB4,1,5,0},{0xC4,1,5,0},{0xD4,2,7,0},
    {0xBE,1,5,0},{0xCE,1,5,0},{0xDE,2,7,0},{0xBF,1,5,0},{0xCF,1,5,0},{0xDF,2,7,0},
    {0xBC,1,5,0},{0xCC,1,5,0},{0xDC,2,7,0},{0xBD,1,5,0},{0xCD,1,5,0},{0xDD,2,7,0},
    /* DECD, SWAP, XCHB, CLRC/TSTA, TSTB */
    {0xBB,1,9,0},{0xCB,1,9,0},{0xDB,2,11,0},{0xB7,1,8,0},{0xC7,1,8,0},{0xD7,2,10,0},
    {0xB6,1,6,0},{0xD6,2,8,0},{0xB0,1,6,0},{0xC1,1,6,0},
    /* DJNZ */
    {0xBA,2,7,9},{0xCA,2,7,9},{0xDA,3,9,11},
    /* the jumps */
    {0xE3,2,5,7},{0xE2,2,5,7},{0xE7,2,5,7},{0xE1,2,5,7},{0xE6,2,5,7},{0xE4,2,5,7},{0xE5,2,5,7},
    /* LDA STA CMPA: @, *Rn, @(B) */
    {0x8A,3,11,0},{0x9A,2,10,0},{0xAA,3,13,0},{0x8B,3,11,0},{0x9B,2,10,0},{0xAB,3,13,0},
    {0x8D,3,12,0},{0x9D,2,11,0},{0xAD,3,14,0},
    /* the controls */
    {0x00,1,4,0},{0x05,1,5,0},{0x06,1,5,0},{0x07,1,5,0},{0x0D,1,5,0},{0x09,1,6,0},
    {0x0E,1,6,0},{0x08,1,6,0},{0xB8,1,6,0},{0xC8,1,6,0},{0xD8,2,8,0},
    {0xB9,1,6,0},{0xC9,1,6,0},{0xD9,2,8,0},
};

/* One opcode run, from a state that makes a jump go one way or the
 * other; its cycles. The interrupts are kept off. The operands: a
 * register is R2 (a source) or R3 (a destination), an immediate $FF, a
 * peripheral P2, Timer 1's decrementer. For "taken" every register but A
 * and B is $FF, and a Bit Test's destination is what makes it jump: all
 * ones for BTJO, all zeros for BTJZ (R4, which stays 0). */
static uint32_t time_op(const op_t *o, bool take) {
    unsigned hi = o->op >> 4, lo = o->op & 0x0Fu;
    bool btj = o->taken && (lo == 6u || lo == 7u) && hi <= 0xAu;
    bool btjz = btj && lo == 7u;
    fresh();
    b(o->op);
    for (unsigned i = 1; i < o->len; i++) b(0x00);
    if (btj) {
        switch (hi) {
        case 1: case 3: rom[1] = 0x02; break;                         /* Rn,A  Rn,B */
        case 4: rom[1] = 0x02; rom[2] = btjz ? 0x04 : 0x03; break;    /* Rn,Rd */
        case 2: case 5: rom[1] = 0xFF; break;                         /* %n,A  %n,B */
        case 7: rom[1] = 0xFF; rom[2] = btjz ? 0x04 : 0x03; break;    /* %n,Rd */
        case 8: case 9: rom[1] = 0x02; break;                         /* A,Pn  B,Pn */
        case 0xA: rom[1] = 0xFF; rom[2] = 0x02; break;                /* %n,Pn */
        default: break;                                               /* B,A */
        }
    }
    boot();
    s.cpu.st = 0;
    s.cpu.sp = 0x40;
    memset(s.cpu.rf, 0, sizeof s.cpu.rf);
    if (take) {
        memset(s.cpu.rf + 2, 0xFF, 254);
        s.cpu.rf[4] = 0;
        s.cpu.rf[0] = s.cpu.rf[1] = 0xFF;
    }
    if (btj && take) {
        if (hi == 8u || hi == 9u || hi == 0xAu) {
            /* The peripheral's the destination; A or B or %$FF the source. */
            s.cpu.t1_dec = btjz ? 0x00 : 0xFF;
        } else if (btjz) {
            if (hi == 1u || hi == 2u || hi == 6u) s.cpu.rf[0] = 0;   /* A the destination */
            if (hi == 3u || hi == 5u) s.cpu.rf[1] = 0;               /* B */
            if (hi == 6u) s.cpu.rf[1] = 0xFF;                        /* B,A: B the source */
        }
    }
    if (btjz && !take) {
        /* Not taken: every destination all ones, so NOT d is 0. */
        memset(s.cpu.rf, 0xFF, sizeof s.cpu.rf);
        s.cpu.t1_dec = 0xFF;
    }
    if (o->taken && !btj) {
        if (o->op == 0xBA || o->op == 0xCA || o->op == 0xDA) {
            uint8_t n = take ? 5u : 1u;
            s.cpu.rf[0] = s.cpu.rf[1] = n;
        } else {
            uint8_t st = 0;
            switch (o->op) {
            case 0xE1: st = take ? TMS7000_ST_N : 0; break;
            case 0xE2: st = take ? TMS7000_ST_Z : 0; break;
            case 0xE3: st = take ? TMS7000_ST_C : 0; break;
            case 0xE4: st = take ? 0 : TMS7000_ST_Z; break;
            case 0xE5: st = take ? 0 : TMS7000_ST_N; break;
            case 0xE6: st = take ? 0 : TMS7000_ST_Z; break;
            case 0xE7: st = take ? 0 : TMS7000_ST_C; break;
            }
            s.cpu.st = st;
        }
    }
    uint64_t c0 = s.cpu.cycles;
    tms7000_step_no_irq(&s.cpu, rom);
    return (uint32_t)(s.cpu.cycles - c0);
}

static int test_cycles(void) {
    unsigned n = 0;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        const op_t *o = &table[i];
        uint32_t c = time_op(o, false);
        uint16_t pc_after = s.cpu.pc;
        if (o->taken) {
            CHECK(c == o->cyc, "$%02X not taken: %u cycles, the manual %u", o->op, c, o->cyc);
            CHECK(pc_after == TMS7000_ROM_BASE + o->len, "$%02X not taken: PC %04X", o->op, pc_after);
            c = time_op(o, true);
            CHECK(c == o->taken, "$%02X taken: %u cycles, the manual %u", o->op, c, o->taken);
        } else {
            CHECK(c == o->cyc, "$%02X: %u cycles, the manual %u", o->op, c, o->cyc);
            if (o->op != 0x88 && o->op != 0x98 && o->op != 0xA8)
                CHECK(pc_after == TMS7000_ROM_BASE + o->len || o->op == 0x01,
                      "$%02X: PC %04X after, not past its %u bytes", o->op, pc_after, o->len);
        }
        n++;
    }
    /* JMP, BR, CALL, TRAP, RETS, RETI: their own. */
    fresh(); b(0xE0); b(0x10); boot();
    CHECK(tms7000_step_no_irq(&s.cpu, rom) == 7 && s.cpu.pc == 0xF012, "JMP: 7 cycles to $F012");
    fresh(); b(0x8C); b(0xF1); b(0x23); boot();
    CHECK(tms7000_step_no_irq(&s.cpu, rom) == 10 && s.cpu.pc == 0xF123, "BR @: 10 cycles");
    fresh(); b(0x8E); b(0xF1); b(0x23); boot();
    uint8_t sp0 = s.cpu.sp;
    CHECK(tms7000_step_no_irq(&s.cpu, rom) == 14 && s.cpu.pc == 0xF123, "CALL @: 14 cycles");
    CHECK(s.cpu.sp == (uint8_t)(sp0 + 2u) && s.cpu.rf[(uint8_t)(sp0 + 1u)] == 0xF0 &&
          s.cpu.rf[(uint8_t)(sp0 + 2u)] == 0x03, "CALL pushes the return, high byte first");
    org(0xF123); b(0x0A); s.cpu.pc = 0xF123;
    CHECK(tms7000_step_no_irq(&s.cpu, rom) == 7 && s.cpu.pc == 0xF003 && s.cpu.sp == sp0, "RETS: 7 cycles back");
    fresh(); b(0xE8); vec(0xFFD0, 0xF456); boot();
    CHECK(tms7000_step_no_irq(&s.cpu, rom) == 14 && s.cpu.pc == 0xF456, "TRAP 23 through $FFD0: 14 cycles");
    fresh(); b(0xFE); vec(0xFFFC, 0xF789); boot();
    tms7000_step_no_irq(&s.cpu, rom);
    CHECK(s.cpu.pc == 0xF789, "TRAP 1 through $FFFC");
    printf("cycles: %u opcodes as Table 6-3 has them\n", n);
    return 0;
}

/* ---- status ----------------------------------------------------------- */

/* Run a few instructions from $F000 and leave the CPU there. */
static void run_prog(const uint8_t *p, size_t n, unsigned steps) {
    fresh();
    for (size_t i = 0; i < n; i++) b(p[i]);
    boot();
    s.cpu.sp = 0x40;
    for (unsigned i = 0; i < steps; i++) tms7000_step_no_irq(&s.cpu, rom);
}

#define A  (s.cpu.rf[0])
#define B  (s.cpu.rf[1])
#define ST (s.cpu.st)
#define C_ (TMS7000_ST_C)
#define N_ (TMS7000_ST_N)
#define Z_ (TMS7000_ST_Z)

static int test_status(void) {
    /* MOV %5,A; SUB %6,A: a borrow, C clear, N set. */
    run_prog((const uint8_t[]){ 0x22, 0x05, 0x2A, 0x06 }, 4, 2);
    CHECK(A == 0xFF && (ST & (C_ | N_ | Z_)) == N_, "5 - 6: A %02X ST %02X, not $FF with N alone", A, ST);
    /* MOV %6,A; SUB %6,A: no borrow, C and Z. */
    run_prog((const uint8_t[]){ 0x22, 0x06, 0x2A, 0x06 }, 4, 2);
    CHECK(A == 0 && (ST & (C_ | N_ | Z_)) == (C_ | Z_), "6 - 6: ST %02X, not C and Z", ST);
    /* SETC; MOV %5,A; SBB %3,A: 5 - 3 - 0 = 2, C set. CLRC... SBB %3: 5 - 3 - 1 = 1. */
    run_prog((const uint8_t[]){ 0x22, 0x05, 0x07, 0x2B, 0x03 }, 5, 3);
    CHECK(A == 2 && (ST & C_), "SETC then 5 - 3 with SBB: A %02X, not 2", A);
    run_prog((const uint8_t[]){ 0x22, 0x05, 0xB0, 0x2B, 0x03 }, 5, 3);
    CHECK(A == 1, "CLRC then 5 - 3 with SBB: A %02X, not 1", A);
    /* ADD with carry out; ADC adds it. */
    run_prog((const uint8_t[]){ 0x22, 0xF0, 0x28, 0x20, 0x29, 0x00 }, 6, 3);
    CHECK(A == 0x11 && !(ST & C_), "$F0 + $20 then ADC 0: A %02X ST %02X, not $11", A, ST);
    /* CMP: d - s computed, C when d >= s. */
    run_prog((const uint8_t[]){ 0x22, 0x10, 0x2D, 0x20 }, 4, 2);
    CHECK(A == 0x10 && !(ST & C_) && (ST & N_), "CMP $20 with $10: ST %02X", ST);
    /* DEC from 0: C clear (a borrow); INC from $FF: C set, Z. */
    run_prog((const uint8_t[]){ 0xB5, 0xB2 }, 2, 2);
    CHECK(A == 0xFF && !(ST & C_), "DEC 0: A %02X ST %02X", A, ST);
    run_prog((const uint8_t[]){ 0x22, 0xFF, 0xB3 }, 3, 2);
    CHECK(A == 0 && (ST & (C_ | Z_)) == (C_ | Z_), "INC $FF: ST %02X", ST);
    /* DECD A:B ... the pair R0:R1 is A high, B low: $0100 - 1 = $00FF, C
     * set; $0000 - 1, C clear. */
    run_prog((const uint8_t[]){ 0x22, 0x01, 0x52, 0x00, 0xCB }, 5, 3);
    CHECK(A == 0x00 && B == 0xFF && (ST & C_), "DECD B on $0100: %02X%02X ST %02X", A, B, ST);
    run_prog((const uint8_t[]){ 0xB5, 0xC5, 0xCB }, 3, 3);
    CHECK(A == 0xFF && B == 0xFF && !(ST & C_), "DECD B on $0000: %02X%02X ST %02X", A, B, ST);
    /* DECD sets N and Z on the high byte alone: "JZ jumps when the MSB
     * equals zero" (the manual's DECD). $0100 - 1 = $00FF: Z. */
    run_prog((const uint8_t[]){ 0x22, 0x01, 0x52, 0x00, 0xCB }, 5, 3);
    CHECK(ST & Z_, "DECD to $00FF: Z clear, not set on the high byte");
    /* SWAP $1E: $E1, C from bit 4 (1). */
    run_prog((const uint8_t[]){ 0x22, 0x1E, 0xB7 }, 3, 2);
    CHECK(A == 0xE1 && (ST & C_) && (ST & N_), "SWAP $1E: A %02X ST %02X", A, ST);
    /* RL $81: $03, C; RLC with C set: $80 -> $01 C; RR $01: $80 C; RRC. */
    run_prog((const uint8_t[]){ 0x22, 0x81, 0xBE }, 3, 2);
    CHECK(A == 0x03 && (ST & C_), "RL $81: A %02X ST %02X", A, ST);
    run_prog((const uint8_t[]){ 0x22, 0x80, 0x07, 0xBF }, 4, 3);
    CHECK(A == 0x01 && (ST & C_), "RLC $80 with C: A %02X ST %02X", A, ST);
    run_prog((const uint8_t[]){ 0x22, 0x01, 0xBC }, 3, 2);
    CHECK(A == 0x80 && (ST & C_) && (ST & N_), "RR $01: A %02X ST %02X", A, ST);
    run_prog((const uint8_t[]){ 0x22, 0x02, 0x07, 0xBD }, 4, 3);
    CHECK(A == 0x81 && !(ST & C_), "RRC $02 with C: A %02X ST %02X", A, ST);
    /* MPY $12 by $34 into A:B, $03A8; N and Z from A. */
    run_prog((const uint8_t[]){ 0x22, 0x12, 0x2C, 0x34 }, 4, 2);
    CHECK(A == 0x03 && B == 0xA8 && !(ST & (C_ | Z_ | N_)), "MPY: %02X%02X ST %02X", A, B, ST);
    /* DAC: $19 + $28 = $47 BCD; $99 + $01 = $00, C. DSB: $42 - $15 = $27, C. */
    run_prog((const uint8_t[]){ 0xB0, 0x22, 0x19, 0x2E, 0x28 }, 5, 3);
    CHECK(A == 0x47 && !(ST & C_), "DAC $19 + $28: A %02X", A);
    run_prog((const uint8_t[]){ 0xB0, 0x22, 0x99, 0x2E, 0x01 }, 5, 3);
    CHECK(A == 0x00 && (ST & (C_ | Z_)) == (C_ | Z_), "DAC $99 + $01: A %02X ST %02X", A, ST);
    run_prog((const uint8_t[]){ 0x22, 0x42, 0x07, 0x2F, 0x15 }, 5, 3);
    CHECK(A == 0x27 && (ST & C_), "DSB $42 - $15: A %02X ST %02X", A, ST);
    /* BTJZ %$01,A: jumps if bit 0 of A is clear. */
    run_prog((const uint8_t[]){ 0x22, 0xFE, 0x27, 0x01, 0x10 }, 5, 2);
    CHECK(s.cpu.pc == 0xF005 + 0x10, "BTJZ %%1 on $FE did not jump: PC %04X", s.cpu.pc);
    run_prog((const uint8_t[]){ 0x22, 0x01, 0x27, 0x01, 0x10 }, 5, 2);
    CHECK(s.cpu.pc == 0xF005, "BTJZ %%1 on $01 jumped");
    /* XCHB A: A and B change places, the flags from the new A. */
    run_prog((const uint8_t[]){ 0x22, 0x80, 0x52, 0x00, 0xB6 }, 5, 3);
    CHECK(A == 0x00 && B == 0x80 && (ST & Z_), "XCHB A: %02X %02X ST %02X", A, B, ST);
    /* PUSH ST; DINT; POP ST: back as it was. EINT sets I, C, N, Z. */
    run_prog((const uint8_t[]){ 0x05, 0x0E, 0x06, 0x08 }, 4, 4);
    CHECK(ST == (TMS7000_ST_I | C_ | N_ | Z_), "EINT, PUSH ST, DINT, POP ST: ST %02X", ST);
    return 0;
}

/* ---- the timer and the lines ------------------------------------------ */

static int test_timer(void) {
    /* MOVP %3,P2 (reload 3); MOVP %$81,P3 (start, prescale 1); MOVP %4,P0
     * (INT2 enabled); EINT; then NOPs. The interrupt is due every
     * 8 x 2 x 4 = 64 cycles after the start. */
    fresh();
    b(0xA2); b(0x03); b(0x02);
    b(0xA2); b(0x81); b(0x03);
    b(0xA2); b(0x04); b(0x00);
    b(0x05);
    vec(0xFFFA, 0xF800);
    org(0xF800); b(0x0B);   /* RETI */
    boot();
    for (int i = 0; i < 2; i++) tms7000_step(&s.cpu, rom);
    uint64_t started = s.cpu.cycles;
    uint64_t taken[4];
    unsigned n = 0;
    while (n < 4 && s.cpu.cycles < started + 1000u) {
        uint16_t pc = s.cpu.pc;
        tms7000_step(&s.cpu, rom);
        if (s.cpu.pc == 0xF800 && pc != 0xF800) taken[n++] = s.cpu.cycles;
    }
    CHECK(n == 4, "Timer 1 interrupted %u times in 1,000 cycles", n);
    for (unsigned i = 1; i < n; i++) {
        int64_t gap = (int64_t)(taken[i] - taken[i - 1]);
        CHECK(gap >= 64 - 19 && gap <= 64 + 19, "interrupts %u and %u %lld cycles apart, not 64", i - 1, i, (long long)gap);
    }
    int64_t span = (int64_t)(taken[3] - taken[0]);
    CHECK(span >= 3 * 64 - 8 && span <= 3 * 64 + 8, "three timer periods took %lld cycles, not 192", (long long)span);
    /* The control: prescale 3 halves the rate. */
    fresh();
    b(0xA2); b(0x03); b(0x02); b(0xA2); b(0x83); b(0x03);
    b(0xA2); b(0x04); b(0x00); b(0x05);
    vec(0xFFFA, 0xF800); org(0xF800); b(0x0B);
    boot();
    unsigned m = 0;
    while (s.cpu.cycles < 1000u) {
        uint16_t pc = s.cpu.pc;
        tms7000_step(&s.cpu, rom);
        m += s.cpu.pc == 0xF800 && pc != 0xF800;
    }
    CHECK(m >= 6 && m <= 8, "prescale 3: %u interrupts in 1,000 cycles, not about 7", m);

    /* INT3: its flag while the line is high, cleared and set again by a
     * write while it stays high, gone when it falls. */
    fresh();
    boot();
    tms7000_set_line(&s.cpu, TMS7000_INT3, true);
    CHECK(s.cpu.iocnt0 & 0x20u, "INT3 high and no flag");
    fresh(); b(0xA2); b(0x20); b(0x00); boot();
    tms7000_set_line(&s.cpu, TMS7000_INT3, true);
    tms7000_step(&s.cpu, rom);
    CHECK(s.cpu.iocnt0 & 0x20u, "INT3's flag cleared while its line was high");
    tms7000_set_line(&s.cpu, TMS7000_INT3, false);
    CHECK(!(s.cpu.iocnt0 & 0x20u), "INT3's flag stayed when its line fell");
    /* IOCNT0: a 1 written to a flag clears it, a 0 leaves it. */
    fresh(); b(0xA2); b(0x00); b(0x00); b(0xA2); b(0x08); b(0x00); boot();
    s.cpu.iocnt0 |= 0x08u;
    tms7000_step(&s.cpu, rom);
    CHECK(s.cpu.iocnt0 & 0x08u, "writing 0 to INT2's flag cleared it");
    tms7000_step(&s.cpu, rom);
    CHECK(!(s.cpu.iocnt0 & 0x08u), "writing 1 to INT2's flag left it");

    /* An interrupt's entry: the status and the PC pushed, the status
     * cleared, the flag taken; RETI puts both back. */
    fresh(); b(0xA2); b(0x10); b(0x00); b(0x07); b(0x05); b(0x00);
    vec(0xFFF8, 0xF900); org(0xF900); b(0x0B);
    boot();
    s.cpu.sp = 0x40;
    for (int i = 0; i < 3; i++) tms7000_step(&s.cpu, rom);   /* INT3 enabled, SETC, EINT */
    uint8_t st_before = s.cpu.st;
    tms7000_set_line(&s.cpu, TMS7000_INT3, true);
    tms7000_set_line(&s.cpu, TMS7000_INT3, false);
    s.cpu.iocnt0 |= 0x20u;   /* a pulse: the flag alone */
    uint32_t c = tms7000_step(&s.cpu, rom);
    CHECK(s.cpu.pc == 0xF900 && c == 19, "INT3 not taken through $FFF8 in 19 cycles: PC %04X, %u", s.cpu.pc, c);
    CHECK(s.cpu.st == 0, "the status %02X in the handler, not cleared", s.cpu.st);
    CHECK(!(s.cpu.iocnt0 & 0x20u), "INT3's flag still up in its handler");
    CHECK(s.cpu.rf[0x41] == st_before && s.cpu.rf[0x42] == 0xF0 && s.cpu.rf[0x43] == 0x05,
          "the stack: %02X %02X %02X, not the status then $F005", s.cpu.rf[0x41], s.cpu.rf[0x42], s.cpu.rf[0x43]);
    tms7000_step(&s.cpu, rom);
    CHECK(s.cpu.pc == 0xF005 && s.cpu.st == st_before && s.cpu.sp == 0x40, "RETI: PC %04X ST %02X", s.cpu.pc, s.cpu.st);
    printf("timer: interrupts every 64 cycles at prescale 1, reload 3; INT3's flag follows its line\n");
    return 0;
}

/* ---- the oracle -------------------------------------------------------- */

/* Each instruction's PC, and before it the interrupt MAME's trace says
 * was taken, as "(interrupted at XXXX, IRQ n)": n + 1, or 0 for none. */
static uint16_t *trace_pc;
static uint8_t  *trace_irq;
static size_t    trace_n;

static bool load_trace(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t cap = 1u << 20;
    trace_pc = malloc(cap * sizeof *trace_pc);
    trace_irq = malloc(cap);
    char line[256];
    uint8_t irq = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned pc, from, n;
        if (sscanf(line, " (interrupted at %X, IRQ %u)", &from, &n) == 2) {
            irq = (uint8_t)(n + 1u);
            continue;
        }
        if (sscanf(line, "%4X:", &pc) != 1) continue;
        if (trace_n == cap) {
            cap *= 2;
            trace_pc = realloc(trace_pc, cap * sizeof *trace_pc);
            trace_irq = realloc(trace_irq, cap);
        }
        trace_irq[trace_n] = irq;
        trace_pc[trace_n++] = (uint16_t)pc;
        irq = 0;
    }
    fclose(f);
    return trace_n > 0;
}

static uint8_t bytes[1024];
static size_t  n_bytes;

static void load_bytes(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        unsigned fr, v;
        if (sscanf(line, "%u byte %x", &fr, &v) == 2 && n_bytes < sizeof bytes) bytes[n_bytes++] = (uint8_t)v;
    }
    fclose(f);
}

static int test_oracle(const char *dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/pic-7040-510.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("oracle: skip, no %s\n", path);
        return 0;
    }
    size_t got = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(got == sizeof rom, "%s: %zu bytes, not 4 KB", path, got);
    const char *tdir = getenv("PICO_COCO_SSC_TRACE");
    if (!tdir) tdir = PICO_COCO_SOURCE_DIR "/out/mame/ssc";
    snprintf(path, sizeof path, "%s/ssc.tr", tdir);
    if (!load_trace(path)) {
        printf("oracle: skip, no MAME trace at %s (tools/mame/trace-ssc.sh)\n", path);
        return 0;
    }
    snprintf(path, sizeof path, "%s/ssc.bytes", tdir);
    load_bytes(path);

    ssc_init(&s, rom, 0);
    size_t next_byte = 0, entries[3] = { 0, 0, 0 }, resets = 0;
    for (size_t i = 0; i < trace_n; i++) {
        uint16_t want = trace_pc[i];
        /* An interrupt where MAME's trace says one was taken: INT3 brings
         * the next byte, as the 6809's write did. */
        if (trace_irq[i]) {
            unsigned level = trace_irq[i] - 1u;
            if (level == TMS7000_INT3) {
                if (next_byte < n_bytes) s.porta = bytes[next_byte++];
                s.busy = true;
                tms7000_set_line(&s.cpu, TMS7000_INT3, true);
            }
            CHECK(level < 3u, "MAME's trace has an IRQ %u", level);
            tms7000_enter(&s.cpu, rom, level);
            entries[level]++;
        }
        if (s.cpu.pc != want) {
            /* A jump with no instruction or interrupt to make it: the
             * 6809's reset through $FF7D. */
            if (want == 0xF000) {
                tms7000_reset(&s.cpu, rom);
                s.busy = false;
                resets++;
            }
            if (s.cpu.pc != want) {
                printf("oracle: parts at instruction %zu: MAME at %04X, ours at %04X\n", i, want, s.cpu.pc);
                for (size_t k = i > 8 ? i - 8 : 0; k <= i; k++) printf("  %zu: %04X\n", k, trace_pc[k]);
                CHECK(false, "the firmware's path parted from MAME's");
            }
        }
        tms7000_step_no_irq(&s.cpu, rom);
    }
    printf("oracle: %zu instructions agree; %zu bytes given, %zu INT1, %zu INT2, %zu INT3 entries, %zu resets\n",
           trace_n, next_byte, entries[0], entries[1], entries[2], resets);
    CHECK(next_byte == n_bytes, "the firmware took %zu of the %zu bytes", next_byte, n_bytes);

    /* The end: MAME's registers and register file. */
    snprintf(path, sizeof path, "%s/ssc.end", tdir);
    f = fopen(path, "r");
    if (f) {
        unsigned pc, sp, st;
        CHECK(fscanf(f, "PC %X SP %X ST %X", &pc, &sp, &st) == 3, "ssc.end unreadable");
        uint8_t rf[256];
        for (unsigned a = 0; a < 256; a++) {
            unsigned v;
            CHECK(fscanf(f, "%2X", &v) == 1, "ssc.end: register file short at R%u", a);
            rf[a] = (uint8_t)v;
        }
        fclose(f);
        if (s.cpu.pc != pc && (pc == 0xF012 || pc == 0xF21C || pc == 0xF033))
            tms7000_enter(&s.cpu, rom, pc == 0xF012 ? TMS7000_INT3 : pc == 0xF21C ? TMS7000_INT1 : TMS7000_INT2);
        CHECK(s.cpu.pc == pc && s.cpu.sp == sp && s.cpu.st == st,
              "at the end: PC %04X SP %02X ST %02X, MAME's %04X %02X %02X", s.cpu.pc, s.cpu.sp, s.cpu.st, pc, sp, st);
        unsigned diffs = 0;
        for (unsigned a = 0; a < 256; a++) {
            if (s.cpu.rf[a] != rf[a]) {
                if (diffs < 8) printf("  R%u: ours %02X, MAME's %02X\n", a, s.cpu.rf[a], rf[a]);
                diffs++;
            }
        }
        CHECK(diffs == 0, "%u of the 256 registers differ from MAME's at the end", diffs);
        printf("oracle: at the end, the PC, SP, status and register file equal MAME's\n");
    }
    free(trace_pc);
    free(trace_irq);
    return 0;
}

int main(void) {
    if (test_cycles()) return 1;
    if (test_status()) return 1;
    if (test_timer()) return 1;
    const char *dir;
    guest_find_roms(&dir);
    if (test_oracle(dir)) return 1;
    TEST_DONE();
}
