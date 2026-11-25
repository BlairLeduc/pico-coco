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
 * The 7040's cycle is 16 master ticks (tms7000.h). The cartridge is
 * brought up to a tick before the 6809 touches it and at the field's
 * end (ssc_run).
 */
#ifndef PICO_COCO_SSC_H
#define PICO_COCO_SSC_H

#include <stdbool.h>
#include <stdint.h>

#include "tms7000.h"

#define SSC_RAM_SIZE     0x800u
#define SSC_TICKS_PER_CYCLE 16u

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

    /* M18: the AY-3-8913 and the SP0256-AL2. Until they are built, the
     * AY's registers alone, and the allophones as they are loaded. */
    uint8_t   ay_addr;
    uint8_t   ay_reg[16];
    uint32_t  allophones;     /* loaded, since power-on                  */
    uint8_t   last_allophone;
} ssc_t;

/* Power on, then RESET, the 7040 started from its ROM. `now` is the
 * machine's tick. */
void ssc_init(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now);

/* The 6809's access at $FF7D or $FF7E, at tick `now`: the cartridge is
 * brought up to `now` first. */
uint8_t ssc_read(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a, uint64_t now);
void    ssc_write(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a, uint8_t v, uint64_t now);

/* Run the cartridge to tick `now`. */
void ssc_run(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now);

#endif /* PICO_COCO_SSC_H */
