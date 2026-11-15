/* bench.c — M2's workloads (design.md §7.5, §17). See bench.h. */
#include "bench.h"

#include <string.h>

#include "bus.h"

/* ---- The flat bus ---------------------------------------------------- */

/* The bench is the only thing behind the slow path (coco.h): a write to a
 * protected page is dropped, and nothing reads through it, since every
 * page has a read pointer. */
uint8_t bus_read_slow(coco_t *m, uint16_t a) {
    (void)m;
    (void)a;
    return 0xFFu;
}

void bus_write_slow(coco_t *m, uint16_t a, uint8_t v) {
    (void)m;
    (void)a;
    (void)v;
}

/* Every page reads from the bench's RAM; [protect_lo, protect_hi) of them
 * send their writes to the slow path, as ROM does. */
static void machine_clear(bench_t *b, unsigned protect_lo, unsigned protect_hi) {
    memset(b, 0, sizeof(*b));
    for (unsigned p = 0; p < COCO_PAGE_COUNT; p++) {
        uint8_t *base = &b->m.ram[p * COCO_PAGE_SIZE];
        b->m.page[p].read = base;
        b->m.page[p].write = (p >= protect_lo && p < protect_hi) ? NULL : base;
    }
    mc6809_init(&b->m.cpu);
}

/* ---- basic: a minimal assembler -------------------------------------- */

enum {
    L_BROMHK, L_NUM, L_RET,
    L_START, L_STMT, L_NEXT, L_NOTDIG,
    L_LOADARG, L_STOREFAC,
    L_FADD, L_FNZ, L_SHIFT, L_SH, L_ADD, L_FDONE,
    L_COUNT
};

#define FIXUPS 32

typedef struct { uint16_t at; uint8_t label; bool rel; } fixup_t;

typedef struct {
    uint8_t *mem;
    uint16_t at;
    uint16_t label[L_COUNT];
    fixup_t  fix[FIXUPS];
    unsigned nfix;
} asm_t;

static void b8(asm_t *a, uint8_t v) { a->mem[a->at++] = v; }

/* The 6809 is big-endian. */
static void b16(asm_t *a, uint16_t v) {
    b8(a, (uint8_t)(v >> 8));
    b8(a, (uint8_t)v);
}

static void here(asm_t *a, unsigned l) { a->label[l] = a->at; }

static void org(asm_t *a, uint16_t at) { a->at = at; }

/* A short branch: opcode, then an 8-bit offset to the label. */
static void br(asm_t *a, uint8_t op, unsigned l) {
    b8(a, op);
    if (a->nfix < FIXUPS)
        a->fix[a->nfix++] = (fixup_t){ a->at, (uint8_t)l, true };
    b8(a, 0);
}

/* JSR or JMP, extended, to a label. */
static void jext(asm_t *a, uint8_t op, unsigned l) {
    b8(a, op);
    if (a->nfix < FIXUPS)
        a->fix[a->nfix++] = (fixup_t){ a->at, (uint8_t)l, false };
    b16(a, 0);
}

static void resolve(asm_t *a) {
    for (unsigned i = 0; i < a->nfix; i++) {
        uint16_t at = a->fix[i].at, to = a->label[a->fix[i].label];
        if (a->fix[i].rel) {
            a->mem[at] = (uint8_t)(to - (uint16_t)(at + 1));
        } else {
            a->mem[at] = (uint8_t)(to >> 8);
            a->mem[at + 1] = (uint8_t)to;
        }
    }
}

#define OP(a, ...)                                                         \
    do {                                                                   \
        static const uint8_t code_[] = { __VA_ARGS__ };                    \
        for (size_t i_ = 0; i_ < sizeof code_; i_++)                       \
            b8((a), code_[i_]);                                            \
    } while (0)

/* Opcodes, by name and mode, as the listing below uses them: _I
 * immediate, _D direct, _X indexed, _E extended. */
