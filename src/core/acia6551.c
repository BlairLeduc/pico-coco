/* acia6551.c — the 6551 ACIA (acia6551.h, design.md §12.11). */

#include "acia6551.h"

#include <string.h>

/* The interrupt's two sources this model raises: DCD and DSR never
 * change (acia6551.h). Reading the status register clears both. */
#define SRC_RDRF 0x01u
#define SRC_TDRE 0x02u

/* The baud rate generator's divisors of the crystal for its 16x clock
 * (Figure 6: 50 baud is 1,843,200 / 16 / 2304); 0 is the crystal
 * itself, as MAME has it (acia6551.h). */
static const uint16_t divisor[16] = {
    1, 2304, 1536, 1048, 856, 768, 384, 192, 96, 64, 48, 32, 24, 16, 12, 6,
};

static unsigned word_bits(uint8_t control) { return 8u - ((control & ACIA_CTL_WORD) >> 5); }

void acia_format(const acia_t *a, unsigned *bits, unsigned *parity, unsigned *stop_halves) {
    unsigned w = word_bits(a->control);
    unsigned p = (a->command & 0x20u) ? 1u + ((a->command >> 6) & 3u) : 0u;
    unsigned s = 2u;
    if (a->control & ACIA_CTL_STOP) {
        /* Figure 6: two, but one and a half for five bits and no parity,
         * and one for eight bits and a parity bit. */
        if (w == 5u && !p) s = 3u;
        else if (!(w == 8u && p)) s = 4u;
    }
    *bits = w;
    *parity = p;
    *stop_halves = s;
}

uint32_t acia_frame_ticks(uint8_t control, uint8_t command) {
    acia_t t = { .control = control, .command = command };
    unsigned w, p, s;
    acia_format(&t, &w, &p, &s);
    uint64_t halves = 2u + 2u * w + (p ? 2u : 0u) + s;
    /* A bit is 16 of the generator's clocks, each `divisor` crystal
     * cycles; ticks rounded to the nearest. */
    uint64_t xtal = halves * 8u * divisor[control & ACIA_CTL_BAUD];
    return (uint32_t)((xtal * COCO_TICK_HZ + ACIA_XTAL_HZ / 2u) / ACIA_XTAL_HZ);
}

uint32_t acia_baud(const acia_t *a) {
    return ACIA_XTAL_HZ / 16u / divisor[a->control & ACIA_CTL_BAUD];
}

/* ---- the enables (Figure 7) ------------------------------------------- */

static inline bool dtr(const acia_t *a) { return (a->command & ACIA_CMD_DTR) != 0; }
static inline bool tx_on(const acia_t *a) { return dtr(a) && (a->command & ACIA_CMD_TX) != 0; }
static inline bool tx_irq_on(const acia_t *a) {
    return dtr(a) && (a->command & ACIA_CMD_TX) == ACIA_CMD_TX_IRQ;
}
static inline bool tx_break(const acia_t *a) { return (a->command & ACIA_CMD_TX) == ACIA_CMD_TX_BRK; }
static inline bool rx_on(const acia_t *a) { return dtr(a) && (a->control & ACIA_CTL_RXCLK) != 0; }
static inline bool rx_irq_on(const acia_t *a) { return dtr(a) && !(a->command & ACIA_CMD_RXIRQ); }

/* ---- the rings --------------------------------------------------------- */

#define RING_MASK (COCO_SERIAL_RING - 1u)
_Static_assert((COCO_SERIAL_RING & RING_MASK) == 0, "the serial ring is a power of two");

static bool ring_put(acia_ring_t *r, uint8_t c) {
    if ((uint16_t)(r->head - r->tail) >= COCO_SERIAL_RING) {
        r->lost++;
        return false;
    }
    r->buf[r->head & RING_MASK] = c;
    r->head++;
    return true;
}

static int ring_get(acia_ring_t *r) {
    if (r->head == r->tail) return -1;
    return r->buf[r->tail++ & RING_MASK];
}

/* ---- events ------------------------------------------------------------ */

static void set_irq(acia_t *a, uint8_t s) {
    a->irq_src |= s;
    if (a->irq_src) a->status |= ACIA_SR_IRQ;
}

static void schedule(acia_t *a) {
    uint64_t d = UINT64_MAX;
    if (a->tx_busy) d = a->tx_end;
    else if (tx_irq_on(a) && a->tx_next < d) d = a->tx_next;
    if (a->rx_busy && a->rx_end < d) d = a->rx_end;
    a->due = d;
}

/* A character time begins for the transmitter at t: the data register's
 * character into the shift register if one waits, and the interrupt if
 * it is enabled, as at every character time (MAME; acia6551.h). */
static void tx_frame(acia_t *a, uint64_t t) {
    if (!(a->status & ACIA_SR_TDRE) && !tx_break(a)) {
        a->tx_shift = a->tdr;
        a->status |= ACIA_SR_TDRE;
        a->tx_busy = true;
        a->tx_end = t + a->frame;
    }
    if (tx_irq_on(a)) set_irq(a, SRC_TDRE);
    a->tx_next = t + a->frame;
}

