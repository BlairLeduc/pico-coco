/* test_acia.c — the 6551 ACIA (acia6551.h, design.md §12.11), held to
 * Synertek's SY6551 data sheet: its registers, its resets, its baud rate
 * table and its frames, and the interrupts as MAME's mos6551.cpp raises
 * them where the sheet is silent. Every timing is checked by running the
 * chip to the tick before and the tick of the event.
 *
 * Planted one at a time (2026-11-25), each failed this test: the stop
 * bit left out of the frame; IRQ withdrawn when its source is disabled,
 * as MAME has it (test_os9's Tandy ACIAPAK hangs on it too); a read of
 * RDR leaving RDRF; and the transmitter's interrupt not repeated while
 * it idles. */

#include <stdint.h>
#include <string.h>

#include "acia6551.h"
#include "test_util.h"

#define DATA    0xFF68u
#define STATUS  0xFF69u
#define COMMAND 0xFF6Au
#define CONTROL 0xFF6Bu

/* 9600 baud, the generator on the receiver too, 8 bits, 1 stop. */
#define CTL_9600_8N1 0x1Eu
/* DTR on, the receiver's interrupt on, RTS low, the transmitter's off. */
#define CMD_RTS      0x09u
/* DTR on, both interrupts on. */
#define CMD_TX_IRQ   0x05u

static uint64_t ticks_for_bits(double bits, double baud) {
    return (uint64_t)(bits / baud * COCO_TICK_HZ + 0.5);
}

static int test_reset(void) {
    acia_t a;
    acia_init(&a, 0);
    /* Figures 6 to 8: control 0, command 0, status 0--10000 with DCD and
     * DSR low (acia6551.h: the pak's lines are tied ready). */
    CHECK(acia_read(&a, STATUS, 0) == 0x10, "status at reset %02X", a.status);
    CHECK(acia_read(&a, COMMAND, 0) == 0x00, "command at reset");
    CHECK(acia_read(&a, CONTROL, 0) == 0x00, "control at reset");
    CHECK(!acia_irq(&a), "IRQ at reset");
    /* Command and control read back as written; the mirrors every four. */
    acia_write(&a, CONTROL, 0xA5, 0);
    acia_write(&a, COMMAND, 0x5A, 0);
    CHECK(acia_read(&a, CONTROL, 0) == 0xA5 && acia_read(&a, COMMAND, 0) == 0x5A, "registers read back");
    CHECK(acia_read(&a, 0xFF6Fu, 0) == 0xA5, "control's mirror");
    return 0;
}

static int test_baud(void) {
    /* Figure 6's table, in baud: 109.92 and 134.58 are the generator's. */
    static const double table[16] = {
        115200, 50, 75, 109.92, 134.58, 150, 300, 600,
        1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200,
    };
    for (unsigned b = 0; b < 16; b++) {
        acia_t a;
        acia_init(&a, 0);
        acia_write(&a, CONTROL, (uint8_t)(0x10u | b), 0);
        acia_write(&a, COMMAND, CMD_RTS, 0);
        CHECK(acia_baud(&a) == (uint32_t)table[b], "rate %u: %u baud", b, acia_baud(&a));
        /* A character of 8N1 leaves after ten bits at the rate, to a
         * tick (rounded to the nearest). */
        uint64_t t0 = 1000;
        acia_write(&a, DATA, 0x55, t0);
        uint64_t want = t0 + ticks_for_bits(10, table[b]);
        uint64_t off = want / 20000u + 1u;   /* the table's two decimals */
        acia_run(&a, want - off - 1u);
        CHECK(acia_tx_get(&a) < 0, "rate %u: the character left early", b);
        acia_run(&a, want + off);
        CHECK(acia_tx_get(&a) == 0x55, "rate %u: the character did not leave by tick %llu", b,
              (unsigned long long)want);
    }
    return 0;
}