enum {
    ABX = 0x3A, ADCA_D = 0x99, ADCB_D = 0xD9, ADDB_I = 0xCB, ADDD_D = 0xD3,
    ANDA_I = 0x84, BCC = 0x24, BCS = 0x25, BEQ = 0x27, BNE = 0x26, BRA = 0x20,
    CLR_D = 0x0F, CMPA_I = 0x81, DECA = 0x4A, INC_D = 0x0C, JMP_E = 0x7E,
    JSR_D = 0x9D, JSR_E = 0xBD, LDA_D = 0x96, LDA_E = 0xB6, LDA_X = 0xA6,
    LDB_D = 0xD6, LDB_I = 0xC6, LDD_D = 0xDC, LDD_X = 0xEC, LDU_D = 0xDE,
    LDX_E = 0xBE, LDX_I = 0x8E, LEAX = 0x30, LSR_D = 0x04, MUL = 0x3D,
    NEGA = 0x40, ORA_I = 0x8A, PSHS = 0x34, PULS = 0x35, ROR_D = 0x06,
    RTS = 0x39, STA_D = 0x97, STA_X = 0xA7, STB_D = 0xD7, STD_D = 0xDD,
    STD_X = 0xED, STU_D = 0xDF, STX_D = 0x9F, STX_E = 0xBF, SUBA_D = 0x90,
    SUBA_I = 0x80, SUBA_X = 0xA0, TSTA = 0x4D,
};

/* Indexed postbytes. */
#define X_POSTINC  0x80u    /* ,X+  */
#define X_POSTINC2 0x81u    /* ,X++ */
#define X_ZERO     0x84u    /* ,X   */
#define S_POSTINC  0xE0u    /* ,S+  */
#define X_PLUS1    0x01u    /* 1,X  */

/* The direct page, as Color BASIC lays it out where it matters: the
 * character fetch at $9F, with the text pointer the operand of its LDA at
 * $A6; FPA0 and FPA1 each an exponent and four mantissa bytes, most
 * significant first. */
#define GETNCH   0x9Fu
#define CHARAD   0xA6u
#define FP0EXP   0x4Fu
#define FPA0     0x50u
#define FP1EXP   0x5Cu
#define FPA1     0x5Du
#define STMT     0x41u

#define CODE     0xA000u    /* where Color BASIC's ROM is              */
#define LINE     0x0800u
#define TABLE    0x0A00u
#define STACK    0x3F00u

/* The digits 0-9 as packed five-byte floats: exponent + $80, then the
 * mantissa with its hidden top bit replaced by the sign (0, positive). */
static const uint8_t digits[10][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x81, 0x00, 0x00, 0x00, 0x00 },
    { 0x82, 0x00, 0x00, 0x00, 0x00 },
    { 0x82, 0x40, 0x00, 0x00, 0x00 },
    { 0x83, 0x00, 0x00, 0x00, 0x00 },
    { 0x83, 0x20, 0x00, 0x00, 0x00 },
    { 0x83, 0x40, 0x00, 0x00, 0x00 },
    { 0x83, 0x60, 0x00, 0x00, 0x00 },
    { 0x84, 0x00, 0x00, 0x00, 0x00 },
    { 0x84, 0x10, 0x00, 0x00, 0x00 },
};

/* Three statements, with spaces for the fetch to skip: 45, 45 and 65. */
static const char line[] =
    "1+2+3+4+5+6+7+8+9:"
    "9 + 8 + 7 + 6 + 5 + 4 + 3 + 2 + 1:"
    "5+5+5+5+5+5+5+5+5+5+5+5+5";

/* The program walks the line for ever. GETNCH fetches each character: C
 * set for a digit, Z set for ':' or the end of the line, spaces skipped. A
 * digit is loaded from the table into FPA1 and added into FPA0; a ':' or
 * the end of the line packs FPA0 into the statement's result slot. The
 * add handles two non-negative numbers: zero, the swap that puts the
 * larger exponent in FPA0, the bitwise alignment shift, the 32-bit add
 * and the renormalising shift on a carry. Signs, rounding and the
 * byte-wise shift of the real add are left out.
 *
 * A stand-in written to the shape of the ROM's inner loop, not a copy of
 * it: M3 runs the real one (design.md §17). */