/* A character from the ring into the receiver at t, if it listens. */
static void rx_start(acia_t *a, uint64_t t) {
    if (a->rx_busy || !rx_on(a)) return;
    int c = ring_get(&a->rx);
    if (c < 0) return;
    a->rx_shift = (uint8_t)c;
    a->rx_busy = true;
    a->rx_end = t + a->frame;
}

static uint8_t word_mask(const acia_t *a) { return (uint8_t)(0xFFu >> (8u - word_bits(a->control))); }

static void rx_done(acia_t *a) {
    a->rx_busy = false;
    /* The unread character is overwritten (MAME; §18). */
    if (a->status & ACIA_SR_RDRF) a->status |= ACIA_SR_OVERRUN;
    a->rdr = a->rx_shift & word_mask(a);
    a->status |= ACIA_SR_RDRF;
    if (rx_irq_on(a)) set_irq(a, SRC_RDRF);
    /* Echo: the character goes back out as it came in, the transmitter
     * idle (bits 3-2 must be 0, Figure 7). */
    if (a->command & ACIA_CMD_ECHO) ring_put(&a->tx, a->rdr);
}

void acia_run(acia_t *a, uint64_t now) {
    while (a->due <= now) {
        uint64_t t = a->due;
        if (a->tx_busy && a->tx_end == t) {
            a->tx_busy = false;
            ring_put(&a->tx, a->tx_shift & word_mask(a));
            if (tx_on(a)) tx_frame(a, t);
        } else if (!a->tx_busy && tx_irq_on(a) && a->tx_next == t) {
            tx_frame(a, t);
        } else if (a->rx_busy && a->rx_end == t) {
            rx_done(a);
            rx_start(a, t);
        }
        schedule(a);
    }
}

/* ---- the bus ----------------------------------------------------------- */

void acia_init(acia_t *a, uint64_t now) {
    memset(a, 0, sizeof *a);
    /* Figures 6 to 8, the hardware reset: control and command 0, status
     * 0--10000 with DCD and DSR low. */
    a->status = ACIA_SR_TDRE;
    a->frame = acia_frame_ticks(0, 0);
    a->tx_next = now;
    a->due = UINT64_MAX;
}

/* After a write to command or control: what the new enables start. An
 * IRQ already raised stays until the status register is read (Figure
 * 8), though its source is disabled: MAME withdraws it, and Tandy's
 * ACIAPAK, which disables the transmitter's interrupt just after one is
 * raised, then takes a FIRQ that no 6551 claims, and whose flag no one
 * clears, for ever (§18). */
static void enables(acia_t *a, uint64_t now) {
    if (!a->tx_busy && tx_on(a) &&
        (!(a->status & ACIA_SR_TDRE) || (tx_irq_on(a) && a->tx_next <= now)))
        tx_frame(a, now);
    rx_start(a, now);
    schedule(a);
}

uint8_t acia_read(acia_t *a, uint16_t addr, uint64_t now) {
    acia_run(a, now);
    switch (addr & 3u) {
    case 0:
        a->status &= (uint8_t)~(ACIA_SR_PARITY | ACIA_SR_FRAMING | ACIA_SR_OVERRUN | ACIA_SR_RDRF);
        return a->rdr;
    case 1: {
        uint8_t s = a->status;
        a->irq_src = 0;
        a->status &= (uint8_t)~ACIA_SR_IRQ;
        return s;
    }
    case 2:
        return a->command;
    default:
        return a->control;
    }
}

void acia_write(acia_t *a, uint16_t addr, uint8_t v, uint64_t now) {
    acia_run(a, now);
    switch (addr & 3u) {
    case 0:
        a->tdr = v;
        a->status &= (uint8_t)~ACIA_SR_TDRE;
        /* An idle transmitter starts at once (MAME). */
        if (!a->tx_busy && tx_on(a)) tx_frame(a, now);
        schedule(a);
        break;
    case 1:
        /* The programmed reset (Figures 7 and 8): overrun cleared,
         * command bits 4-0 cleared, control and IRQ kept; the data is
         * ignored. */
        a->status &= (uint8_t)~ACIA_SR_OVERRUN;
        a->command &= ACIA_CMD_PARITY;
        enables(a, now);
        break;
    case 2:
        a->command = v;
        a->frame = acia_frame_ticks(a->control, a->command);
        enables(a, now);
        break;
    default:
        a->control = v;
        a->frame = acia_frame_ticks(a->control, a->command);
        enables(a, now);
        break;
    }
}

bool acia_rx_put(acia_t *a, uint8_t c, uint64_t now) {
    acia_run(a, now);
    /* A receiver that does not listen loses what the line carries. */
    if (!rx_on(a)) return true;
    if (!ring_put(&a->rx, c)) return false;
    rx_start(a, now);
    schedule(a);
    return true;
}

int acia_tx_get(acia_t *a) {
    return ring_get(&a->tx);
}
