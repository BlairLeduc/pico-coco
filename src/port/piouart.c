/* piouart.c — the development log on a PIO UART (piouart.h). */

#include "piouart.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"

#include "piouart.pio.h"

#define PIOUART_PIO pio0

static unsigned s_tx_sm, s_rx_sm;

bool piouart_writable(void) {
    return !pio_sm_is_tx_fifo_full(PIOUART_PIO, s_tx_sm);
}

void piouart_putc(char c) {
    pio_sm_put_blocking(PIOUART_PIO, s_tx_sm, (uint8_t)c);
}

static void out_chars(const char *buf, int len) {
    for (int i = 0; i < len; i++) piouart_putc(buf[i]);
}

static int in_chars(char *buf, int len) {
    int n = 0;
    while (n < len && !pio_sm_is_rx_fifo_empty(PIOUART_PIO, s_rx_sm))
        buf[n++] = (char)(pio_sm_get(PIOUART_PIO, s_rx_sm) >> 24);
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t s_driver = {
    .out_chars = out_chars,
    .in_chars = in_chars,
    .crlf_enabled = true,
};

void piouart_init(unsigned tx_pin, unsigned rx_pin, unsigned baud) {
    /* Eight PIO cycles a bit. */
    float div = (float)clock_get_hz(clk_sys) / (8.0f * (float)baud);

    s_tx_sm = (unsigned)pio_claim_unused_sm(PIOUART_PIO, true);
    unsigned off = pio_add_program(PIOUART_PIO, &piouart_tx_program);
    pio_sm_set_pins_with_mask(PIOUART_PIO, s_tx_sm, 1u << tx_pin, 1u << tx_pin);
    pio_sm_set_pindirs_with_mask(PIOUART_PIO, s_tx_sm, 1u << tx_pin, 1u << tx_pin);
    pio_gpio_init(PIOUART_PIO, tx_pin);
    pio_sm_config c = piouart_tx_program_get_default_config(off);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, tx_pin, 1);
    sm_config_set_sideset_pins(&c, tx_pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(PIOUART_PIO, s_tx_sm, off, &c);
    pio_sm_set_enabled(PIOUART_PIO, s_tx_sm, true);

    s_rx_sm = (unsigned)pio_claim_unused_sm(PIOUART_PIO, true);
    off = pio_add_program(PIOUART_PIO, &piouart_rx_program);
    pio_sm_set_consecutive_pindirs(PIOUART_PIO, s_rx_sm, rx_pin, 1, false);
    pio_gpio_init(PIOUART_PIO, rx_pin);
    gpio_pull_up(rx_pin);
    c = piouart_rx_program_get_default_config(off);
    sm_config_set_in_pins(&c, rx_pin);
    sm_config_set_in_shift(&c, true, true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(PIOUART_PIO, s_rx_sm, off, &c);
    pio_sm_set_enabled(PIOUART_PIO, s_rx_sm, true);

    stdio_set_driver_enabled(&s_driver, true);
}
