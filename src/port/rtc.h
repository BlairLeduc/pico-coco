/* rtc.h — the board's time, for the SmartWatch (design.md §12.7).
 *
 * Local time, from the first of these that has it:
 *
 *   1. the shell's clock (picocalc_shell.h): UTC in the RP2350's
 *      always-on timer, which the shell sets and which runs on through
 *      the reboot into this program, and the local offset in minutes
 *      the shell had when it started it. Read whenever it is wanted,
 *      with no bus traffic; never set here;
 *   2. a DS3231 on i2c0 at GP4 (SDA) and GP5 (SCL), 100 kHz, address
 *      $68 (hardware-notes.md §1.1), which holds UTC, as the shell
 *      writes it: in builds that leave those pins free, the release
 *      build and the UART0 one, when the shell's clock is not set (the
 *      loader's menu after a power-on). No offset is known then, so the
 *      time is UTC. Read at boot and once a minute, running on from the
 *      microsecond timer between; never written;
 *   3. neither: 2000-01-01 00:00:00 at boot.
 *
 * Core 1 only: rtc_init at bring-up, rtc_poll in its loop. g_clock is
 * what core 0 gives the SmartWatch before each field.
 */
#ifndef PICO_COCO_RTC_H
#define PICO_COCO_RTC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { RTC_NONE, RTC_SHELL, RTC_DS3231 } rtc_source_t;

typedef struct {
    volatile uint32_t secs;      /* local seconds from 2000-01-01 00:00:00 */
    volatile rtc_source_t source;
    volatile int16_t  offset_min;   /* the shell's, from UTC; 0 otherwise */
    volatile uint32_t reads, errors;
} host_clock_t;

extern host_clock_t g_clock;

/* Find the time, as above. Returns where it came from. */
rtc_source_t rtc_init(void);
void rtc_poll(void);

#endif /* PICO_COCO_RTC_H */
