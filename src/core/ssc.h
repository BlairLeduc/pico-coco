/* ssc.h — the Speech/Sound Cartridge, Tandy 26-3144 (design.md §12.9).
 *
 * A small computer of its own on the cartridge port, decoding $FF7D and
 * $FF7E outside SCS:
 *
 *   $FF7D write  bit 0 high holds the SP0256 in reset; its fall resets the
 *                TMS7040 and the AY (the manual: POKE 1 then 0)
 *   $FF7D read   $FF
 *   $FF7E write  a byte for the TMS7040: its port A, its INT3, and BUSY
 *   $FF7E read   the status, active low: bit 7 BUSY (the last byte not
 *                yet taken), bit 6 speaking (the SP0256's SBY low), bit 5
 *                a sound in progress; bits 4-0 high
 *
 * Inside (MAME 0.289's coco_ssc.cpp, §18): a TMS7040 (tms7000.h) running
 * its 4 KB mask ROM, 2 KB of static RAM, an AY-3-8913 and an SP0256-AL2.
 * The 7040's ports:
 *   A  the byte the 6809 wrote; reading it drops INT3
 *   B  the RAM's A0-A7
 *   C  bit 0 A8 and the AY's BC1, 1 A9, 2 A10, 3 the RAM's R/W* and the
 *      AY's BDIR, 4 the RAM's CS*, 5 the SP0256's ALD*, 6 the AY's CS*,
 *      7 the latch that clears BUSY, on its rise
 *   D  the data bus: the RAM's, the AY's, and the allophone on ALD*'s fall
 * The SP0256's LRQ (its FIFO has room) is the 7040's INT1.
 *
 * The 7040's cycle is 16 master ticks (tms7000.h), the AY's 8, and the
 * SP0256 makes a sample every 312 cycles of its 3.12 MHz crystal, about
 * 1,431.8 ticks, kept as a rational. ssc_run brings the cartridge up to
 * a tick, the 7040's instructions and the SP0256's samples in time
 * order, so LRQ reaches INT1 within a sample; it is called before the
 * 6809 touches the cartridge, before anything moves the sound bus, and
 * at the field's end.
 *
 * Its sound: the AY and the SP0256 mixed into one level, the
 * multiplexer's cartridge input (§10), to SSC_LEVEL_MAX: the AY's full
 * scale, all three channels at 15, SSC_AY_SHARE of it; the SP0256's
 * swing about SSC_SP_MID, SSC_SP_SHARE of it at the chip's full scale,
 * clipped to the level's range. The weights are an estimate (§18): MAME
 * sums its AY at gain 2.0 and its SP0256 at 1.75, unclipped, where the
 * manual's speech is about 0.7 of BASIC's loudest DAC sound; here it is
 * about half that, which clips no speech the AL2 makes in the tests.
 * The level goes to the machine at each change, stamped
 * (ssc_level_changed, coco.c).
 */
#ifndef PICO_COCO_SSC_H
#define PICO_COCO_SSC_H

#include <stdbool.h>
#include <stdint.h>

#include "ay8912.h"
#include "sp0256.h"
#include "tms7000.h"

#define SSC_RAM_SIZE     0x800u
#define SSC_TICKS_PER_CYCLE 16u
#define SSC_TICKS_PER_AY    8u

/* The SP0256's sample period in master ticks: COCO_TICK_HZ x 312 /
 * 3,120,000 = 1,431,818 / 1,000 at 14,318,180 Hz (config.h). */
#define SSC_SP_TICKS_NUM  1431818u
#define SSC_SP_TICKS_DEN  1000u

#define SSC_LEVEL_MAX    65535u
#define SSC_AY_SHARE     22937u   /* 0.35                                 */
#define SSC_SP_MID       32768u   /* 0.50                                 */
#define SSC_SP_SHARE     131070u  /* 2.0: speech peaks near a sixth of the
                                   * SP0256's scale; louder ones clip     */

#define SSC_RESET   0xFF7Du
#define SSC_DATA    0xFF7Eu

/* Port C's lines (above). */
#define SSC_C_A8    0x01u
#define SSC_C_BC1   0x01u
#define SSC_C_RW    0x08u
#define SSC_C_BDIR  0x08u
#define SSC_C_RCS   0x10u
#define SSC_C_ALD   0x20u
#define SSC_C_ACS   0x40u
#define SSC_C_BUSY  0x80u

/* Status bits at $FF7E, active low. */
#define SSC_ST_BUSY   0x80u
#define SSC_ST_SPEAK  0x40u
#define SSC_ST_SOUND  0x20u

typedef struct {
    tms7000_t cpu;
    uint8_t   ram[SSC_RAM_SIZE];
    uint8_t   porta;          /* the byte the 6809 last wrote at $FF7E   */
    uint8_t   portb;          /* the RAM's low address                   */
    uint8_t   portc;          /* the bus controller, as the pins drive it */
    uint8_t   portd;          /* the data bus                            */
    bool      busy;           /* the BUSY latch                          */
    uint8_t   reset_line;     /* $FF7D as last written                   */
    uint64_t  cycle0;         /* the tick at the 7040's cycle 0          */

    ay8912_t  ay;
    sp0256_t  sp;
    uint64_t  sp_next;        /* the tick of the SP0256's next sample      */
    uint32_t  sp_frac;        /* its fraction, in 1/SSC_SP_TICKS_DEN       */
    uint32_t  ay_level;       /* the AY's, to AY_LEVEL_MAX                 */
    uint32_t  level;          /* the mix, to SSC_LEVEL_MAX                 */
    uint32_t  allophones;     /* loaded, since power-on                    */
    uint32_t  bytes;          /* written at $FF7E since power-on; a count,
                               * not state (not in a save state)         */
    /* For ssc_run alone, not state: the AL2 while it runs; a count of
     * what the 7040 has done to the outside (a port changed, a byte
     * taken); and the 7040's state at a point it may come back to, to
     * see it repeat (ssc.c). */
    const uint8_t *al2_now;
    uint32_t  effects;
    uint16_t  idle_pc;
    uint32_t  idle_effects, idle_entries;
    uint64_t  idle_cycles, idle_insns, idle_period;
    uint8_t   idle_rf[256];
    uint8_t   idle_regs[12];
    uint32_t  skipped;        /* passes skipped, since power-on            */
    bool      exact;          /* tests: step every pass and every sample   */
    bool      attached;       /* in a machine's port, which hears it: set
                               * by the port after ssc_init (cart.c)      */
} ssc_t;

/* Power on, then RESET, the 7040 started from its ROM. `now` is the
 * machine's tick. */
void ssc_init(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now);

/* The 6809's access at $FF7D or $FF7E, at tick `now`: the cartridge is
 * brought up to `now` first. The ROMs are the 7040's and the AL2's. */
uint8_t ssc_read(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint16_t a, uint64_t now);
void    ssc_write(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint16_t a, uint8_t v,
                  uint64_t now);

/* Run the cartridge to tick `now`. */
void ssc_run(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint64_t now);

/* The machine's (coco.c): the cartridge's level is now s->level, from
 * tick `at`. */
void ssc_level_changed(ssc_t *s, uint64_t at);

#endif /* PICO_COCO_SSC_H */
