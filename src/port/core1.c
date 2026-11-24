/* core1.c — core 1's loop: the panel, the southbridge, the card and the
 * log (design.md §6, §9, §11.1, §12.1).
 *
 * Bring-up in hardware-notes.md §10's order; the boot's ROMs off the
 * card, with the page that says what is missing (roms.h); then the loop:
 * present the newest snapshot, pump the log, poll the keyboard at 30 Hz,
 * and draw the perf line once a second; serve the park (park.h), which
 * runs the menu, and draw the status line. Pico-oric's loop, with M6's
 * retries for a card that goes in silent.
 */

#include <stdio.h>

#include "core1.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "display.h"
#include "dskio.h"
#include "handoff.h"
#include "kbd.h"
#include "keymapio.h"
#include "soundio.h"
#include "lcd.h"
#include "log.h"
#include "menu.h"
#include "park.h"
#include "roms.h"
#include "rtc.h"
#include "sd.h"
#include "settingsio.h"
#include "southbridge.h"
#include "status.h"
#include "storage.h"
#include "tapeio.h"

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

/* A note stays on the status line this long (core1_note). */
#define NOTE_US      3000000u

/* Core 1's own frame: the missing-ROM page (roms.h) before the guest
 * starts, the menu's (menu.h) once it runs. */
static coco_frame_t s_page;

static char     s_note[COCO_TEXT_COLS + 1];
static uint32_t s_note_until;

void core1_note(const char *text) {
    snprintf(s_note, sizeof s_note, "%s", text);
    s_note_until = time_us_32() + NOTE_US;
    display_status(s_note);
}

/* The status line at the foot (§9.6, §14), while it is on: the deck and
 * the cartridge, or the drive at work (status.h); or a note, until it has been up for NOTE_US.
 * Drawn only when it changes. */
static void draw_status(void) {
    if (s_note[0]) {
        if ((int32_t)(time_us_32() - s_note_until) < 0) return;
        s_note[0] = 0;
    }
    char text[COCO_TEXT_COLS + 1] = "";
    if (g_ui.status) {
        int drive = g_machine.rsdos ? (int)g_c0.disk : -1;
        status_format(tapeio_inserted(), g_c0.tape_unsaved != 0, g_c0.deck,
                      g_machine.rsdos ? "RS-DOS" : g_machine.cart ? g_boot.cart_path : "", drive,
                      drive >= 0 ? dskio_inserted((unsigned)drive) : "", text);
    }
    display_status(text);
}

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

/* The settings into g_ui (§12.6), the backlight the file's or the
 * panel's own (hardware-notes.md §6): register values step by 16,
 * 16-240. */
static void apply_settings(const settings_t *st) {
    g_ui.volume = st->volume;
    g_ui.perf_line = st->perf;
    g_ui.status = st->status;
    g_ui.fast_tape = st->fast_tape;
    g_ui.vdg_t1 = st->vdg_t1;
    g_ui.artefacts = st->artefacts;
    g_ui.artefact_phase = st->artefact_phase;
    g_ui.artefact_red = st->artefact_phase == SETTINGS_PHASE_RED;
    g_ui.dark_bg = st->dark_bg;
    display_set_colours(st->colour);
    g_ui.disk_sounds = st->disk_sounds;
    uint8_t r[2];
    if (st->backlight) {
        g_ui.backlight = st->backlight;
        (void)sb_write(SB_REG_BKL, (uint8_t)(g_ui.backlight * 16u), NULL);
    } else if (sb_read(SB_REG_BKL, r) == SB_OK && r[1] >= 16u) {
        g_ui.backlight = r[1] / 16u > 15u ? 15u : r[1] / 16u;
    }
}

/* The boot's card job (design.md §12.1, §12.6), with core 0 waiting, as
 * every card job is (§6), in one mount: the settings, the ROMs, the
 * cartridge and the tape they name; then the page, if Extended Color
 * BASIC cannot start. */
