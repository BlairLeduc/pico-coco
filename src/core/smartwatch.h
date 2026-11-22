/* smartwatch.h — a Dallas DS1216 SmartWatch at $FF78-$FF7F
 * (design.md §12.7).
 *
 * Tandy's OS-9 Level One 2.01 has a clock module, clock_tc3, that reads
 * the date at $FF7C from a DS1216 rather than taking it at "TIME ?". The
 * chip answers only reads, so the CPU talks to it by which address it
 * reads, as clock_tc3 does (read off the module, 2026-11-17):
 *
 *   A2 low ($FF78-$FF7B)   a bit written, A0 its value
 *   A2 high ($FF7C-$FF7F)  a bit read, on D0; the rest of the byte is
 *                          the bus as it was
 *
 * Shut, a read with A2 high starts the key again, and each written bit
 * must be the next of the 64-bit key, $C5 $3A $A3 $5C $C5 $3A $A3 $5C,
 * each byte least significant bit first, or the key starts again. Once
 * the key is matched it is open for 64 bits, read or written, then
 * shuts: 8 registers of BCD, each least significant bit first:
 *
 *   0 hundredths   1 seconds   2 minutes   3 hours (24-hour)
 *   4 day of week, 1-7 from Sunday   5 date   6 month   7 year
 *
 * The chip keeps no century. OS-9 Level One keeps the year as years
 * since 1900, 126 for 2026, and its 2.01 SETIME takes four digits; the
 * module turns that into "BCD" by its own sum, ten for each $10, so 126
 * is written $C6, and read back as 126. The year register here is so:
 * the years since 1900, their tens digit up to 15, 1900 to 2059. A
 * register of 26 would read as 1926.
 *
 * The registers are the time the key was matched. A written set is a
 * new time, kept as an offset from the host's, so the clock the port
 * gives (its DS3231, smartwatch_set_host) is never written: a set lasts
 * until power-off, as the guest's own clock would until its battery
 * failed, and never moves the board's. Not in a save state: the offset
 * goes with the machine, and a key half sent is only a few cycles long.
 *
 * As the chip, from the module's code and the DS1216 datasheet's
 * protocol as remembered (§18: medium). Nothing else in the CoCo answers
 * at $FF78-$FF7F, so with it not fitted they read the bus as it was.
 */
#ifndef PICO_COCO_SMARTWATCH_H
#define PICO_COCO_SMARTWATCH_H

#include <stdbool.h>
#include <stdint.h>

#define SMARTWATCH_BASE  0xFF78u   /* through $FF7F                       */
#define SMARTWATCH_A2    0x04u
#define SMARTWATCH_BITS  64u

/* A civil date and time, the year in full. */
typedef struct {
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
} smartwatch_time_t;

typedef struct {
    bool     fitted;
    bool     open;         /* the key matched: the registers are next    */
    uint8_t  key;          /* bits of the key matched while shut         */
    uint8_t  bit;          /* bits of the registers moved while open     */
    uint8_t  reg[8];
    bool     wrote;        /* a bit was written while open               */
    /* The host's clock, in seconds from 2000-01-01 00:00:00, as the port
     * last gave it; and the guest's set, seconds ahead of it. */
    uint32_t host;
    int64_t  offset;
    uint32_t sets;         /* times the guest set it                     */
} smartwatch_t;

void smartwatch_init(smartwatch_t *w);

/* The host's time, given by the port before each field. */
void smartwatch_set_host(smartwatch_t *w, uint32_t seconds_since_2000);

/* A read of $FF78-$FF7F, `bus` the byte the bus holds. */
uint8_t smartwatch_read(smartwatch_t *w, uint16_t a, uint8_t bus);

/* The years the register holds: 1900-01-01 to 2059-12-31, in seconds
 * from 2000-01-01; the time is held to them. */
#define SMARTWATCH_FIRST (-3155673600LL)
#define SMARTWATCH_LAST  (1893455999LL)

/* Seconds from 2000-01-01, before it negative, and back: days from
 * civil, as Howard Hinnant's algorithm. */
int64_t smartwatch_seconds(const smartwatch_time_t *t);
void    smartwatch_civil(int64_t seconds, smartwatch_time_t *t);

#endif /* PICO_COCO_SMARTWATCH_H */
