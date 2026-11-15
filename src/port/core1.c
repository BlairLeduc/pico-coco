/* core1.c — core 1's loop: the panel, the southbridge, the card and the
 * log (design.md §6, §9, §11.1, §12).
 *
 * M6's shape (§17): bring-up in hardware-notes.md §10's order, its
 * costs measured, then the loop with no guest to present. M7: the
 * presenter, taking snapshots from the pool, joins the loop.
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
#include "southbridge.h"

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

static card_job_t s_job;

void core1_main(void) {
    /* 1. The southbridge first: a dead bus is the first thing to know
     *    about (hardware-notes.md §10). */
    g_c1.i2c_hz = sb_init();
    uint8_t r[2];
    uint32_t t0 = time_us_32();
    if (sb_read(SB_REG_VER, r) == SB_OK) g_c1.sb_version = r[1];
    g_bringup.i2c_us = time_us_32() - t0;

    /* 2. The LCD. lcd_init sleeps through the panel's reset, which sets
     *    alarms whose IRQ is core 0's (hardware-notes.md §9.7); harmless
     *    here, because core 0 is only waiting for `ready`. Nothing after
     *    this sleeps. */
    g_c1.spi_hz = lcd_init();
    display_measure(&g_bringup.fill_us, &g_bringup.blit_us, &g_bringup.row_us,
                    &g_bringup.panel_us);
    display_test_pattern();
    board_temp_init();

    /* 3. The card's ROMs (design.md §12.1), with core 0 waiting, as
     *    every card job will be (§6). */
    card_roms(&s_job);
    g_c1.card_jobs++;
    (void)card_poll();   /* the slot's level as the job found it */

    __dmb();
    g_c1.ready = true;

    uint32_t now = time_us_32();
    uint32_t next_poll = now, next_bat = now, next_temp = now;
    uint32_t next_retry = now;
    unsigned retries = 0;     /* left for the card now in       */
    bool     retried = false; /* this card has had its retries  */
    for (;;) {
        log_pump();

        /* M6 has no guest to park, so a card that goes in is read at
         * once; from M7 that is a parked job (card.h). */
        bool job = false;
        if (card_poll()) {
            log_core1("  card         : %s\n", card_present() ? "in" : "out");
            retries = 0;
            job = card_present();
        } else if (retries > 0 && card_present() &&
                   (int32_t)(time_us_32() - next_retry) >= 0) {
            retries--;
            log_core1("  card         : trying again\n");
            job = true;
        }
        if (job) {
            card_roms(&s_job);
            g_c1.card_jobs++;
            if (s_job.state == CARD_UNUSABLE && retries == 0 && !retried) {
                retries = CARD_RETRIES;
                retried = true;
            }
            if (s_job.state != CARD_UNUSABLE) retries = 0;
            next_retry = time_us_32() + CARD_RETRY_US;
        }
        if (!card_present()) retried = false;

        /* One poll is an I2C transaction of ~4.8 ms (hardware-notes.md
         * §6.1), so at most one of these runs a loop. */
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
        busy_wait_us_32(20);
    }
}
