/* piouart.h — the development log on a PIO UART (design.md §12.11).
 *
 * In the UART0 development build (PICO_COCO_UART_PORT=0) the log and the
 * typed keys of tools/uart-type.sh stay on GP2/GP3, 115200 8N1, but
 * through PIO, so that the hardware UART0 is the RS-232 pak's on GP0/GP1
 * (serial.h), as in the build that ships (hardware-notes.md §1.1). It is
 * stdio's driver: printf and getchar_timeout_us reach it as they reached
 * the UART, and log_pump feeds it without waiting.
 */
#ifndef PICO_COCO_PIOUART_H
#define PICO_COCO_PIOUART_H

#include <stdbool.h>

/* The two state machines on pio0, and stdio's driver; before the first
 * printf. */
void piouart_init(unsigned tx_pin, unsigned rx_pin, unsigned baud);

/* Room for a byte in the transmit FIFO, and one byte into it. */
bool piouart_writable(void);
void piouart_putc(char c);

#endif /* PICO_COCO_PIOUART_H */
