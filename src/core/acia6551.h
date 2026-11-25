/* acia6551.h — the 6551 ACIA of the Deluxe RS-232 Program Pak
 * (design.md §12.11).
 *
 * Written from Synertek's SY6551 data sheet (the 1981-1982 Data Catalog,
 * pp. 3-169 to 3-176): the four registers, their reset values and the
 * baud rate generator's fifteen rates from a 1.8432 MHz crystal. Where
 * the sheet says nothing, MAME's mos6551.cpp is followed, as a tool
 * (§12.9, §18): the transmitter's interrupt repeats each character time
 * while it idles, divisor 0 takes the crystal itself as the 16x clock,
 * a receiver clocked from RxC hears nothing (the pak leaves RxC open),
 * and an overrun's byte replaces the one unread. Not MAME, but the
 * sheet: an IRQ raised stays until the status register is read, its
 * source disabled or not (acia6551.c).
 *
 *   $FF68  read: receive data, clearing RDRF and the three errors
 *          write: transmit data, clearing TDRE
 *   $FF69  read: status, clearing IRQ; write: the programmed reset
 *   $FF6A  command, read and written
 *   $FF6B  control, read and written
 *
 * Modelled a character at a time, not a bit, as the tape's signal is an
 * edge at a time (§12.3): a character takes its frame, start, data,
 * parity and stop bits at the programmed rate, in oscillator ticks
 * (§13.2). Characters leave through `tx` and arrive through `rx`, rings
 * the port empties and fills; the line itself is the port's, which sets
 * its UART to acia_baud and acia_format.
 *
 * The modem lines are tied as a three-wire cable leaves them, with no
 * modem behind the pak: CTS, DCD and DSR low, ready, so the transmitter
 * runs, the receiver hears and the status register's DCD and DSR bits
 * read 0. DTR and RTS go nowhere. Break and parity are not on the
 * port's line: a break is a character time of nothing, and a received
 * character never has a parity or framing error.
 *
 * Every timed step is an event at an exact tick, `due`, at which the run
 * loop stops (cart.h); accesses bring the chip to their own tick first.
 */
#ifndef PICO_COCO_ACIA6551_H
#define PICO_COCO_ACIA6551_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define ACIA_XTAL_HZ   1843200u   /* the data sheet's crystal              */
#define ACIA_FIRST     0xFF68u    /* the pak's registers, outside SCS     */
#define ACIA_LAST      0xFF6Bu

/* Status register bits (the data sheet's Figure 8). */
#define ACIA_SR_PARITY  0x01u
#define ACIA_SR_FRAMING 0x02u
#define ACIA_SR_OVERRUN 0x04u
#define ACIA_SR_RDRF    0x08u
#define ACIA_SR_TDRE    0x10u
#define ACIA_SR_DCD     0x20u
#define ACIA_SR_DSR     0x40u
#define ACIA_SR_IRQ     0x80u

/* Command register bits (Figure 7). */
#define ACIA_CMD_DTR    0x01u     /* 1: receiver and interrupts enabled   */
#define ACIA_CMD_RXIRQ  0x02u     /* 1: the receiver's interrupt disabled */
#define ACIA_CMD_TX     0x0Cu     /* the transmitter controls             */
#define ACIA_CMD_TX_IRQ 0x04u     /*   01: interrupt on, RTS low          */
#define ACIA_CMD_TX_BRK 0x0Cu     /*   11: break                          */
#define ACIA_CMD_ECHO   0x10u
#define ACIA_CMD_PARITY 0xE0u

/* Control register bits (Figure 6). */
#define ACIA_CTL_BAUD   0x0Fu
#define ACIA_CTL_RXCLK  0x10u     /* 1: the receiver on the generator     */
#define ACIA_CTL_WORD   0x60u     /* 00: 8 bits ... 11: 5 bits            */
#define ACIA_CTL_STOP   0x80u

typedef struct {
    uint8_t buf[COCO_SERIAL_RING];
    uint16_t head, tail;          /* head written, tail read; equal: empty */
    uint32_t lost;                /* characters a full ring turned away    */
} acia_ring_t;

typedef struct {
    uint8_t  control, command, status;
    uint8_t  tdr, rdr;
    uint8_t  irq_src;             /* the interrupt's sources (acia6551.c);
                                     status's IRQ set while any is        */
    /* The transmitter: a character in its shift register until tx_end,
     * and while it idles, its character times kept from tx_next, for
     * the repeated interrupt. */
    bool     tx_busy;
    uint8_t  tx_shift;
    uint64_t tx_end;
    uint64_t tx_next;
    /* The receiver: a character arriving until rx_end. */
    bool     rx_busy;
    uint8_t  rx_shift;
    uint64_t rx_end;
    uint64_t due;                 /* the next event, UINT64_MAX for none  */
    uint32_t frame;               /* a character's ticks, from control    */
    acia_ring_t tx, rx;           /* to the line, from it                 */
} acia_t;

/* The hardware reset (RES): control and command 0, status TDRE, and the
 * rings emptied. */
void    acia_init(acia_t *a, uint64_t now);

uint8_t acia_read(acia_t *a, uint16_t addr, uint64_t now);
void    acia_write(acia_t *a, uint16_t addr, uint8_t v, uint64_t now);

/* The chip brought up to tick now: characters finished, sent and taken,
 * interrupts raised. */
void    acia_run(acia_t *a, uint64_t now);

/* /IRQ, the pak's CART line, low while the status register's IRQ is set. */
static inline bool acia_irq(const acia_t *a) { return (a->status & ACIA_SR_IRQ) != 0; }

/* A character from the line at tick now; false if the ring is full. */
bool    acia_rx_put(acia_t *a, uint8_t c, uint64_t now);
/* The next character sent, or -1. */
int     acia_tx_get(acia_t *a);

/* The line's rate in baud (115,200 for divisor 0) and its format: word
 * length 5 to 8, parity 0 none, 1 odd, 2 even, 3 mark, 4 space, and
 * stop bits in halves (2, 3 or 4). */
uint32_t acia_baud(const acia_t *a);
void     acia_format(const acia_t *a, unsigned *bits, unsigned *parity, unsigned *stop_halves);

/* A character's frame in ticks for the control and command given. */
uint32_t acia_frame_ticks(uint8_t control, uint8_t command);

#endif /* PICO_COCO_ACIA6551_H */
