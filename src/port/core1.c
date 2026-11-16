/* core1.c — core 1's loop: the panel, the southbridge, the card and the
 * log (design.md §6, §9, §11.1, §12.1).
 *
 * Bring-up in hardware-notes.md §10's order; the boot's ROMs off the
 * card, with the page that says what is missing (roms.h); then the loop:
 * present the newest snapshot, pump the log, poll the keyboard at 30 Hz,
 * and draw the perf line once a second. Pico-oric's M7 loop, with M6's
 * retries for a card that goes in silent. The menu and the park are M9's.
 */

#include "core1.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "display.h"
#include "handoff.h"
#include "kbd.h"
#include "lcd.h"
#include "log.h"
#include "roms.h"
#include "southbridge.h"
#include "status.h"

/* Keyboard polls, as hardware-notes.md §6.1 and design.md §11.1 have them;
 * the poll also feeds the MCU's 2.5 s bus watchdog. */
#define KBD_POLL_US  33333u
/* The battery gauge and the die, for the heartbeat. */
#define BAT_POLL_US  5000000u
#define TEMP_POLL_US 1000000u

/* A card that is in but not yet answering is tried again this often, so
 * many times, while it stays in (hardware-notes.md §7.1): seen in M6, a
 * job 250 ms after card detect settled found the card silent. */
#define CARD_RETRY_US 500000u
#define CARD_RETRIES  3u

/* The missing-ROM page, core 1's own frame (roms.h). */
static coco_frame_t s_page;

/* The perf line at the top (design.md §7.5, §9.6): core 0's last second,
 * and core 1's longest present and dropped snapshots over its own.
 * Hidden, it is blank; drawn only when the text changes. */
static void draw_perf(uint32_t present_max_us, uint32_t dropped) {
    char text[COCO_TEXT_COLS + 1] = "";
    if (g_ui.perf_line && g_c0.seconds) {
        perf_line_t p = {
            .busy1000 = g_c0.busy1000, .head100 = g_c0.head100,
            .present_us = present_max_us, .dropped = dropped,
            .underruns = g_c0.underruns, .late = g_c0.late_refills,
        };
        status_perf_format(&p, text);
    }
    display_perf(text);
}

/* The boot's ROMs off the card (design.md §12.1), with core 0 waiting,
 * as every card job will be (§6); and the page, if Extended Color BASIC
 * cannot start. */
static void boot_rom(void) {
    card_roms(&g_boot.job, g_boot.basic, g_boot.ext);
    g_c1.card_jobs++;
    if (roms_choice(&g_boot.job) != BOOT_EXTENDED) {
        roms_page(&g_boot.job, &s_page);
        display_present(&s_page, NULL);
    }
}

/* The card went in or out with the page up: the guest has not started,
 * so there is nothing to park, and the job runs again unless core 0 has
 * claimed what the last one found (handoff.h). */
static void boot_rom_again(void) {
    g_boot.busy = true;
    __dmb();
    if (!g_boot.claimed) {
        boot_rom();
        __dmb();
        g_boot.generation++;
    }
    __dmb();
    g_boot.busy = false;
}

void core1_main(void) {
    g_bringup.start_us = time_us_32();

    /* 1. The southbridge first: a dead bus is the first thing to know
     *    about (hardware-notes.md §10). */
    g_c1.i2c_hz = sb_init();
    uint8_t r[2];
    uint32_t t0 = time_us_32();
    if (sb_read(SB_REG_VER, r) == SB_OK) g_c1.sb_version = r[1];
    g_bringup.i2c_us = time_us_32() - t0;

    /* 2. The LCD. lcd_init sleeps through the panel's reset, which sets
     *    alarms whose IRQ is core 0's (hardware-notes.md §9.7); harmless
     *    here, because core 0 is only waiting for `ready`, and the guest
     *    has not started. Nothing after this sleeps. */
    t0 = time_us_32();
    g_c1.spi_hz = lcd_init();
    g_bringup.lcd_us = time_us_32() - t0;
    display_init();
    board_temp_init();

    /* 3. The card's ROMs. */
    t0 = time_us_32();
    boot_rom();
    g_bringup.card_us = time_us_32() - t0;
    (void)card_poll();   /* the slot's level as the job found it */

    g_boot.ready_us = time_us_32();
    __dmb();
    g_c1.ready = true;

    uint32_t now = time_us_32();
    uint32_t next_poll = now, next_bat = now, next_temp = now;
    uint32_t sec_start = now, sec_max_us = 0, sec_dropped = g_pool.dropped;
    uint32_t next_retry = now;
    unsigned retries = 0;     /* left for the card now in       */
    bool     retried = false; /* this card has had its retries  */
    for (;;) {
        int i = pool_take();
        if (i >= 0) {
            display_stats_t st;
            display_present(&g_pool.buf[i], &st);
            pool_release(i);
            g_c1.presents++;
            if (st.full) g_c1.full_presents++;
            g_c1.last_us = st.us;
            if (st.us > g_c1.max_us) g_c1.max_us = st.us;
            if (st.us > sec_max_us) sec_max_us = st.us;
        }

        log_pump();

        /* Until core 0 claims the ROMs, a card that goes in is read at
         * once; once the guest runs, a change is only noted, and card
         * work waits for the park (M9, card.h). */
        bool job = false;
        if (card_poll()) {
            log_core1("  card         : %s\n", card_present() ? "in" : "out");
            retries = 0;
            job = !g_boot.claimed;
        } else if (retries > 0 && card_present() && !g_boot.claimed &&
                   (int32_t)(time_us_32() - next_retry) >= 0) {
            retries--;
            log_core1("  card         : trying again\n");
            job = true;
        }
        if (job) {
            boot_rom_again();
            card_state_t st = g_boot.job.state;
            if (st == CARD_UNUSABLE && retries == 0 && !retried) {
                retries = CARD_RETRIES;
                retried = true;
            }
            if (st != CARD_UNUSABLE) retries = 0;
            next_retry = time_us_32() + CARD_RETRY_US;
        }
        if (!card_present()) retried = false;

        now = time_us_32();
        if (now - sec_start >= 1000000u) {
            sec_start = now;
            uint32_t dropped = g_pool.dropped;
            draw_perf(sec_max_us, dropped - sec_dropped);
            sec_max_us = 0;
            sec_dropped = dropped;
        }

        /* One poll is an I2C transaction of ~4.8 ms (hardware-notes.md
         * §6.1), so at most one of these runs between two presents. */
        now = time_us_32();
        if ((int32_t)(now - next_poll) >= 0) {
            next_poll = now + KBD_POLL_US;
            g_c1.key_events += kbd_poll();
            uint32_t us = time_us_32() - now;
            if (us > g_c1.poll_max_us) g_c1.poll_max_us = us;
            g_c1.polls++;
        } else if ((int32_t)(now - next_bat) >= 0) {
            next_bat = now + BAT_POLL_US;
            g_c1.battery = sb_read(SB_REG_BAT, r) == SB_OK ? r[1] : -1;
        } else if ((int32_t)(now - next_temp) >= 0) {
            next_temp = now + TEMP_POLL_US;
            g_c1.temp_c = board_temp_c();
        }

        /* Never sleep_us here (hardware-notes.md §9.7). */
        if (i < 0) busy_wait_us_32(20);
    }
}
