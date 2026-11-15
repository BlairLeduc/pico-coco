/* mc6821.h — the MC6821 PIA, as the generic part (design.md §8.3, EL §4.2).
 *
 * The chip, not the machine: two ports, each with an output register, a
 * data direction register and a control register; the output latch kept
 * apart from the pins, so a read returns the pins on input bits and the
 * latch on output bits; C1 an edge input with its polarity and enable; C2
 * an edge input, or an output set by hand; the flags cleared by a read of
 * the data register; IRQA and IRQB out. An enabled edge sets its flag at
 * once and raises the IRQ output tRS3 later (the datasheet's figure 13,
 * 1 us), which the machine times (coco.c): the output is a state, cleared
 * by a read of the data register, and raised by mc6821_raise_c1 even if
 * that read came first, as XRoar models it (§18). What the pins are wired to is the
 * machine's (coco.c), which sets each port's inputs before a read and
 * reads its outputs after a write.
 *
 * Register select, RS1 RS0: 0 port A data or DDR (by CRA bit 2), 1 CRA,
 * 2 port B data or DDR, 3 CRB.
 */
#ifndef PICO_COCO_MC6821_H
#define PICO_COCO_MC6821_H

#include <stdbool.h>
#include <stdint.h>

/* Control register bits. */
#define MC6821_CR_C1_IRQ   0x01u   /* C1 interrupt enabled                  */
#define MC6821_CR_C1_RISE  0x02u   /* C1 active on low-to-high, else high-to-low */
#define MC6821_CR_DATA     0x04u   /* data register selected, else DDR      */
#define MC6821_CR_C2_B3    0x08u   /* C2 in: IRQ enable; out, manual: level */
#define MC6821_CR_C2_B4    0x10u   /* C2 in: rising edge; out: 1 manual     */
#define MC6821_CR_C2_OUT   0x20u   /* C2 an output                          */
#define MC6821_CR_IRQ2     0x40u   /* C2 flag (read only)                   */
#define MC6821_CR_IRQ1     0x80u   /* C1 flag (read only)                   */

typedef struct {
    uint8_t out;     /* the output register (the latch)                 */
    uint8_t ddr;     /* 1 = output                                      */
    uint8_t cr;
    uint8_t in;      /* the pins as the outside drives them; 1 = high    */
    bool    c1;      /* last level on C1                                */
    bool    c2;      /* last level on C2 as an input                    */
    bool    irq_out; /* IRQA or IRQB                                    */
} mc6821_port_t;

typedef struct {
    mc6821_port_t a, b;
} mc6821_t;

/* The RESET line: every register zero. */
void    mc6821_reset(mc6821_t *p);

/* Register access, `reg` the two register-select bits. A read of a data
 * register clears its side's flags. */
uint8_t mc6821_read(mc6821_t *p, unsigned reg);
void    mc6821_write(mc6821_t *p, unsigned reg, uint8_t v);

/* The control lines as inputs: an edge of the selected polarity sets the
 * flag. True when its interrupt is enabled, so the caller raises the
 * output tRS3 later with mc6821_raise. */
bool    mc6821_set_c1(mc6821_port_t *port, bool level);
bool    mc6821_set_c2(mc6821_port_t *port, bool level);

/* tRS3 after an edge for which set_c1 or set_c2 returned true. */
void    mc6821_raise_c1(mc6821_port_t *port);
void    mc6821_raise_c2(mc6821_port_t *port);

/* The port's output pins: the latch where the bit is an output, high
 * (pulled up) where it is an input. */
static inline uint8_t mc6821_out(const mc6821_port_t *port) {
    return (uint8_t)((port->out & port->ddr) | (uint8_t)~port->ddr);
}

/* C2 as the chip drives it: its manual level when an output set by hand;
 * otherwise high. */
static inline bool mc6821_c2_out(const mc6821_port_t *port) {
    if ((port->cr & (MC6821_CR_C2_OUT | MC6821_CR_C2_B4)) ==
        (MC6821_CR_C2_OUT | MC6821_CR_C2_B4))
        return (port->cr & MC6821_CR_C2_B3) != 0;
    return true;   /* M8: the pulse and handshake modes, if software uses them */
}

/* The port's interrupt output. */
static inline bool mc6821_irq(const mc6821_port_t *port) {
    return port->irq_out;
}

#endif /* PICO_COCO_MC6821_H */
