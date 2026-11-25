/* serial.h — the Deluxe RS-232 Program Pak's line on the PicoCalc's
 * USB-C connector (design.md §12.11).
 *
 * UART0 on GP0 (TX) and GP1 (RX), through the carrier's CH340C
 * (hardware-notes.md §1.1), in every build: the UART0 development
 * build's log is on PIO instead (piouart.h). The line runs at the rate
 * and in the format the guest's 6551 is set to (acia_baud, acia_format),
 * so a terminal on the host is set to the same.
 *
 * Core 1 owns the UART. Its interrupt, on core 1, moves received bytes
 * into a ring and refills the transmit FIFO from another, so a core 1
 * loop busy presenting or polling the keyboard, or a park, loses
 * nothing up to a ring's worth. Core 0, between fields, moves the rings'
 * bytes to and from the 6551's own (serial_field). Characters the guest
 * does not take, because no pak is fitted or its receiver is off, are
 * lost, as on the line.
 */
#ifndef PICO_COCO_SERIAL_H
#define PICO_COCO_SERIAL_H

#include <stdint.h>

#include "coco.h"

typedef struct {
    uint32_t rx, tx;           /* bytes through the UART                 */
    uint32_t rx_lost;          /* a full ring turned away                */
    uint32_t errors;           /* framing, parity, break or overrun      */
    uint32_t baud;             /* as the UART is set                     */
} serial_stats_t;

/* Core 1, before it says it is ready: the UART, its pins and its
 * interrupt, idle at 9600 8N1. */
void serial_init(void);

/* Core 1's loop: the transmit FIFO started when core 0 has queued bytes
 * and the interrupt is idle. Never waits. */
void serial_pump(void);

/* Core 0, between fields: the line set to the guest's format if it has
 * changed, the received bytes given to the 6551, and what it sent
 * queued for the UART. */
void serial_field(coco_t *m);

void serial_stats(serial_stats_t *out);

#endif /* PICO_COCO_SERIAL_H */