static void boot_rom(void) {
    g_boot.cart_len = 0;
    g_boot.cart_path[0] = 0;
    if (sd_present()) {
        uint32_t t0 = time_us_32();
        int fr = storage_mount();
        g_boot.job.mount_us = time_us_32() - t0;
        g_boot.job.fresult = fr;
        if (fr == 0) {
            settingsio_load(&g_boot.settings);
            card_roms_mounted(&g_boot.job, g_boot.basic, g_boot.basic11, g_boot.ext);
            const settings_t *st = &g_boot.settings;
            /* The card's layouts, and the one the file names (§11.3); a
             * cartridge or boot tape a layout names may choose another
             * after it. */
            keymapio_scan();
            soundio_load();   /* the drives' recordings, if the card has them */
            if (st->layout[0]) {
                int li = keymapio_find(st->layout);
                if (li >= 0) keymapio_choose(keymapio_get((unsigned)li));
                else settingsio_fail("layout", "no such layout");
            }
            if (st->cart[0] && card_is_rsdos(st->cart)) {
                /* The RS-DOS cartridge: Disk BASIC's ROM by its SHA-1
                 * (rsdos.h); main.c refuses it where it cannot run. */
                uint32_t len = 0;
                const char *err = card_disk_rom_load(&g_boot.job, g_boot.cart, &len);
                if (err) settingsio_fail(st->cart, err);
                else {
                    g_boot.cart_len = len;
                    snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s", CARD_RSDOS);
                }
            } else if (st->cart[0]) {
                uint32_t len = 0;
                const char *err = card_cart_load(st->cart, g_boot.cart, &len);
                if (err) settingsio_fail(st->cart, err);
                else {
                    g_boot.cart_len = len;
                    int n = st->cart[0] == '/'
                        ? snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s", st->cart)
                        : snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s/%s",
                                   COCO_CART_DIR, st->cart);
                    if (n >= (int)sizeof g_boot.cart_path) g_boot.cart_path[0] = 0;
                    keymapio_file_loaded(g_boot.cart_path);
                }
            }
            if (st->boot_tape[0]) {
                const char *err = tapeio_boot(st->boot_tape);
                if (err) settingsio_fail(st->boot_tape, err);
            }
            for (unsigned d = 0; d < COCO_DISK_DRIVES; d++) {
                if (!st->boot_disk[d][0]) continue;
                const char *err = dskio_insert(NULL, d, st->boot_disk[d]);
                if (err) settingsio_fail(st->boot_disk[d], err);
            }
            storage_unmount();
        } else {
            card_roms(&g_boot.job, g_boot.basic, g_boot.basic11, g_boot.ext);
            settingsio_none(&g_boot.settings);
        }
    } else {
        card_roms(&g_boot.job, g_boot.basic, g_boot.basic11, g_boot.ext);
        settingsio_none(&g_boot.settings);
    }
    apply_settings(&g_boot.settings);
    menu_init(&s_page);
    g_c1.card_jobs++;
    if (!roms_start_now(&g_boot.job, g_boot.settings.extended)) {
        roms_page(&g_boot.job, &s_page);
        display_present(&s_page, NULL);
        display_settle();
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
    /* The board's clock, for the SmartWatch (rtc.h). */
    t0 = time_us_32();
    rtc_source_t src = rtc_init();
    {
        smartwatch_time_t t;
        smartwatch_civil(g_clock.secs, &t);
        int off = g_clock.offset_min, a = off < 0 ? -off : off;
        log_core1("  clock        : %s, local %04u-%02u-%02u %02u:%02u:%02u, UTC%c%02d:%02d, %lu us\n",
                  src == RTC_SHELL ? "the shell's" : src == RTC_DS3231 ? "the DS3231's, as UTC"
                  : PICO_COCO_DS3231 ? "none, from 2000" : "none, from 2000 (GP4/GP5 the UART's)",
                  t.year, t.month, t.day, t.hour, t.minute, t.second, off < 0 ? '-' : '+',
                  a / 60, a % 60, (unsigned long)(time_us_32() - t0));
    }

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
            display_set_t1(g_ui.vdg_t1);
            display_set_dark_bg(g_ui.dark_bg);
            /* A random phase is chosen afresh at each reset, as a real
             * VDG comes up locked to either edge of its clock (§9.5);
             * the microsecond core 1 sees it on is as random as that. */
            static uint32_t s_resets;
            if (g_c0.resets != s_resets) {
                s_resets = g_c0.resets;
                if (g_ui.artefact_phase == SETTINGS_PHASE_RANDOM) {
                    g_ui.artefact_red = ((time_us_32() >> 3) & 1u) != 0;
                    log_core1("  artifacts    : a reset; the phase is %s\n",
                              g_ui.artefact_red ? "red" : "blue");
                }
            }
            display_set_artefacts(g_ui.artefacts, g_ui.artefact_red);
            display_present(&g_pool.buf[i], &st);
            pool_release(i);
            g_c1.presents++;
            if (st.full) g_c1.full_presents++;
            g_c1.last_us = st.us;
            if (st.us > g_c1.max_us) g_c1.max_us = st.us;
            if (st.us > sec_max_us) sec_max_us = st.us;
        }

        log_pump();
        rtc_poll();

        /* The guest parked: its job. Until core 0 claims the ROMs, a card
         * that goes in is read at once; once the guest runs, a change is
         * only noted, and card work waits for a park (card.h). */
        bool job = false;
        if (park_serve()) {
            /* a hold: a step a loop */
        } else if (card_poll()) {
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
        if (g_boot.claimed) draw_status();

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
