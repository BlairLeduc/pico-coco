/* tms7000.h — the TMS7040 of the Speech/Sound Cartridge (design.md §12.9).
 *
 * A Texas Instruments TMS7000-family microcomputer, NMOS, with 4 KB of
 * mask ROM at $F000-$FFFF and 256 bytes of register file. Written from
 * TI's TMS7000 Family Data Manual (SPND001C, 1989): Table 6-3 for every
 * opcode's length, cycles and status bits, §3.6 for the interrupts and
 * §3.7 for Timer 1. Held to MAME 0.289's tms7000 by a trace of the
 * cartridge's firmware (test_tms7000; MAME is a tool, never source, D2).
 *
 * Its memory, as the 7040 has it in single-chip mode:
 *   $0000-$00FF  the register file, R0 = A, R1 = B; the stack lives here
 *   $0100-$01FF  the peripheral file, P0 to P11 decoded:
 *     P0   IOCNT0: bits 0, 2, 4 enable INT1, INT2, INT3; bits 1, 3, 5
 *          their flags, cleared by writing 1; bits 6-7 the memory mode
 *     P2   Timer 1: written, the reload; read, the decrementer
 *     P3   Timer 1: written, bit 7 start (and reload), bit 6 the source
 *          (only the internal clock is modelled), bits 4-0 the prescale
 *          reload; read, the capture latch, taken as INT3 rises
 *     P4   port A, input only;  P6 port B, output only
 *     P8   port C, P9 its direction;  P10 port D, P11 its direction
 *   $F000-$FFFF  the ROM; anything else reads 0 (MAME's unmapped value)
 *
 * A cycle is a machine cycle, the input clock over 2. The cartridge's is
 * twice the port's E (§12.9), so a cycle is 16 master ticks, the 6809's
 * at its slow rate. Timer 1 counts the input clock over 16, a timer clock
 * every 8 cycles, and interrupts every 8 x (prescale + 1) x (reload + 1)
 * cycles (the manual's §3.7.8).
 *
 * INT1 and INT3 are lines the cartridge drives: while one is high its
 * flag is set, and clearing the flag with the line still high sets it
 * again (as MAME has them). Interrupts are taken between instructions,
 * INT1 first, then INT2, then INT3, with the I bit set and the level
 * enabled: the status and the PC pushed, the status cleared, 19 cycles.
 *
 * The ports are the board's: the core calls tms7000_port_in and
 * tms7000_port_out, which the board defines (ssc.c), and the board finds
 * itself from the tms7000_t it is given, a member of its own state. No
 * function pointers and no pointers into a machine, so a coco_t is copied
 * and saved whole (cart.h); the ROM is passed to tms7000_run each time.
 */
#ifndef PICO_COCO_TMS7000_H
#define PICO_COCO_TMS7000_H

#include <stdbool.h>
#include <stdint.h>

#define TMS7000_ROM_SIZE   0x1000u
#define TMS7000_ROM_BASE   0xF000u

/* The status register. */
#define TMS7000_ST_C   0x80u
#define TMS7000_ST_N   0x40u
#define TMS7000_ST_Z   0x20u
#define TMS7000_ST_I   0x10u

/* The ports, as tms7000_port_in and _out name them. */
#define TMS7000_PORT_A 0u
#define TMS7000_PORT_B 1u
#define TMS7000_PORT_C 2u
#define TMS7000_PORT_D 3u

/* Interrupt levels, as IOCNT0 and the vectors number them from 0. */
#define TMS7000_INT1   0u
#define TMS7000_INT2   1u
#define TMS7000_INT3   2u

typedef struct tms7000_s {
    uint16_t pc;
    uint8_t  sp, st;
    uint8_t  rf[256];         /* the register file                          */

    uint8_t  iocnt0;          /* P0                                         */
    uint8_t  t1_reload;       /* P2 as written                              */
    uint8_t  t1_ctl;          /* P3 as written                              */
    uint8_t  t1_dec;          /* the decrementer, as P2 reads it            */
    uint8_t  t1_pre;          /* the prescaler                              */
    uint8_t  t1_capture;      /* as P3 reads it                             */
    uint8_t  t1_phase;        /* cycles into the current timer clock, 0-7   */
    uint8_t  latch[4];        /* ports A-D as written                       */
    uint8_t  ddr[4];          /* their directions; A's is always 0, B's $FF */
    bool     line[3];         /* INT1, (unused), INT3 as driven             */
    bool     idle;            /* in IDLE, waiting for an interrupt          */

    uint64_t cycles;          /* since power-on                             */
    uint64_t instructions;    /* executed, not interrupt entries            */
    uint32_t illegal;         /* undefined opcodes met, each a 5-cycle NOP  */
} tms7000_t;

/* The board's ports (ssc.c). A read of an input port; a write gives the
 * latch masked by its direction, as the pins drive it. */
uint8_t tms7000_port_in(tms7000_t *c, unsigned port);
void    tms7000_port_out(tms7000_t *c, unsigned port, uint8_t v);

/* Power on: the register file and the peripheral file cleared. RESET
 * follows (tms7000_reset). */
void tms7000_init(tms7000_t *c);

/* RESET: the ports to their reset state through the board, the status
 * cleared, IOCNT0 cleared, SP at $FF, then the vector at $FFFE taken as
 * a TRAP 0 does, which leaves SP at $01; 17 cycles. */
void tms7000_reset(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]);

/* An interrupt line driven high or low (TMS7000_INT1 or _INT3). */
void tms7000_set_line(tms7000_t *c, unsigned level, bool high);

/* Run whole instructions until at least cycle `until`, taking interrupts
 * between them; returns the cycles run. In IDLE with nothing pending, the
 * time passes in one step, the timer run through it. */
uint32_t tms7000_run(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t until);

/* One instruction, or one interrupt entry if one is due; its cycles. */
uint32_t tms7000_step(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]);

/* For the oracle (test_tms7000): one instruction without looking at the
 * interrupts, and an interrupt entry for `level` made now, its flag
 * cleared as a taken one's is. */
uint32_t tms7000_step_no_irq(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE]);
uint32_t tms7000_enter(tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], unsigned level);

/* The byte at a 16-bit address as the CPU reads it, without side effects
 * on the peripheral file (for tests and the disassembler). */
uint8_t tms7000_peek(const tms7000_t *c, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a);

#endif /* PICO_COCO_TMS7000_H */