/* The frame for each format: start, data, parity and stop bits. */
static int test_frames(void) {
    static const struct { uint8_t ctl, cmd; double bits; } f[] = {
        { 0x1E, 0x09, 10.0 },    /* 8N1                                    */
        { 0x9E, 0x09, 11.0 },    /* 8N2                                    */
        { 0x3E, 0x29, 10.0 },    /* 7, odd parity, 1                       */
        { 0xBE, 0x69, 11.0 },    /* 7, even parity, 2                      */
        { 0x9E, 0x29, 11.0 },    /* 8, parity, "2": one (Figure 6)          */
        { 0xFE, 0x09, 7.5 },     /* 5N "2": one and a half (Figure 6)       */
        { 0xFE, 0x29, 9.0 },     /* 5, parity, 2                           */
        { 0x5E, 0xA9, 9.0 },     /* 6, mark parity, 1                      */
    };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        acia_t a;
        acia_init(&a, 0);
        acia_write(&a, CONTROL, f[i].ctl, 0);
        acia_write(&a, COMMAND, f[i].cmd, 0);
        acia_write(&a, DATA, 0x3C, 0);
        uint64_t want = ticks_for_bits(f[i].bits, 9600);
        acia_run(&a, want - 2u);
        CHECK(acia_tx_get(&a) < 0, "format %u: early", i);
        acia_run(&a, want + 1u);
        int c = acia_tx_get(&a);
        uint8_t mask = (uint8_t)(0xFFu >> ((f[i].ctl >> 5) & 3u));
        CHECK(c == (0x3C & mask), "format %u: %d, not %02X by %llu ticks", i, c, 0x3C & mask,
              (unsigned long long)want);
        unsigned bits, par, stop;
        acia_format(&a, &bits, &par, &stop);
        CHECK(bits == 8u - ((f[i].ctl >> 5) & 3u), "format %u: word length %u", i, bits);
    }
    return 0;
}

static int test_transmitter(void) {
    acia_t a;
    acia_init(&a, 0);
    uint64_t fr = ticks_for_bits(10, 9600);
    acia_write(&a, CONTROL, CTL_9600_8N1, 0);
    /* With the transmitter off (bits 3-2 00), a character waits. */
    acia_write(&a, COMMAND, 0x01, 0);
    acia_write(&a, DATA, 0x41, 0);
    CHECK(!(acia_read(&a, STATUS, 0) & ACIA_SR_TDRE), "TDRE with the transmitter off");
    acia_run(&a, 10 * fr);
    CHECK(acia_tx_get(&a) < 0, "a character sent with the transmitter off");
    /* RTS low starts it: TDRE as the character moves to the shift
     * register, at once. */
    uint64_t t = 10 * fr;
    acia_write(&a, COMMAND, CMD_RTS, t);
    CHECK(acia_read(&a, STATUS, t) & ACIA_SR_TDRE, "TDRE once the character is shifting");
    /* A second waits in the data register for the first, then follows
     * it with no gap. */
    acia_write(&a, DATA, 0x42, t + 10);
    CHECK(!(acia_read(&a, STATUS, t + 10) & ACIA_SR_TDRE), "TDRE with a character waiting");
    acia_run(&a, t + fr - 2);
    CHECK(acia_tx_get(&a) < 0, "the first left early");
    acia_run(&a, t + fr + 1);
    CHECK(acia_tx_get(&a) == 0x41, "the first did not leave");
    CHECK(acia_read(&a, STATUS, t + fr + 1) & ACIA_SR_TDRE, "TDRE once the second is shifting");
    acia_run(&a, t + 2 * fr - 2);
    CHECK(acia_tx_get(&a) < 0, "the second left early");
    acia_run(&a, t + 2 * fr + 1);
    CHECK(acia_tx_get(&a) == 0x42, "the second did not follow the first");
    /* DTR off disables the transmitter. */
    acia_write(&a, COMMAND, 0x08, t + 3 * fr);
    acia_write(&a, DATA, 0x43, t + 3 * fr);
    acia_run(&a, t + 6 * fr);
    CHECK(acia_tx_get(&a) < 0, "a character sent with DTR off");
    /* Break: RTS low, nothing sent, no interrupt. */
    acia_write(&a, COMMAND, 0x0D, t + 6 * fr);
    acia_run(&a, t + 9 * fr);
    CHECK(acia_tx_get(&a) < 0, "a character sent in a break");
    CHECK(!acia_irq(&a), "an interrupt in a break");
    return 0;
}

