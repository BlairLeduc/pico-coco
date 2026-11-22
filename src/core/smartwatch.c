/* smartwatch.c — the DS1216 SmartWatch (design.md §12.7). See
 * smartwatch.h. */

#include "smartwatch.h"

#include <string.h>

static const uint8_t k_key[8] = { 0xC5u, 0x3Au, 0xA3u, 0x5Cu, 0xC5u, 0x3Au, 0xA3u, 0x5Cu };

void smartwatch_init(smartwatch_t *w) {
    bool fitted = w->fitted;
    memset(w, 0, sizeof *w);
    w->fitted = fitted;
}

void smartwatch_set_host(smartwatch_t *w, uint32_t seconds_since_2000) {
    w->host = seconds_since_2000;
}

/* ---- the calendar ------------------------------------------------------ */

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2u;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m > 2u ? (int64_t)m - 3 : (int64_t)m + 9) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 730425;   /* 0000-03-01 to 2000-01-01 */
}

int64_t smartwatch_seconds(const smartwatch_time_t *t) {
    return days_from_civil(t->year, t->month, t->day) * 86400 + t->hour * 3600 +
           t->minute * 60 + t->second;
}

void smartwatch_civil(int64_t s, smartwatch_time_t *t) {
    int64_t days = s >= 0 ? s / 86400 : -((-s + 86399) / 86400);
    int64_t r = s - days * 86400;
    int64_t z = days + 730425;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    t->year = (uint16_t)(yoe + era * 400 + (m <= 2));
    t->month = (uint8_t)m;
    t->day = (uint8_t)d;
    t->hour = (uint8_t)(r / 3600);
    t->minute = (uint8_t)(r / 60 % 60);
    t->second = (uint8_t)(r % 60);
}

bool smartwatch_valid(const smartwatch_time_t *t) {
    static const uint8_t k_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (t->month < 1u || t->month > 12u || t->hour > 23u || t->minute > 59u || t->second > 59u)
        return false;
    unsigned y = t->year;
    bool leap = (y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u;
    unsigned last = k_days[t->month - 1u] + (t->month == 2u && leap);
    return t->day >= 1u && t->day <= last;
}

/* ---- the registers ----------------------------------------------------- */

static uint8_t bcd(unsigned v) { return (uint8_t)((v / 10u) << 4 | v % 10u); }

/* BCD, its tens digit to `tens` (9, or 15 for the year: smartwatch.h). */
static bool unbcd(uint8_t b, unsigned tens, unsigned lo, unsigned hi, unsigned *out) {
    if ((b & 0x0Fu) > 9u || (b >> 4) > tens) return false;
    unsigned v = (b >> 4) * 10u + (b & 0x0Fu);
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

/* The time now, into the registers, as the key is matched. */
static void latch(smartwatch_t *w) {
    int64_t now = (int64_t)w->host + w->offset;
    if (now < SMARTWATCH_FIRST) now = SMARTWATCH_FIRST;
    if (now > SMARTWATCH_LAST) now = SMARTWATCH_LAST;
    smartwatch_time_t t;
    smartwatch_civil(now, &t);
    int64_t days = (now - SMARTWATCH_FIRST) / 86400;   /* 1900-01-01 was a Monday, 2 */
    w->reg[0] = 0;
    w->reg[1] = bcd(t.second);
    w->reg[2] = bcd(t.minute);
    w->reg[3] = bcd(t.hour);
    w->reg[4] = (uint8_t)((days + 1) % 7 + 1);
    w->reg[5] = bcd(t.day);
    w->reg[6] = bcd(t.month);
    w->reg[7] = bcd(t.year - 1900u);
}

/* The registers written, as a new time; one that is not a date is not
 * taken, as the chip would keep nonsense the guest would read back. */
static void take(smartwatch_t *w) {
    unsigned s, mi, h, d, mo, y;
    if (!unbcd(w->reg[1] & 0x7Fu, 9, 0, 59, &s) || !unbcd(w->reg[2] & 0x7Fu, 9, 0, 59, &mi) ||
        !unbcd(w->reg[3] & 0x3Fu, 9, 0, 23, &h) || !unbcd(w->reg[5] & 0x3Fu, 9, 1, 31, &d) ||
        !unbcd(w->reg[6] & 0x1Fu, 9, 1, 12, &mo) || !unbcd(w->reg[7], 15, 0, 159, &y))
        return;
    smartwatch_time_t t = { (uint16_t)(1900u + y), (uint8_t)mo, (uint8_t)d,
                            (uint8_t)h, (uint8_t)mi, (uint8_t)s };
    if (!smartwatch_valid(&t)) return;
    w->offset = smartwatch_seconds(&t) - (int64_t)w->host;
    w->sets++;
}

uint8_t smartwatch_read(smartwatch_t *w, uint16_t a, uint8_t bus) {
    bool write = (a & SMARTWATCH_A2) == 0u;
    unsigned v = a & 1u;
    if (!w->open) {
        if (!write) {
            w->key = 0;
            return bus;
        }
        if (((k_key[w->key / 8u] >> (w->key % 8u)) & 1u) != v) {
            w->key = 0;
            return bus;
        }
        if (++w->key == SMARTWATCH_BITS) {
            w->open = true;
            w->bit = 0;
            w->wrote = false;
            w->key = 0;
            latch(w);
        }
        return bus;
    }
    unsigned r = w->bit / 8u, b = w->bit % 8u;
    uint8_t out = bus;
    if (write) {
        w->reg[r] = (uint8_t)((w->reg[r] & ~(1u << b)) | v << b);
        w->wrote = true;
    } else {
        out = (uint8_t)((bus & 0xFEu) | ((w->reg[r] >> b) & 1u));
    }
    if (++w->bit == SMARTWATCH_BITS) {
        w->open = false;
        if (w->wrote) take(w);
    }
    return out;
}
