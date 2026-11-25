/* sp0256.c — the SP0256-AL2 (sp0256.h, design.md §12.9). */

#include "sp0256.h"

#include <string.h>

#include "hot.h"

/* The parameter registers. */
enum { R_AM, R_PR, R_B0, R_F0, R_B1, R_F1, R_B2, R_F2, R_B3, R_F3, R_B4, R_F4, R_B5, R_F5, R_IA, R_IP };

/* How a field is applied: clear every register first; clear B5 and F5;
 * add it, sign-extended (a delta); insert it above its shift, keeping
 * the bits below (a field). Otherwise it replaces the register. */
#define F_CLRA  0x01u
#define F_CLR5  0x02u
#define F_DELTA 0x04u
#define F_FIELD 0x08u

typedef struct {
    uint8_t reg, len, shift, flags;
} field_t;

/* Every field each loading opcode reads, in order (sp0256.h): the
 * register, the bits read, how far they are shifted up, and how they are
 * applied. Zbiciak's table for jzIntv, as MAME 0.289's sp0256.cpp
 * carries it (THIRD-PARTY.md), in this file's own form; a length of 0
 * reads nothing. */
static const field_t fields[177] = {
    { R_AM, 0, 0, F_CLRA },              /*   0 */
    { R_AM, 8, 0, F_CLRA },              /*   1 */
    { R_PR, 8, 0, 0 },                   /*   2 */
    { R_B0, 8, 0, 0 },                   /*   3 */
    { R_F0, 8, 0, 0 },                   /*   4 */
    { R_B1, 8, 0, 0 },                   /*   5 */
    { R_F1, 8, 0, 0 },                   /*   6 */
    { R_B2, 8, 0, 0 },                   /*   7 */
    { R_F2, 8, 0, 0 },                   /*   8 */
    { R_B3, 8, 0, 0 },                   /*   9 */
    { R_F3, 8, 0, 0 },                   /*  10 */
    { R_B4, 8, 0, 0 },                   /*  11 */
    { R_F4, 8, 0, 0 },                   /*  12 */
    { R_B5, 8, 0, 0 },                   /*  13 */
    { R_F5, 8, 0, 0 },                   /*  14 */
    { R_IA, 8, 0, 0 },                   /*  15 */
    { R_IP, 8, 0, 0 },                   /*  16 */
    { R_AM, 6, 2, F_CLRA },              /*  17 */
    { R_PR, 8, 0, 0 },                   /*  18 */
    { R_B3, 4, 3, 0 },                   /*  19 */
    { R_F3, 6, 2, 0 },                   /*  20 */
    { R_B4, 7, 1, 0 },                   /*  21 */
    { R_F4, 6, 2, 0 },                   /*  22 */
    { R_B5, 8, 0, 0 },                   /*  23 */
    { R_F5, 8, 0, 0 },                   /*  24 */
    { R_AM, 6, 2, F_CLRA },              /*  25 */
    { R_PR, 8, 0, 0 },                   /*  26 */
    { R_B3, 6, 1, 0 },                   /*  27 */
    { R_F3, 7, 1, 0 },                   /*  28 */
    { R_B4, 8, 0, 0 },                   /*  29 */
    { R_F4, 8, 0, 0 },                   /*  30 */
    { R_B5, 8, 0, 0 },                   /*  31 */
    { R_F5, 8, 0, 0 },                   /*  32 */
    { R_AM, 0, 0, F_CLR5 },              /*  33 */
    { R_AM, 6, 2, 0 },                   /*  34 */
    { R_F3, 6, 2, F_FIELD },             /*  35 */
    { R_F4, 6, 2, F_FIELD },             /*  36 */
    { R_F5, 8, 0, F_FIELD },             /*  37 */
    { R_AM, 0, 0, F_CLR5 },              /*  38 */
    { R_AM, 6, 2, 0 },                   /*  39 */
    { R_F3, 7, 1, F_FIELD },             /*  40 */
    { R_F4, 8, 0, F_FIELD },             /*  41 */
    { R_F5, 8, 0, F_FIELD },             /*  42 */
    { 0, 0, 0, 0 },                    /*  43 unused */
    { 0, 0, 0, 0 },                    /*  44 unused */
    { R_AM, 4, 2, F_DELTA },             /*  45 */
    { R_PR, 5, 0, F_DELTA },             /*  46 */
    { R_B0, 3, 4, F_DELTA },             /*  47 */
    { R_F0, 3, 3, F_DELTA },             /*  48 */
    { R_B1, 3, 4, F_DELTA },             /*  49 */
    { R_F1, 3, 3, F_DELTA },             /*  50 */
    { R_B2, 3, 4, F_DELTA },             /*  51 */
    { R_F2, 3, 3, F_DELTA },             /*  52 */
    { R_B3, 3, 3, F_DELTA },             /*  53 */
    { R_F3, 4, 2, F_DELTA },             /*  54 */
    { R_B4, 4, 1, F_DELTA },             /*  55 */
    { R_F4, 4, 2, F_DELTA },             /*  56 */
    { R_B5, 5, 0, F_DELTA },             /*  57 */
    { R_F5, 5, 0, F_DELTA },             /*  58 */
    { R_AM, 4, 2, F_DELTA },             /*  59 */
    { R_PR, 5, 0, F_DELTA },             /*  60 */
    { R_B0, 4, 1, F_DELTA },             /*  61 */
    { R_F0, 4, 2, F_DELTA },             /*  62 */
    { R_B1, 4, 1, F_DELTA },             /*  63 */
    { R_F1, 4, 2, F_DELTA },             /*  64 */
    { R_B2, 4, 1, F_DELTA },             /*  65 */
    { R_F2, 4, 2, F_DELTA },             /*  66 */
    { R_B3, 4, 1, F_DELTA },             /*  67 */
    { R_F3, 5, 1, F_DELTA },             /*  68 */
    { R_B4, 5, 0, F_DELTA },             /*  69 */
    { R_F4, 5, 0, F_DELTA },             /*  70 */
    { R_B5, 5, 0, F_DELTA },             /*  71 */
    { R_F5, 5, 0, F_DELTA },             /*  72 */
    { R_AM, 0, 0, F_CLR5 },              /*  73 */
    { R_AM, 6, 2, 0 },                   /*  74 */
    { R_F0, 5, 3, F_FIELD },             /*  75 */
    { R_F1, 5, 3, F_FIELD },             /*  76 */
    { R_F2, 5, 3, F_FIELD },             /*  77 */
    { R_AM, 0, 0, F_CLR5 },              /*  78 */
    { R_AM, 6, 2, 0 },                   /*  79 */
    { R_F0, 6, 2, F_FIELD },             /*  80 */
    { R_F1, 6, 2, F_FIELD },             /*  81 */
    { R_F2, 6, 2, F_FIELD },             /*  82 */
    { R_AM, 6, 2, F_CLRA },              /*  83 */
    { R_PR, 8, 0, 0 },                   /*  84 */
    { R_B0, 3, 4, 0 },                   /*  85 */
    { R_F0, 5, 3, 0 },                   /*  86 */
    { R_B1, 3, 4, 0 },                   /*  87 */
    { R_F1, 5, 3, 0 },                   /*  88 */
    { R_B2, 3, 4, 0 },                   /*  89 */
    { R_F2, 5, 3, 0 },                   /*  90 */
    { R_B3, 4, 3, 0 },                   /*  91 */
    { R_F3, 6, 2, 0 },                   /*  92 */
    { R_B4, 7, 1, 0 },                   /*  93 */
    { R_F4, 6, 2, 0 },                   /*  94 */
    { R_IA, 5, 0, 0 },                   /*  95 */
    { R_IP, 5, 0, 0 },                   /*  96 */
    { R_AM, 6, 2, F_CLRA },              /*  97 */
    { R_PR, 8, 0, 0 },                   /*  98 */
    { R_B0, 6, 1, 0 },                   /*  99 */
    { R_F0, 6, 2, 0 },                   /* 100 */
    { R_B1, 6, 1, 0 },                   /* 101 */
    { R_F1, 6, 2, 0 },                   /* 102 */
    { R_B2, 6, 1, 0 },                   /* 103 */
    { R_F2, 6, 2, 0 },                   /* 104 */
    { R_B3, 6, 1, 0 },                   /* 105 */
    { R_F3, 7, 1, 0 },                   /* 106 */
    { R_B4, 8, 0, 0 },                   /* 107 */
    { R_F4, 8, 0, 0 },                   /* 108 */
    { R_IA, 5, 0, 0 },                   /* 109 */
    { R_IP, 5, 0, 0 },                   /* 110 */
    { R_AM, 4, 2, F_DELTA },             /* 111 */
    { R_PR, 5, 0, F_DELTA },             /* 112 */
    { R_B3, 3, 3, F_DELTA },             /* 113 */
    { R_F3, 4, 2, F_DELTA },             /* 114 */
    { R_B4, 4, 1, F_DELTA },             /* 115 */
    { R_F4, 4, 2, F_DELTA },             /* 116 */
    { R_B5, 5, 0, F_DELTA },             /* 117 */
    { R_F5, 5, 0, F_DELTA },             /* 118 */
    { R_AM, 4, 2, F_DELTA },             /* 119 */
    { R_PR, 5, 0, F_DELTA },             /* 120 */
    { R_B3, 4, 1, F_DELTA },             /* 121 */
    { R_F3, 5, 1, F_DELTA },             /* 122 */
    { R_B4, 5, 0, F_DELTA },             /* 123 */
    { R_F4, 5, 0, F_DELTA },             /* 124 */
    { R_B5, 5, 0, F_DELTA },             /* 125 */
    { R_F5, 5, 0, F_DELTA },             /* 126 */
    { R_AM, 6, 2, 0 },                   /* 127 */
    { R_PR, 8, 0, 0 },                   /* 128 */
    { R_AM, 6, 2, F_CLRA },              /* 129 */
    { R_PR, 8, 0, 0 },                   /* 130 */
    { R_B0, 3, 4, 0 },                   /* 131 */
    { R_F0, 5, 3, 0 },                   /* 132 */
    { R_B1, 3, 4, 0 },                   /* 133 */
    { R_F1, 5, 3, 0 },                   /* 134 */
    { R_B2, 3, 4, 0 },                   /* 135 */
    { R_F2, 5, 3, 0 },                   /* 136 */
    { R_B3, 4, 3, 0 },                   /* 137 */
    { R_F3, 6, 2, 0 },                   /* 138 */
    { R_B4, 7, 1, 0 },                   /* 139 */
    { R_F4, 6, 2, 0 },                   /* 140 */
    { R_B5, 8, 0, 0 },                   /* 141 */
    { R_F5, 8, 0, 0 },                   /* 142 */
    { R_IA, 5, 0, 0 },                   /* 143 */
    { R_IP, 5, 0, 0 },                   /* 144 */
    { R_AM, 6, 2, F_CLRA },              /* 145 */
    { R_PR, 8, 0, 0 },                   /* 146 */
    { R_B0, 6, 1, 0 },                   /* 147 */
    { R_F0, 6, 2, 0 },                   /* 148 */
    { R_B1, 6, 1, 0 },                   /* 149 */
    { R_F1, 6, 2, 0 },                   /* 150 */
    { R_B2, 6, 1, 0 },                   /* 151 */
    { R_F2, 6, 2, 0 },                   /* 152 */
    { R_B3, 6, 1, 0 },                   /* 153 */
    { R_F3, 7, 1, 0 },                   /* 154 */
    { R_B4, 8, 0, 0 },                   /* 155 */
    { R_F4, 8, 0, 0 },                   /* 156 */
    { R_B5, 8, 0, 0 },                   /* 157 */
    { R_F5, 8, 0, 0 },                   /* 158 */
    { R_IA, 5, 0, 0 },                   /* 159 */
    { R_IP, 5, 0, 0 },                   /* 160 */
    { R_AM, 0, 0, F_CLR5 },              /* 161 */
    { R_AM, 6, 2, 0 },                   /* 162 */
    { R_PR, 8, 0, 0 },                   /* 163 */
    { R_F0, 5, 3, F_FIELD },             /* 164 */
    { R_F1, 5, 3, F_FIELD },             /* 165 */
    { R_F2, 5, 3, F_FIELD },             /* 166 */
    { R_IA, 5, 0, 0 },                   /* 167 */
    { R_IP, 5, 0, 0 },                   /* 168 */
    { R_AM, 0, 0, F_CLR5 },              /* 169 */
    { R_AM, 6, 2, 0 },                   /* 170 */
    { R_PR, 8, 0, 0 },                   /* 171 */
    { R_F0, 6, 2, F_FIELD },             /* 172 */
    { R_F1, 6, 2, F_FIELD },             /* 173 */
    { R_F2, 6, 2, F_FIELD },             /* 174 */
    { R_IA, 5, 0, 0 },                   /* 175 */
    { R_IP, 5, 0, 0 },                   /* 176 */
};

