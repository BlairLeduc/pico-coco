/* serial.c — the RS-232 pak's line on UART0 (serial.h, design.md §12.11). */

#include "serial.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "log.h"

#define SERIAL_UART    uart0
#define SERIAL_TX_PIN  0u        /* hardware-notes.md §1.1 */
#define SERIAL_RX_PIN  1u
#define SERIAL_RING    1024u     /* half a second at 19,200 baud */
#define RING_MASK      (SERIAL_RING - 1u)
_Static_assert((SERIAL_RING & RING_MASK) == 0, "the serial rings are a power of two");

/* Single producer, single consumer: rx written by core 1's interrupt and
 * read by core 0; tx written by core 0 and read by core 1. */
static uint8_t           s_rx[SERIAL_RING], s_tx[SERIAL_RING];
static volatile uint32_t s_rx_head, s_rx_tail, s_tx_head, s_tx_tail;
static volatile serial_stats_t s_st;

/* The format last given to the UART, core 0's. */
static uint32_t s_baud;
static unsigned s_bits, s_parity, s_stop;

static void __not_in_flash_func(serial_irq)(void) {
    uart_hw_t *hw = uart_get_hw(SERIAL_UART);
    /* Received, to the ring; a full ring loses the byte. */
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
        uint32_t dr = hw->dr;
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_BE_BITS | UART_UARTDR_OE_BITS))
            s_st.errors++;
        uint32_t head = s_rx_head;
        if (head - s_rx_tail >= SERIAL_RING) {
            s_st.rx_lost++;
            continue;
        }
        s_rx[head & RING_MASK] = (uint8_t)dr;
        __dmb();
        s_rx_head = head + 1u;
        s_st.rx++;
    }
    /* The transmit FIFO refilled; with nothing left, its interrupt off,
     * as it stays asserted while the FIFO is below its level. */
    if (hw->imsc & UART_UARTIMSC_TXIM_BITS) {
        uint32_t tail = s_tx_tail, head = s_tx_head;
        __dmb();
        while (tail != head && !(hw->fr & UART_UARTFR_TXFF_BITS)) {
            hw->dr = s_tx[tail++ & RING_MASK];
            s_st.tx++;
        }
        s_tx_tail = tail;
        if (tail == head) hw_clear_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
    }
}

void serial_init(void) {
    s_baud = uart_init(SERIAL_UART, 9600u);
    s_bits = 8u;
    s_parity = 0u;
    s_stop = 2u;
    s_st.baud = s_baud;
    gpio_set_function(SERIAL_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SERIAL_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(SERIAL_UART, false, false);
    uart_set_fifo_enabled(SERIAL_UART, true);
    irq_set_exclusive_handler(UART0_IRQ, serial_irq);
    irq_set_enabled(UART0_IRQ, true);
    /* The receiver at its FIFO's level, and on its timeout for the bytes
     * left below it. */
    hw_set_bits(&uart_get_hw(SERIAL_UART)->imsc, UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS);
}

void serial_pump(void) {
    uart_hw_t *hw = uart_get_hw(SERIAL_UART);
    if (s_tx_tail == s_tx_head || (hw->imsc & UART_UARTIMSC_TXIM_BITS)) return;
    /* The PL011's interrupt comes as the FIFO falls through its level,
     * so the first bytes go in by hand, its own interrupt held off. */
    uint32_t irq = save_and_disable_interrupts();
    uint32_t tail = s_tx_tail, head = s_tx_head;
    __dmb();
    while (tail != head && !(hw->fr & UART_UARTFR_TXFF_BITS)) {
        hw->dr = s_tx[tail++ & RING_MASK];
        s_st.tx++;
    }
    s_tx_tail = tail;
    if (tail != head) hw_set_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
    restore_interrupts(irq);
}

/* The UART to the 6551's format: 5 to 8 bits, parity none, odd, even,
 * mark or space (stick parity), one stop bit or two, for 1.5 too. Below
 * about 143 baud the divider is at its end, at 150 MHz (§12.11). */
static void set_format(uint32_t baud, unsigned bits, unsigned parity, unsigned stop_halves) {
    uart_parity_t p = parity == 1u ? UART_PARITY_ODD : parity == 2u ? UART_PARITY_EVEN
                    : parity == 3u ? UART_PARITY_ODD : parity == 4u ? UART_PARITY_EVEN
                                   : UART_PARITY_NONE;
    s_st.baud = uart_set_baudrate(SERIAL_UART, baud);
    uart_set_format(SERIAL_UART, bits, stop_halves > 2u ? 2u : 1u, p);
    /* Mark is a stick 1, EPS clear; space a stick 0, EPS set (the
     * PL011's SPS). */
    hw_write_masked(&uart_get_hw(SERIAL_UART)->lcr_h, parity >= 3u ? UART_UARTLCR_H_SPS_BITS : 0u,
                    UART_UARTLCR_H_SPS_BITS);
    log_printf("  rs-232       : %lu baud (%lu on the UART), %u bits, parity %s, %s stop\n",
               (unsigned long)baud, (unsigned long)s_st.baud, bits,
               (const char *[]){ "none", "odd", "even", "mark", "space" }[parity],
               stop_halves == 2u ? "1" : stop_halves == 3u ? "1.5" : "2");
}

void serial_field(coco_t *m) {
    const acia_t *a = coco_serial(m);
    if (!a) {
        /* No pak: the line's bytes go nowhere. */
        s_rx_tail = s_rx_head;
        return;
    }
    uint32_t baud = acia_baud(a);
    unsigned bits, parity, stop;
    acia_format(a, &bits, &parity, &stop);
    if (baud != s_baud || bits != s_bits || parity != s_parity || stop != s_stop) {
        s_baud = baud;
        s_bits = bits;
        s_parity = parity;
        s_stop = stop;
        set_format(baud, bits, parity, stop);
    }
    /* In: as many as the 6551's ring takes; the rest wait. */
    uint32_t tail = s_rx_tail, head = s_rx_head;
    __dmb();
    while (tail != head && coco_serial_put(m, s_rx[tail & RING_MASK])) tail++;
    __dmb();
    s_rx_tail = tail;
    /* Out: what the guest sent, while the ring has room. */
    head = s_tx_head;
    while (head - s_tx_tail < SERIAL_RING) {
        int c = coco_serial_get(m);
        if (c < 0) break;
        s_tx[head++ & RING_MASK] = (uint8_t)c;
    }
    __dmb();
    s_tx_head = head;
}

void serial_stats(serial_stats_t *out) {
    out->rx = s_st.rx;
    out->tx = s_st.tx;
    out->rx_lost = s_st.rx_lost;
    out->errors = s_st.errors;
    out->baud = s_st.baud;
}
