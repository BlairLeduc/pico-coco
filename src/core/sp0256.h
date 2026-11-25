/* sp0256.h — the General Instrument SP0256-AL2 of the Speech/Sound
 * Cartridge (design.md §12.9).
 *
 * The chip is modelled from its mask ROM (the owner, 2026-11-24): a
 * microsequencer reads each allophone's program from the AL2's 2 KB
 * (from /coco/roms/ by SHA-1, D10), and drives a 12-pole filter, six
 * second-order sections, excited by a pulse train or by noise, one
 * sample every 312 cycles of its own 3.12 MHz crystal, 10 kHz.
 *
 * The format of the microcode and the filter are as Joe Zbiciak's
 * reverse engineering for jzIntv has them, from GI's application manual
 * and patent, which MAME 0.289's sp0256.cpp carries (read 2026-11-24;
 * MAME is a tool, never source, D2): what each opcode loads, field by
 * field, is the table in sp0256.c, written here in its own form. Its
 * output is MAME's, the filter's sum limited to 14 bits; the chip's own
 * DAC resolution is not settled (§18).
 *
 * The chip's address space is in bits. The AL2's ROM lies at bytes
 * $1000-$17FF; an allophone n, loaded on ALD*, starts at byte $1000 +
 * 2n. LRQ, high while the chip can take another allophone, is the
 * TMS7040's INT1 (ssc.h); SBY, high once the last has finished, is the
 * cartridge's "speaking" status. No SPB640 FIFO is fitted.
 */
#ifndef PICO_COCO_SP0256_H
#define PICO_COCO_SP0256_H

#include <stdbool.h>
#include <stdint.h>

#define SP0256_ROM_SIZE   0x800u
#define SP0256_ROM_BASE   0x1000u     /* in bytes, of the chip's space     */
#define SP0256_CLOCK_HZ   3120000u
#define SP0256_DIVIDER    312u        /* clocks a sample: 10 kHz            */
#define SP0256_LEVEL_MAX  8191        /* the output's magnitude, at most    */

typedef struct {
    /* The microsequencer. */
    uint32_t pc;              /* in bits                                    */
    uint32_t stack;           /* one return address, 0 for none             */
    uint32_t page;            /* in bits                                    */
    uint8_t  mode;            /* SETMODE's: bits 1-2 the mode, 4-5 repeat MSBs */
    bool     halted;
    bool     lrq;             /* the allophone register is free             */
    bool     sby;             /* standing by: nothing being spoken          */
    uint16_t ald;             /* the address loaded, in bits from the base  */
    bool     silent;

    /* The filter. */
    int16_t  rpt;             /* periods left of the current block          */
    int16_t  cnt;             /* samples to the next pitch pulse            */
    uint16_t per;             /* the pitch period; 0 for noise              */
    uint16_t rng;             /* the noise's 15-bit LFSR                    */
    int16_t  amp;
    bool     interp;
    int16_t  b_coef[6], f_coef[6];
    int16_t  z[6][2];
    uint8_t  r[16];           /* AM, PR, B0, F0 ... B5, F5, IA, IP          */

    int16_t  out;             /* the last sample                            */
    uint32_t samples;         /* since power-on                             */
    uint32_t allophones;      /* taken                                      */
} sp0256_t;

/* Power on and RESET: halted, standing by, LRQ high. */
void sp0256_reset(sp0256_t *sp);

/* ALD*: an allophone, 0-63. Dropped while LRQ is low, as the chip does. */
void sp0256_ald(sp0256_t *sp, uint8_t allophone);

/* Whether the chip stands by with its filter at rest, so that each sample
 * is 0 and moves only the pitch counter: halted, LRQ high, no
 * interpolation, a pitch period, the filter's memory and output 0. */
bool sp0256_settled(const sp0256_t *sp);

/* n samples of a settled chip at once, as n calls would leave it. */
void sp0256_skip_settled(sp0256_t *sp, uint32_t n);

/* One sample, from the mask ROM. Returns it, -8192 to 8191. */
int16_t sp0256_sample(sp0256_t *sp, const uint8_t rom[SP0256_ROM_SIZE]);

#endif /* PICO_COCO_SP0256_H */