/* For each opcode, as the chip fetches it (least significant bit first;
 * GI's own notation, most significant first, in the comment), and each
 * of the four modes (SETMODE's bits, 00 01 10 11): the first and last of
 * its fields above, or -1 for an opcode that loads none. */
static const int16_t field_index[16][4][2] = {
    /*  0 0000 */ { { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    /*  1 1000 */ { { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    /*  2 0100 */ { { 17, 22 }, { 17, 24 }, { 25, 30 }, { 25, 32 } },
    /*  3 1100 */ { { 83, 94 }, { 129, 142 }, { 97, 108 }, { 145, 158 } },
    /*  4 0010 */ { { 83, 96 }, { 129, 144 }, { 97, 110 }, { 145, 160 } },
    /*  5 1010 */ { { 73, 77 }, { 74, 77 }, { 78, 82 }, { 79, 82 } },
    /*  6 0110 */ { { 33, 36 }, { 34, 37 }, { 38, 41 }, { 39, 42 } },
    /*  7 1110 */ { { 127, 128 }, { 127, 128 }, { 127, 128 }, { 127, 128 } },
    /*  8 0001 */ { { 1, 14 }, { 1, 16 }, { 1, 14 }, { 1, 16 } },
    /*  9 1001 */ { { 45, 56 }, { 45, 58 }, { 59, 70 }, { 59, 72 } },
    /* 10 0101 */ { { 161, 166 }, { 162, 166 }, { 169, 174 }, { 170, 174 } },
    /* 11 1101 */ { { 111, 116 }, { 111, 118 }, { 119, 124 }, { 119, 126 } },
    /* 12 0011 */ { { 161, 168 }, { 162, 168 }, { 169, 176 }, { 170, 176 } },
    /* 13 1011 */ { { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    /* 14 0111 */ { { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    /* 15 1111 */ { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
};

/* The coefficients' quantisation, an 8-bit register's 7 bits to a 9-bit
 * magnitude: the SP0250's data sheet, taken for the SP0256 as Zbiciak
 * and MAME take it. */
static const int16_t qtbl[128] = {
      0,   9,  17,  25,  33,  41,  49,  57,  65,  73,  81,  89,  97, 105, 113, 121,
    129, 137, 145, 153, 161, 169, 177, 185, 193, 201, 209, 217, 225, 233, 241, 249,
    257, 265, 273, 281, 289, 297, 301, 305, 309, 313, 317, 321, 325, 329, 333, 337,
    341, 345, 349, 353, 357, 361, 365, 369, 373, 377, 381, 385, 389, 393, 397, 401,
    405, 409, 413, 417, 421, 425, 427, 429, 431, 433, 435, 437, 439, 441, 443, 445,
    447, 449, 451, 453, 455, 457, 459, 461, 463, 465, 467, 469, 471, 473, 475, 477,
    479, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
    496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511,
};

#define PER_PAUSE   64u   /* a PAUSE's period, and the noise's, in samples */
#define PER_NOISE   64

static inline int16_t coef(uint8_t x) {
    return (x & 0x80u) ? qtbl[(uint8_t)(-x) & 0x7Fu] : (int16_t)-qtbl[x & 0x7Fu];
}

static inline uint32_t bitrev32(uint32_t v) {
    v = (v >> 16) | (v << 16);
    v = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
    v = ((v & 0xF0F0F0F0u) >> 4) | ((v & 0x0F0F0F0Fu) << 4);
    v = ((v & 0xCCCCCCCCu) >> 2) | ((v & 0x33333333u) << 2);
    v = ((v & 0xAAAAAAAAu) >> 1) | ((v & 0x55555555u) << 1);
    return v;
}

/* A byte of the chip's space: the mask ROM at $1000-$17FF, nothing else. */
static inline uint8_t rom_byte(const uint8_t *rom, uint32_t a) {
    a &= 0xFFFFu;
    return (a - SP0256_ROM_BASE < SP0256_ROM_SIZE) ? rom[a - SP0256_ROM_BASE] : 0;
}

/* `len` bits (up to 8) at the PC, least significant first. */
static uint32_t getb(sp0256_t *sp, const uint8_t *rom, unsigned len) {
    uint32_t d0 = rom_byte(rom, sp->pc >> 3), d1 = rom_byte(rom, (sp->pc + 8u) >> 3);
    uint32_t v = ((d1 << 8) | d0) >> (sp->pc & 7u);
    sp->pc += len;
    return v & ((1u << len) - 1u);
}

/* The filter's parameters from the registers; the next sample a pulse. */
static void regdec(sp0256_t *sp) {
    sp->amp = (int16_t)((sp->r[R_AM] & 0x1Fu) << ((sp->r[R_AM] & 0xE0u) >> 5));
    sp->cnt = 0;
    sp->per = sp->r[R_PR];
    for (unsigned i = 0; i < 6u; i++) {
        sp->b_coef[i] = coef(sp->r[R_B0 + 2u * i]);
        sp->f_coef[i] = coef(sp->r[R_F0 + 2u * i]);
    }
    sp->interp = sp->r[R_IA] || sp->r[R_IP];
}

void sp0256_reset(sp0256_t *sp) {
    memset(sp, 0, sizeof *sp);
    sp->halted = true;
    sp->rpt = -1;
    sp->rng = 1;
    sp->lrq = true;
    sp->page = (uint32_t)SP0256_ROM_BASE << 3;
    sp->silent = true;
    sp->sby = true;
}

void sp0256_ald(sp0256_t *sp, uint8_t allophone) {
    if (!sp->lrq) return;   /* the register is full: dropped */
    sp->lrq = false;
    sp->ald = (uint16_t)((allophone & 0x3Fu) << 4);
    sp->sby = false;
}

/* The microsequencer, while no block of samples is in hand: a new
 * allophone from ALD if halted, then instructions until one gives the
 * filter a repeat count, or a return to 0 halts. */
static void micro(sp0256_t *sp, const uint8_t *rom) {
    while (sp->rpt <= 0) {
        if (sp->halted && !sp->lrq) {
            sp->pc = sp->ald | ((uint32_t)SP0256_ROM_BASE << 3);
            sp->halted = false;
            sp->lrq = true;   /* ready for the next */
            sp->ald = 0;
            memset(sp->r, 0, sizeof sp->r);
            sp->allophones++;
        }
        if (sp->halted) {
            sp->rpt = 1;
            sp->lrq = true;
            sp->ald = 0;
            memset(sp->r, 0, sizeof sp->r);
            sp->sby = true;
            return;
        }

        unsigned imm = getb(sp, rom, 4), op = getb(sp, rom, 4);
        unsigned repeat = 0;
        bool jumped = false;
        switch (op) {
        case 0x0:                                   /* RTS, or SETPAGE */
            if (imm) {
                sp->page = bitrev32(imm) >> 13;
            } else {
                uint32_t to = sp->stack;
                sp->stack = 0;
                if (!to) {
                    sp->halted = true;
                    sp->pc = 0;
                } else {
                    sp->pc = to;
                }
                jumped = true;
            }
            break;
        case 0xD:                                   /* JSR */
        case 0xE: {                                 /* JMP */
            uint32_t to = sp->page | (bitrev32(imm) >> 17) | (bitrev32(getb(sp, rom, 8)) >> 21);
            if (op == 0xD) sp->stack = (sp->pc + 7u) & ~7u;   /* the next byte */
            sp->pc = to;
            jumped = true;
            break;
        }
        case 0x1:                                   /* SETMODE */
            sp->mode = (uint8_t)(((imm & 8u) >> 2) | (imm & 4u) | ((imm & 3u) << 4));
            break;
        default:                                    /* the loads and PAUSE */
            repeat = imm | (sp->mode & 0x30u);
            break;
        }
        if (op != 0x1) sp->mode &= 0x0Fu;
        if (jumped || !repeat) continue;

        sp->rpt = (int16_t)(repeat + 1u);
        const int16_t *ix = field_index[op][(sp->mode & 6u) >> 1];
        for (int i = ix[0]; i >= 0 && i <= ix[1]; i++) {
            const field_t *f = &fields[i];
            if (f->flags & F_CLRA) {
                memset(sp->r, 0, sizeof sp->r);
                sp->silent = true;
            }
            if (f->flags & F_CLR5) sp->r[R_B5] = sp->r[R_F5] = 0;
            if (!f->len) continue;
            int32_t v = (int32_t)getb(sp, rom, f->len);
            if ((f->flags & F_DELTA) && (v & (1 << (f->len - 1u)))) v -= 1 << f->len;
            uint8_t b = (uint8_t)(v << f->shift);
            sp->silent = false;
            if (f->flags & F_FIELD) {
                sp->r[f->reg] = (uint8_t)((sp->r[f->reg] & ((1u << f->shift) - 1u)) | b);
            } else if (f->flags & F_DELTA) {
                sp->r[f->reg] = (uint8_t)(sp->r[f->reg] + b);
            } else {
                sp->r[f->reg] = b;
            }
        }
        if (op == 0xF) {                            /* PAUSE */
            sp->silent = true;
            sp->r[R_PR] = PER_PAUSE;
        }
        regdec(sp);
        return;
    }
}

/* One step of the excitation and the filter. False when the block ran
 * out on it: that step gives no sample, and the microsequencer runs. */
static bool step(sp0256_t *sp) {
    bool interp = false;
    int32_t s;
    if (sp->per) {
        if (sp->cnt <= 0) {
            sp->cnt = (int16_t)(sp->cnt + sp->per);
            s = sp->amp;
            sp->rpt--;
            interp = sp->interp;
            memset(sp->z, 0, sizeof sp->z);
        } else {
            s = 0;
            sp->cnt--;
        }
    } else {
        if (--sp->cnt <= 0) {
            interp = sp->interp;
            sp->cnt = PER_NOISE;
            sp->rpt--;
            memset(sp->z, 0, sizeof sp->z);
        }
        bool bit = sp->rng & 1u;
        sp->rng = (uint16_t)((sp->rng >> 1) ^ (bit ? 0x4001u : 0));
        s = bit ? sp->amp : -sp->amp;
    }
    if (interp) {
        sp->r[R_AM] = (uint8_t)(sp->r[R_AM] + sp->r[R_IA]);
        sp->r[R_PR] = (uint8_t)(sp->r[R_PR] + sp->r[R_IP]);
        sp->amp = (int16_t)((sp->r[R_AM] & 0x1Fu) << ((sp->r[R_AM] & 0xE0u) >> 5));
        sp->per = sp->r[R_PR];
    }
    if (sp->rpt <= 0) return false;

    /* Six second-order sections, in 16-bit arithmetic as the chip's. */
    for (unsigned j = 0; j < 6u; j++) {
        s = (int16_t)(s + ((sp->b_coef[j] * sp->z[j][1]) >> 9));
        s = (int16_t)(s + ((sp->f_coef[j] * sp->z[j][0]) >> 8));
        sp->z[j][1] = sp->z[j][0];
        sp->z[j][0] = (int16_t)s;
    }
    if (s > SP0256_LEVEL_MAX) s = SP0256_LEVEL_MAX;
    if (s < -SP0256_LEVEL_MAX - 1) s = -SP0256_LEVEL_MAX - 1;
    sp->out = (int16_t)s;
    return true;
}

bool sp0256_settled(const sp0256_t *sp) {
    if (!sp->halted || !sp->lrq || sp->interp || !sp->per || sp->out || sp->rpt <= 0) return false;
    for (unsigned j = 0; j < 6u; j++)
        if (sp->z[j][0] || sp->z[j][1]) return false;
    return true;
}

/* Halted, each sample is: the microsequencer leaves the block one period
 * long (rpt 1, the registers clear, nothing else moved); a step at the
 * pitch counter's end is a pulse that ends the block, gives no sample and
 * clears a filter already clear; the next step counts down and gives 0.
 * So each sample takes one from the counter, or past its end, adds the
 * period less one. */
void sp0256_skip_settled(sp0256_t *sp, uint32_t n) {
    int32_t cnt = sp->cnt, per = sp->per;
    sp->samples += n;
    while (n) {
        if (cnt <= 0) {
            cnt += per - 1;
            n--;
            continue;
        }
        uint32_t run = (uint32_t)cnt < n ? (uint32_t)cnt : n;
        cnt -= (int32_t)run;
        n -= run;
    }
    sp->cnt = (int16_t)cnt;
}

int16_t COCO_HOT1(sp0256_sample)(sp0256_t *sp, const uint8_t rom[SP0256_ROM_SIZE]) {
    for (;;) {
        if (sp->rpt <= 0) micro(sp, rom);
        if (step(sp)) break;
    }
    sp->samples++;
    return sp->out;
}