static int test_tx_irq(void) {
    acia_t a;
    acia_init(&a, 0);
    uint64_t fr = ticks_for_bits(10, 9600);
    acia_write(&a, CONTROL, CTL_9600_8N1, 0);
    /* Transmit interrupts with RTS on and none: never. */
    acia_write(&a, COMMAND, CMD_RTS, 0);
    acia_run(&a, 5 * fr);
    CHECK(!acia_irq(&a), "an interrupt with the transmitter's disabled");
    /* Enabled while the transmitter idles: at once, and the status read
     * clears it, which then repeats each character time (MAME). */
    uint64_t t = 5 * fr;
    acia_write(&a, COMMAND, CMD_TX_IRQ, t);
    CHECK(acia_irq(&a), "no interrupt when enabled with TDRE set");
    uint8_t s = acia_read(&a, STATUS, t);
    CHECK((s & (ACIA_SR_IRQ | ACIA_SR_TDRE)) == (ACIA_SR_IRQ | ACIA_SR_TDRE), "status %02X", s);
    CHECK(!acia_irq(&a), "the status read did not clear IRQ");
    CHECK(!(acia_read(&a, STATUS, t) & ACIA_SR_IRQ), "IRQ read twice");
    acia_run(&a, t + fr - 2);
    CHECK(!acia_irq(&a), "the interrupt repeated early");
    acia_run(&a, t + fr + 1);
    CHECK(acia_irq(&a), "the interrupt did not repeat a character time on");
    /* Disabling the transmitter's interrupt leaves IRQ until the status
     * is read (Figure 8), and raises no more. */
    acia_write(&a, COMMAND, CMD_RTS, t + fr + 2);
    CHECK(acia_irq(&a), "IRQ withdrawn when its source was disabled");
    (void)acia_read(&a, STATUS, t + fr + 3);
    acia_run(&a, t + 3 * fr);
    CHECK(!acia_irq(&a), "IRQ raised by a disabled source");
    /* A character written with it enabled: IRQ as it starts to shift. */
    acia_write(&a, COMMAND, CMD_TX_IRQ, t + 4 * fr);
    (void)acia_read(&a, STATUS, t + 4 * fr);
    acia_write(&a, DATA, 0x31, t + 4 * fr + 5);
    CHECK(acia_irq(&a), "no interrupt as the character moved to the shift register");
    return 0;
}

static int test_receiver(void) {
    acia_t a;
    acia_init(&a, 0);
    uint64_t fr = ticks_for_bits(10, 9600);
    /* Nothing is heard before DTR and the generator's clock. */
    CHECK(acia_rx_put(&a, 0x11, 0), "a put refused");
    acia_write(&a, CONTROL, 0x0E, 0);   /* the receiver on RxC: open */
    acia_write(&a, COMMAND, CMD_RTS, 0);
    acia_rx_put(&a, 0x12, 0);
    acia_run(&a, 4 * fr);
    CHECK(!(acia_read(&a, STATUS, 4 * fr) & ACIA_SR_RDRF), "a character heard on an open RxC");
    uint64_t t = 4 * fr;
    acia_write(&a, CONTROL, CTL_9600_8N1, t);
    acia_rx_put(&a, 0xA7, t);
    acia_run(&a, t + fr - 2);
    CHECK(!(a.status & ACIA_SR_RDRF), "RDRF early");
    CHECK(!acia_irq(&a), "IRQ early");
    acia_run(&a, t + fr + 1);
    CHECK(a.status & ACIA_SR_RDRF, "RDRF not set a frame on");
    CHECK(acia_irq(&a), "the receiver's interrupt not raised");
    /* The status read clears IRQ, not RDRF; the data read clears RDRF. */
    uint8_t s = acia_read(&a, STATUS, t + fr + 2);
    CHECK((s & (ACIA_SR_IRQ | ACIA_SR_RDRF)) == (ACIA_SR_IRQ | ACIA_SR_RDRF), "status %02X", s);
    CHECK((acia_read(&a, STATUS, t + fr + 2) & (ACIA_SR_IRQ | ACIA_SR_RDRF)) == ACIA_SR_RDRF,
          "status after the read");
    CHECK(acia_read(&a, DATA, t + fr + 3) == 0xA7, "RDR");
    CHECK(!(acia_read(&a, STATUS, t + fr + 3) & ACIA_SR_RDRF), "RDRF after the data read");
    /* Characters arrive no faster than their frames. */
    t += 2 * fr;
    acia_rx_put(&a, 0x01, t);
    acia_rx_put(&a, 0x02, t);
    acia_run(&a, t + fr + 1);
    CHECK(acia_read(&a, DATA, t + fr + 1) == 0x01, "the first of two");
    acia_run(&a, t + 2 * fr - 2);
    CHECK(!(a.status & ACIA_SR_RDRF), "the second came early");
    acia_run(&a, t + 2 * fr + 1);
    CHECK(acia_read(&a, DATA, t + 2 * fr + 1) == 0x02, "the second of two");
    /* Overrun: the unread character overwritten (MAME), the bit cleared
     * by the data read. */
    t += 3 * fr;
    acia_rx_put(&a, 0x0A, t);
    acia_rx_put(&a, 0x0B, t);
    acia_run(&a, t + 2 * fr + 1);
    s = acia_read(&a, STATUS, t + 2 * fr + 1);
    CHECK(s & ACIA_SR_OVERRUN, "no overrun, status %02X", s);
    CHECK(acia_read(&a, DATA, t + 2 * fr + 1) == 0x0B, "overrun's RDR");
    CHECK(!(acia_read(&a, STATUS, t + 2 * fr + 1) & ACIA_SR_OVERRUN), "overrun kept after the data read");
    /* With its interrupt disabled (bit 1), RDRF and no IRQ. */
    t += 3 * fr;
    acia_write(&a, COMMAND, 0x0B, t);
    acia_rx_put(&a, 0x55, t);
    acia_run(&a, t + fr + 1);
    CHECK((a.status & ACIA_SR_RDRF) && !acia_irq(&a), "the disabled interrupt raised");
    /* Seven bits: the high bit is 0 in RDR. */
    t += 2 * fr;
    (void)acia_read(&a, DATA, t);
    acia_write(&a, CONTROL, 0x3E, t);
    acia_rx_put(&a, 0xFF, t);
    acia_run(&a, t + fr + 1);
    CHECK(acia_read(&a, DATA, t + fr + 1) == 0x7F, "seven bits' RDR");
    /* DTR off: the line's characters are lost. */
    t += 2 * fr;
    acia_write(&a, COMMAND, 0x00, t);
    acia_rx_put(&a, 0x66, t);
    acia_write(&a, COMMAND, CMD_RTS, t + 1);
    acia_run(&a, t + 3 * fr);
    CHECK(!(a.status & ACIA_SR_RDRF), "a character heard with DTR off");
    return 0;
}