void bench_basic_load(bench_t *b) {
    machine_clear(b, CODE >> 8, COCO_PAGE_COUNT);
    uint8_t *mem = b->m.ram;
    memcpy(mem + LINE, line, sizeof line);          /* with its 0 */
    memcpy(mem + TABLE, digits, sizeof digits);

    asm_t a = { .mem = mem };

    /* GETNCH and GETCCH, in RAM where the ROM copies them. */
    org(&a, GETNCH);
    OP(&a, INC_D, CHARAD + 1, BNE, 0x02, INC_D, CHARAD);
    OP(&a, LDA_E, 0x00, 0x00);                      /* operand is CHARAD */
    jext(&a, JMP_E, L_BROMHK);

    org(&a, CODE);
    here(&a, L_BROMHK);
    OP(&a, CMPA_I, ':');
    br(&a, BCC, L_RET);                             /* ':' and above */
    OP(&a, CMPA_I, ' ');
    br(&a, BNE, L_NUM);
    OP(&a, JMP_E, 0x00, GETNCH);                    /* skip a space */
    here(&a, L_NUM);
    OP(&a, SUBA_I, '0', SUBA_I, 0xD0);              /* C set for a digit */
    here(&a, L_RET);
    OP(&a, RTS);

    here(&a, L_START);
    OP(&a, LDX_I, (uint8_t)((LINE - 1) >> 8), (uint8_t)(LINE - 1),
           STX_D, CHARAD, CLR_D, STMT);
    here(&a, L_STMT);
    OP(&a, CLR_D, FP0EXP);
    here(&a, L_NEXT);
    OP(&a, JSR_D, GETNCH);
    br(&a, BCC, L_NOTDIG);
    OP(&a, ANDA_I, 0x0F, LDB_I, 5, MUL,             /* B = 5 x digit */
           LDX_I, TABLE >> 8, (uint8_t)TABLE, ABX);
    jext(&a, JSR_E, L_LOADARG);
    jext(&a, JSR_E, L_FADD);
    br(&a, BRA, L_NEXT);
    here(&a, L_NOTDIG);
    br(&a, BNE, L_NEXT);                            /* '+', and the rest */
    OP(&a, PSHS, 0x02);                             /* A */
    jext(&a, JSR_E, L_STOREFAC);
    OP(&a, PULS, 0x02, TSTA);
    br(&a, BNE, L_STMT);                            /* ':' */
    OP(&a, LDX_E, BENCH_BASIC_PASSES >> 8, (uint8_t)BENCH_BASIC_PASSES,
           LEAX, X_PLUS1,
           STX_E, BENCH_BASIC_PASSES >> 8, (uint8_t)BENCH_BASIC_PASSES);
    br(&a, BRA, L_START);

    /* LOADARG: unpack the table's float at X into FPA1. */
    here(&a, L_LOADARG);
    OP(&a, LDA_X, X_POSTINC, STA_D, FP1EXP,
           LDA_X, X_POSTINC, ORA_I, 0x80, STA_D, FPA1,
           LDD_X, X_POSTINC2, STD_D, FPA1 + 1,
           LDA_X, X_ZERO, STA_D, FPA1 + 3,
           RTS);

    /* STOREFAC: pack FPA0 into the statement's slot, and move to the next. */
    here(&a, L_STOREFAC);
    OP(&a, LDB_D, STMT,
           LDX_I, BENCH_BASIC_RESULT >> 8, (uint8_t)BENCH_BASIC_RESULT, ABX,
           LDA_D, FP0EXP, STA_X, X_POSTINC,
           LDA_D, FPA0, ANDA_I, 0x7F, STA_X, X_POSTINC,
           LDD_D, FPA0 + 1, STD_X, X_POSTINC2,
           LDA_D, FPA0 + 3, STA_X, X_ZERO,
           LDB_D, STMT, ADDB_I, 5, STB_D, STMT,
           RTS);

    /* FADD: FPA0 += FPA1. */
    here(&a, L_FADD);
    OP(&a, LDA_D, FP1EXP);
    br(&a, BEQ, L_FDONE);                           /* FPA1 is zero */
    OP(&a, LDB_D, FP0EXP);
    br(&a, BNE, L_FNZ);
    OP(&a, LDD_D, FP1EXP, STD_D, FP0EXP,            /* FPA0 is zero: copy */
           LDD_D, FPA1 + 1, STD_D, FPA0 + 1,
           LDA_D, FPA1 + 3, STA_D, FPA0 + 3,
           RTS);
    here(&a, L_FNZ);                                /* A = exp1, B = exp0 */
    OP(&a, PSHS, 0x04, SUBA_X, S_POSTINC);          /* A = exp1 - exp0 */
    br(&a, BEQ, L_ADD);
    br(&a, BCS, L_SHIFT);
    OP(&a, LDD_D, FP0EXP, LDU_D, FP1EXP,            /* FPA1 larger: swap */
           STD_D, FP1EXP, STU_D, FP0EXP,
           LDD_D, FPA0 + 1, LDU_D, FPA1 + 1,
           STD_D, FPA1 + 1, STU_D, FPA0 + 1,
           LDA_D, FPA0 + 3, LDB_D, FPA1 + 3,
           STA_D, FPA1 + 3, STB_D, FPA0 + 3,
           LDA_D, FP1EXP, SUBA_D, FP0EXP);
    here(&a, L_SHIFT);                              /* A = exp1 - exp0 < 0 */
    OP(&a, NEGA, CMPA_I, 33);
    br(&a, BCC, L_FDONE);                           /* FPA1 is negligible */
    here(&a, L_SH);
    OP(&a, LSR_D, FPA1, ROR_D, FPA1 + 1, ROR_D, FPA1 + 2, ROR_D, FPA1 + 3, DECA);
    br(&a, BNE, L_SH);
    here(&a, L_ADD);
    OP(&a, LDD_D, FPA0 + 2, ADDD_D, FPA1 + 2, STD_D, FPA0 + 2,
           LDD_D, FPA0, ADCB_D, FPA1 + 1, ADCA_D, FPA1, STD_D, FPA0);
    br(&a, BCC, L_FDONE);
    OP(&a, ROR_D, FPA0, ROR_D, FPA0 + 1, ROR_D, FPA0 + 2, ROR_D, FPA0 + 3,
           INC_D, FP0EXP);
    here(&a, L_FDONE);
    OP(&a, RTS);

    resolve(&a);
    b->m.cpu.s = STACK;
    b->m.cpu.pc = a.label[L_START];
}

