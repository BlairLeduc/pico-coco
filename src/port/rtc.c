/* rtc.c — the board's time (design.md §12.7). See rtc.h. */

#include "rtc.h"

#include "picocalc_shell.h"
#include "pico/stdlib.h"
#include "smartwatch.h"

#ifndef PICOCALC_SHELL_HAS_TIME
#error "the shell's clock needs hardware_powman linked"
#endif

#if PICO_COCO_DS3231
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define RTC_I2C        i2c0
#define RTC_PIN_SDA    4
#define RTC_PIN_SCL    5
#define RTC_ADDR       0x68u
#define RTC_HZ         100000u
#define RTC_TIMEOUT_US 5000u
#endif

#define RTC_EVERY_US   60000000u   /* the DS3231 read again once a minute  */
#define UNIX_2000      946684800   /* 2000-01-01 in seconds from 1970      */

host_clock_t g_clock;

static uint32_t s_base;          /* seconds at s_base_us                  */
static uint64_t s_base_us;
static uint64_t s_next_us;

/* The shell's clock, local, in seconds from 2000; false if not set. */
static bool shell_now(uint32_t *secs) {
    int64_t utc_ms;
    int off;
    if (!picocalc_shell_time(&utc_ms, &off)) return false;
    int64_t s = utc_ms / 1000 + (int64_t)off * 60 - UNIX_2000;
    if (s < 0 || s > SMARTWATCH_LAST) return false;
    *secs = (uint32_t)s;
    g_clock.offset_min = (int16_t)off;
    return true;
}

#if PICO_COCO_DS3231
static unsigned unbcd(uint8_t b) { return (b >> 4) * 10u + (b & 0x0Fu); }

/* Registers 0-6: seconds, minutes, hours, day, date, month and century,
 * year; BCD. The hours in 12-hour mode have bit 6 set and PM in bit 5.
 * Nothing outside 2000-2059, or not a date, is taken. */
static bool read_ds3231(uint32_t *secs) {
    uint8_t reg = 0, r[7];
    if (i2c_write_timeout_us(RTC_I2C, RTC_ADDR, &reg, 1, true, RTC_TIMEOUT_US) != 1 ||
        i2c_read_timeout_us(RTC_I2C, RTC_ADDR, r, 7, false, RTC_TIMEOUT_US) != 7) {
        g_clock.errors++;
        return false;
    }
    unsigned h;
    if (r[2] & 0x40u) {
        h = unbcd(r[2] & 0x1Fu) % 12u + ((r[2] & 0x20u) ? 12u : 0u);
    } else {
        h = unbcd(r[2] & 0x3Fu);
    }
    smartwatch_time_t t = {
        .year = (uint16_t)(2000u + unbcd(r[6]) + ((r[5] & 0x80u) ? 100u : 0u)),
        .month = (uint8_t)unbcd(r[5] & 0x1Fu),
        .day = (uint8_t)unbcd(r[4] & 0x3Fu),
        .hour = (uint8_t)h,
        .minute = (uint8_t)unbcd(r[1] & 0x7Fu),
        .second = (uint8_t)unbcd(r[0] & 0x7Fu),
    };
    if (t.year > 2059u || t.month < 1u || t.month > 12u || t.day < 1u || t.day > 31u ||
        t.hour > 23u || t.minute > 59u || t.second > 59u) {
        g_clock.errors++;
        return false;
    }
    g_clock.reads++;
    *secs = (uint32_t)smartwatch_seconds(&t);
    return true;
}
#endif

static void publish(uint64_t now) {
    g_clock.secs = s_base + (uint32_t)((now - s_base_us) / 1000000u);
}

rtc_source_t rtc_init(void) {
    s_base = 0;
    s_base_us = time_us_64();
    s_next_us = s_base_us + RTC_EVERY_US;
    g_clock.source = RTC_NONE;
    g_clock.offset_min = 0;
    uint32_t secs;
    if (shell_now(&secs)) {
        g_clock.source = RTC_SHELL;
        g_clock.secs = secs;
        return RTC_SHELL;
    }
#if PICO_COCO_DS3231
    i2c_init(RTC_I2C, RTC_HZ);
    gpio_set_function(RTC_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(RTC_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(RTC_PIN_SDA);
    gpio_pull_up(RTC_PIN_SCL);
    if (read_ds3231(&secs)) {
        s_base = secs;
        s_base_us = time_us_64();
        g_clock.source = RTC_DS3231;
    }
#endif
    publish(time_us_64());
    return g_clock.source;
}

void rtc_poll(void) {
    uint64_t now = time_us_64();
    if (g_clock.source == RTC_SHELL) {
        uint32_t secs;
        if (shell_now(&secs)) g_clock.secs = secs;
        return;
    }
#if PICO_COCO_DS3231
    if (g_clock.source == RTC_DS3231 && now >= s_next_us) {
        s_next_us = now + RTC_EVERY_US;
        uint32_t secs;
        if (read_ds3231(&secs)) {
            s_base = secs;
            s_base_us = time_us_64();
            now = s_base_us;
        }
    }
#endif
    publish(now);
}