static int test_program_reset(void) {
    acia_t a;
    acia_init(&a, 0);
    uint64_t fr = ticks_for_bits(10, 9600);
    acia_write(&a, CONTROL, CTL_9600_8N1, 0);
    acia_write(&a, COMMAND, 0xF5, 0);   /* space parity, echo, TX IRQ, DTR */
    acia_rx_put(&a, 1, 0);
    acia_rx_put(&a, 2, 0);
    acia_run(&a, 3 * fr);   /* parity: eleven bits a character */
    CHECK((a.status & ACIA_SR_OVERRUN) && acia_irq(&a), "set-up: %02X", a.status);
    /* Figures 7 and 8: command bits 4-0 and overrun cleared, control, the
     * parity bits and IRQ kept; with DTR off, no interrupt after. */
    acia_write(&a, STATUS, 0x00, 3 * fr + 2);
    CHECK(acia_read(&a, COMMAND, 3 * fr + 2) == 0xE0, "command after a programmed reset");
    CHECK(acia_read(&a, CONTROL, 3 * fr + 2) == CTL_9600_8N1, "control after a programmed reset");
    CHECK(acia_irq(&a), "IRQ cleared by a programmed reset");
    uint8_t s = acia_read(&a, STATUS, 3 * fr + 2);
    CHECK(!(s & ACIA_SR_OVERRUN) && (s & ACIA_SR_RDRF), "status after a programmed reset %02X", s);
    acia_rx_put(&a, 3, 3 * fr + 3);
    acia_run(&a, 6 * fr);
    CHECK(!acia_irq(&a), "an interrupt with DTR off");
    return 0;
}

static int test_echo(void) {
    acia_t a;
    acia_init(&a, 0);
    uint64_t fr = ticks_for_bits(10, 9600);
    acia_write(&a, CONTROL, CTL_9600_8N1, 0);
    acia_write(&a, COMMAND, 0x13, 0);   /* echo, receiver's IRQ off, DTR */
    acia_rx_put(&a, 0x5E, 0);
    acia_run(&a, fr + 1);
    CHECK(acia_tx_get(&a) == 0x5E, "the character not echoed");
    CHECK(acia_read(&a, DATA, fr + 1) == 0x5E, "the echoed character not received");
    return 0;
}

int main(void) {
    if (test_reset()) return 1;
    if (test_baud()) return 1;
    if (test_frames()) return 1;
    if (test_transmitter()) return 1;
    if (test_tx_irq()) return 1;
    if (test_receiver()) return 1;
    if (test_program_reset()) return 1;
    if (test_echo()) return 1;
    TEST_DONE();
}