/* ---- mix --------------------------------------------------------------- */

#define MIX_CODE  0x4000u
#define MIX_END   0x8000u   /* the code's pages are protected up to here */

static uint32_t mix_rng;

static uint32_t mix_rnd(void) {
    mix_rng ^= mix_rng << 13;
    mix_rng ^= mix_rng >> 17;
    mix_rng ^= mix_rng << 5;
    return mix_rng;
}

static uint8_t mix_rnd8(void) { return (uint8_t)(mix_rnd() >> 8); }

/* Whether the generator may use an opcode: documented, and not one that
 * leaves the straight line or waits (jumps, calls, returns, SWI, SYNC,
 * CWAI). Branches stay, with a zero offset, so they are decoded and
 * tested but go on either way; BSR too, which only pushes. */
static bool mix_usable(uint16_t op) {
    if (!mc6809_documented(op)) return false;
    switch (op) {
    case 0x0E: case 0x6E: case 0x7E:                  /* JMP  */
    case 0x9D: case 0xAD: case 0xBD:                  /* JSR  */
    case 0x16: case 0x17:                             /* LBRA, LBSR */
    case 0x13: case 0x3C:                             /* SYNC, CWAI */
    case 0x39: case 0x3B:                             /* RTS, RTI */
    case 0x3F: case 0x103F: case 0x113F:              /* SWI, SWI2, SWI3 */
        return false;
    default:
        return true;
    }
}

/* A documented indexed postbyte: every 5-bit offset; with bit 7 set, the
 * datasheet's modes plain and indirect; and $9F. */
static bool mix_postbyte_ok(uint8_t pb) {
    if (!(pb & 0x80u)) return true;
    if (pb == 0x9Fu) return true;
    switch (pb & 0x1Fu) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x08: case 0x09: case 0x0B: case 0x0C: case 0x0D:
    case 0x11: case 0x13: case 0x14: case 0x15: case 0x16: case 0x18:
    case 0x19: case 0x1B: case 0x1C: case 0x1D:
        return true;
    default:
        return false;
    }
}

/* Emit one indexed postbyte and its offset bytes. */
static void mix_indexed(uint8_t *mem, uint16_t *at) {
    uint8_t pb;
    do pb = mix_rnd8(); while (!mix_postbyte_ok(pb));
    mem[(*at)++] = pb;
    if (pb & 0x80u) {
        switch (pb & 0x0Fu) {
        case 0x8: case 0xC:           mem[(*at)++] = mix_rnd8(); break;
        case 0x9: case 0xD: case 0xF: mem[(*at)++] = mix_rnd8();
                                      mem[(*at)++] = mix_rnd8(); break;
        default: break;
        }
    }
}

/* One instruction at *at. */
static void mix_one(uint8_t *mem, uint16_t *at, const uint16_t *ops, unsigned nops) {
    uint16_t op = ops[mix_rnd() % nops];
    unsigned page = op >> 8;
    uint8_t lo = (uint8_t)op;
    if (page) mem[(*at)++] = (uint8_t)page;
    mem[(*at)++] = lo;

    /* The operand, by the opcode's place in its page. */
    if (page == 0x10u && lo >= 0x21u && lo <= 0x2Fu) {      /* LBcc, 0 */
        mem[(*at)++] = 0; mem[(*at)++] = 0;
        return;
    }
    if (page == 0) {
        if (lo < 0x10u) { mem[(*at)++] = mix_rnd8(); return; }          /* direct */
        if (lo >= 0x20u && lo <= 0x2Fu) { mem[(*at)++] = 0; return; }   /* Bcc, 0 */
        if (lo >= 0x30u && lo <= 0x33u) { mix_indexed(mem, at); return; } /* LEA */
        if (lo >= 0x34u && lo <= 0x37u) {                    /* PSH, PUL */
            uint8_t pb = mix_rnd8();
            if (lo == 0x35u || lo == 0x37u) pb &= 0x7Fu;     /* never PC */
            mem[(*at)++] = pb;
            return;
        }
        if (lo == 0x1Eu || lo == 0x1Fu) {                    /* EXG, TFR */
            uint8_t pb;
            for (;;) {
                pb = mix_rnd8();
                unsigned s = pb >> 4, d = pb & 0x0Fu;
                bool doc = (s <= 5u && d <= 5u) ||
                           (s >= 8u && s <= 11u && d >= 8u && d <= 11u);
                /* Nothing into PC; EXG not with it at all. */
                if (doc && d != 5u && !(lo == 0x1Eu && s == 5u)) break;
            }
            mem[(*at)++] = pb;
            return;
        }
        if (lo == 0x1Au || lo == 0x1Cu) { mem[(*at)++] = mix_rnd8(); return; } /* ORCC, ANDCC */
        if (lo == 0x8Du) { mem[(*at)++] = 0; return; }       /* BSR, 0 */
        if (lo < 0x80u) {
            switch (lo & 0xF0u) {
            case 0x60: mix_indexed(mem, at); return;
            case 0x70: mem[(*at)++] = mix_rnd8(); mem[(*at)++] = mix_rnd8(); return;
            default:   return;                               /* inherent */
            }
        }
    }
    /* $80-$FF on every page: immediate, direct, indexed, extended. */
    switch ((lo >> 4) & 3u) {
    case 0: {
        unsigned l = lo & 0x0Fu;
        bool wide = l == 0x3u || l == 0xCu || l == 0xEu;
        mem[(*at)++] = mix_rnd8();
        if (wide) mem[(*at)++] = mix_rnd8();
        return;
    }
    case 1: mem[(*at)++] = mix_rnd8(); return;
    case 2: mix_indexed(mem, at); return;
    default: mem[(*at)++] = mix_rnd8(); mem[(*at)++] = mix_rnd8(); return;
    }
}

void bench_mix_load(bench_t *b) {
    machine_clear(b, MIX_CODE >> 8, MIX_END >> 8);

    /* The opcodes the generator draws from, each equally likely. */
    static const uint16_t pages[3] = { 0x0000u, 0x1000u, 0x1100u };
    static uint16_t ops[768];
    unsigned nops = 0;
    for (unsigned p = 0; p < 3; p++)
        for (unsigned lo = 0; lo < 256; lo++) {
            uint16_t op = (uint16_t)(pages[p] | lo);
            if (mix_usable(op)) ops[nops++] = op;
        }

    /* Random data everywhere else, from the same seed every time. */
    mix_rng = 0x6809C0C0u;
    for (unsigned i = 0; i < MIX_CODE; i++) b->m.ram[i] = mix_rnd8();
    for (unsigned i = MIX_END; i < COCO_ADDR_SPACE; i++) b->m.ram[i] = mix_rnd8();

    uint16_t at = MIX_CODE;
    /* The longest instruction is five bytes, and the jump three. */
    for (unsigned i = 0; i < BENCH_MIX_INSNS && at < MIX_END - 8u; i++)
        mix_one(b->m.ram, &at, ops, nops);
    b->m.ram[at++] = 0x7Eu;                                    /* JMP MIX_CODE */
    b->m.ram[at++] = (uint8_t)(MIX_CODE >> 8);
    b->m.ram[at++] = (uint8_t)MIX_CODE;

    b->m.cpu.s = 0x3F00u;
    b->m.cpu.pc = MIX_CODE;
}

/* ---- Running --------------------------------------------------------- */

/* Each slice's overshoot is carried as debt into the next, as the
 * machine's loop will (§6), and the last slice asks only for what is
 * left, so a run ends within one instruction of its length. */
uint64_t bench_run(bench_t *b, uint64_t cycles) {
    uint64_t done = 0;
    uint32_t debt = 0;
    while (done < cycles) {
        uint32_t want = BENCH_SLICE_CYCLES - debt;
        if (want > cycles - done)
            want = (uint32_t)(cycles - done);
        uint32_t ran = mc6809_run(&b->m, want);
        debt = ran - want;
        done += ran;
    }
    return done;
}
