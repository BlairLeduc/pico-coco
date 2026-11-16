/* core0.c — core 0's loop: the guest (design.md §6, §11.1, §13).
 *
 * Pico-oric's M7 loop, on the timer's pacing and the CoCo's clock: the
 * guest is counted in master-oscillator ticks (§13.2), and the field's
 * deadline comes from the ticks run. Audio (M8), the park and the menu
 * (M9) are left out, each marked where it will go.
 */

#include "core0.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "card.h"
#include "fieldlog.h"
#include "handoff.h"
#include "kbd.h"
#include "log.h"
#include "southbridge.h"
#include "textpage.h"

/* The heartbeat's period. */
#define HEARTBEAT_US 5000000u

/* A field this far behind its deadline is not caught up: the schedule
 * starts again from now, and the slip is counted (EL §6.3). */
#define SLIP_US 60000u

/* FS, which no key sends, asks for the screen as text in the log
 * (tools/uart-screen.sh), so a run driven over the UART can read back
 * what the panel shows. */
#define UART_SCREEN_DUMP 0x1Cu

#if PICO_COCO_UART
/* The text screen as 16 lines from RAM at the display offset
 * (textpage.h): semigraphics as spaces, inverse as plain. In a graphics
 * mode it is not what the panel shows, and the log says so. */
static void dump_screen(const coco_t *m) {
    uint8_t mode = coco_vdg_mode(m);
    log_printf("  screen       : field %lu, VDG mode $%02X, SAM V%u, offset $%04X%s\n",
               (unsigned long)m->fields, (unsigned)mode, (unsigned)sam6883_v(&m->sam),
               (unsigned)sam6883_display_base(&m->sam),
               (mode & VDG_AG) ? ", graphics: not what is shown" : "");
    for (int row = 0; row < TEXT_ROWS; row++) {
        char line[TEXT_COLS + 1];
        textpage_screen_row(m, row, line);
        log_printf("  |%-32s|\n", line);
    }
}
#endif

/* The UART's bytes typed at the guest as the PicoCalc would send them, so
 * a hardware run can be driven from the workstation that captures it
 * (§17 M6, tools/uart-type.sh). A byte is taken only while keymatrix has
 * room for its events and their releases, so a fast sender loses
 * characters in the UART's FIFO rather than in the replay. */
static void uart_keys(keymatrix_t *k, const coco_t *m) {
#if PICO_COCO_UART
    if (k->q_len + k->n_open + 2u * COCO_KEY_TEXT_EVENTS > COCO_KEY_EVENT_QUEUE) return;
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) return;
    if (ch == UART_SCREEN_DUMP) {
        dump_screen(m);
        return;
    }
    /* M9: the bytes that hold the guest for a card job, open the menu
     * and pause, as the siblings' do. */
    picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
    unsigned n = keymap_picocalc_text((uint8_t)ch, ev);
    for (unsigned i = 0; i < n; i++) keymatrix_event(k, ev[i].state, ev[i].code);
#else
    (void)k;
    (void)m;
#endif
}

/* The keys that ask the emulator rather than the guest for something
 * (§14). Alt+K presses the reset button: RESET, which Color BASIC takes
 * as a warm start, the program kept (§8.3, §13.3). */
static void requests(keymatrix_t *k, coco_t *m) {
    if (k->reset_request) {
        coco_reset(m);
        log_printf("  keys         : the reset button\n");
    }
    /* M9: the menu, pause and screenshots park the guest. */
    if (k->menu_request || k->pause_request || k->shot_request)
        log_printf("  keys         : %s is not in this firmware yet (M9)\n",
                   k->menu_request ? "the menu" : k->pause_request ? "pause" : "the screenshot");
    k->reset_request = k->pause_request = k->menu_request = k->shot_request = false;
}

/* The heartbeat's battery and die: "87%, 31 C", "87% charging, 31 C",
 * with "?" for either before its first read or after a failed one. Bit 7
 * of the gauge is the charger: set proves USB power, clear proves
 * nothing (hardware-notes.md §6). The readings are core 1's. */
static const char *power_text(void) {
    static char text[48];
    int32_t b = g_c1.battery, t = g_c1.temp_c;
    char bat[20] = "?", die[16] = "?";
    if (b >= 0) snprintf(bat, sizeof bat, "%u%%%s", (unsigned)(b & 0x7F), b & 0x80 ? " charging" : "");
    if (t != INT32_MIN) snprintf(die, sizeof die, "%ld C", (long)t);
    snprintf(text, sizeof text, "%s, %s", bat, die);
    return text;
}

/* v in tenths or hundredths, as "12.3" or "1.23". */
static void tenths(char *out, size_t n, uint32_t v10) {
    snprintf(out, n, "%lu.%lu", (unsigned long)(v10 / 10u), (unsigned long)(v10 % 10u));
}

static void hundredths(char *out, size_t n, uint32_t v100) {
    snprintf(out, n, "%lu.%02lu", (unsigned long)(v100 / 100u), (unsigned long)(v100 % 100u));
}

/* Guest ticks as microseconds of real time. */
static uint64_t ticks_us(uint64_t ticks) {
    return ticks * 1000000u / COCO_TICK_HZ;
}

/* A window of the loop's own counters, for the heartbeat and the perf
 * line: both start again from the machine as it is now. */
typedef struct {
    uint64_t us;              /* wall time at the start                   */
    uint64_t ticks, cycles, insns;   /* the machine's                     */
    uint32_t fields, late;
    uint64_t run_us;          /* inside coco_run_field                    */
    uint64_t busy_us;         /* outside the pacing wait                  */
    uint64_t take_us;         /* coco_video_snapshot                      */
} window_t;

static void window_start(window_t *w, const coco_t *m, uint32_t late) {
    memset(w, 0, sizeof *w);
    w->us = time_us_64();
    w->ticks = coco_ticks(m);
    w->cycles = m->cpu.cycles;
    w->insns = m->cpu.instructions;
    w->fields = m->fields;
    w->late = late;
}

#if PICO_COCO_FIELD_LOG
/* The first fields, recorded as they end and printed once all are taken,
 * one every other field, so the log's pace cannot touch the guest's
 * (fieldlog.h, design.md §15.5). */
static fieldlog_t s_fields[COCO_FIELD_LOG_MAX];
static unsigned   s_fields_taken, s_fields_sent;

static void field_log(const coco_t *m) {
    if (s_fields_taken < COCO_FIELD_LOG_MAX) {
        fieldlog_take(&s_fields[s_fields_taken++], m);
        return;
    }
    if (s_fields_sent < COCO_FIELD_LOG_MAX && (m->fields & 1u)) {
        char line[COCO_FIELD_LOG_LINE];
        fieldlog_format(&s_fields[s_fields_sent++], line, sizeof line);
        log_printf("  fieldlog     : %s\n", line);
        if (s_fields_sent == COCO_FIELD_LOG_MAX)
            log_printf("  fieldlog     : %u fields, log dropped %u\n", s_fields_sent,
                       log_dropped());
    }
}
#endif

void core0_run(coco_t *m, keymatrix_t *k) {
    const uint32_t clk_mhz = clock_get_hz(clk_sys) / 1000000u;
    /* main() powered the guest on just before this (main.c). */
    const uint32_t power_on_us = time_us_32();
    uint32_t late = 0, slips = 0;

    /* The schedule: the field that starts after `sched_ticks` guest ticks
     * is due at sched_us plus their duration, worked out from the tick
     * count each time, so the rounding does not accumulate (EL §6.3). A
     * whole second of ticks is moved into sched_us as it passes, exactly,
     * so the product never grows. */
    uint64_t sched_us = time_us_64() + 1000u;   /* the first field on time */
    uint64_t sched_ticks = 0;

    window_t hb, sec;
    window_start(&hb, m, late);
    window_start(&sec, m, late);
    bool ok_seen = false;

    for (;;) {
        uint64_t due = sched_us + ticks_us(sched_ticks);
        uint64_t now = time_us_64();
        if (now < due) {
            /* Core 0's own alarm: sleeping here interrupts nothing. */
            sleep_until(from_us_since_boot(due));
        } else if (now > due) {
            late++;
            if (now - due > SLIP_US) {
                slips++;
                sched_us = now;
                sched_ticks = 0;
            }
        }

        /* M9: between two fields is the one place the guest parks
         * (§6), and the windows start again after it. */

        uint32_t t_busy = time_us_32();

        /* Keys first, so the matrix the guest scans this field is the
         * one the events describe (§11.1). */
        uart_keys(k, m);
        uint8_t state, code;
        while (kbd_pop(&state, &code)) keymatrix_event(k, state, code);
        keymatrix_field(k, m);
        requests(k, m);

        uint32_t t_run = time_us_32();
        uint32_t ran = coco_run_field(m);
        uint32_t run_us = time_us_32() - t_run;
        sched_ticks += ran;
        if (sched_ticks >= COCO_TICK_HZ) {
            sched_ticks -= COCO_TICK_HZ;
            sched_us += 1000000u;
        }

#if PICO_COCO_FIELD_LOG
        field_log(m);
#endif

        /* The boot to the prompt, from power-on (§17 M7). */
        if (!ok_seen && textpage_screen_find(m, "OK", 0) >= 0) {
            ok_seen = true;
            uint32_t at = time_us_32();
            uint32_t guest_us = at - power_on_us;
            uint64_t t = coco_ticks(m);
            log_printf("  boot         : OK at field %lu, %llu ticks (%llu.%03llu s of guest) and "
                       "%lu.%03lu s after the guest's power-on, %lu.%03lu s after the board's "
                       "reset; core 1 ready at %lu.%03lu s (card %s, mount %lu us, ROMs read "
                       "%lu us)\n",
                       (unsigned long)m->fields, (unsigned long long)t,
                       (unsigned long long)(ticks_us(t) / 1000000u),
                       (unsigned long long)(ticks_us(t) / 1000u % 1000u),
                       (unsigned long)(guest_us / 1000000u),
                       (unsigned long)(guest_us / 1000u % 1000u),
                       (unsigned long)(at / 1000000u), (unsigned long)(at / 1000u % 1000u),
                       (unsigned long)(g_boot.ready_us / 1000000u),
                       (unsigned long)(g_boot.ready_us / 1000u % 1000u),
                       card_state_str(g_boot.job.state), (unsigned long)g_boot.job.mount_us,
                       (unsigned long)g_boot.job.load_us);
        }

        /* The field ends where the next active line begins, which is
         * where the snapshot is taken (§9.3, §13.1). */
        int i = pool_claim();
        if (i >= 0) {
            uint32_t t_take = time_us_32();
            coco_video_snapshot(m, &g_pool.buf[i]);
            uint32_t take_us = time_us_32() - t_take;
            hb.take_us += take_us;
            sec.take_us += take_us;
            pool_publish(i);
        }
        /* M8: the field's samples into the audio queue, which then paces. */

        uint32_t busy = time_us_32() - t_busy;
        hb.run_us += run_us;
        sec.run_us += run_us;
        hb.busy_us += busy;
        sec.busy_us += busy;

        now = time_us_64();
        if (now - sec.us >= 1000000u) {
            uint64_t wall = now - sec.us;
            uint64_t gus = ticks_us(coco_ticks(m) - sec.ticks);
            uint32_t fields = m->fields - sec.fields;
            g_c0.busy1000 = (uint32_t)(sec.busy_us * 1000u / wall);
            /* How many times real time the guest would run unpaced. */
            g_c0.head100 = (uint32_t)(gus * 100u / (sec.run_us + 1u));
            g_c0.hz = (uint32_t)(((uint64_t)fields * 1000000u + wall / 2u) / wall);
            __dmb();
            g_c0.seconds++;
            window_start(&sec, m, late);
        }

        if (now - hb.us >= HEARTBEAT_US) {
            uint64_t wall = now - hb.us;
            uint64_t gus = ticks_us(coco_ticks(m) - hb.ticks);
            uint64_t cyc = m->cpu.cycles - hb.cycles;
            uint64_t insns = m->cpu.instructions - hb.insns;
            uint32_t fields = m->fields - hb.fields;
            /* Guest time over wall time: 1.000 is real time. */
            uint32_t rt1000 = (uint32_t)(gus * 1000u / wall);
            uint32_t busy1000 = (uint32_t)(hb.busy_us * 1000u / wall);
            uint32_t guest1000 = (uint32_t)(hb.run_us * 1000u / wall);
            uint32_t take10000 = (uint32_t)(hb.take_us * 10000u / wall);
            uint32_t head100 = (uint32_t)(gus * 100u / (hb.run_us + 1u));
            uint32_t hpi10 = (uint32_t)(hb.run_us * clk_mhz * 10u / (insns + 1u));
            uint32_t cpi100 = (uint32_t)(cyc * 100u / (insns + 1u));
            char busy_s[12], guest_s[12], hpi_s[12], cpi_s[12], take_s[12];
            tenths(busy_s, sizeof busy_s, busy1000);
            tenths(guest_s, sizeof guest_s, guest1000);
            tenths(hpi_s, sizeof hpi_s, hpi10);
            hundredths(cpi_s, sizeof cpi_s, cpi100);
            hundredths(take_s, sizeof take_s, take10000);
            board_stack_t stk;
            board_stack_use(&stk);
            log_printf("  heartbeat    : %lu fields, rt %lu.%03lu, late %lu (+%lu), slips %lu | "
                       "%lu presents (%lu full, %lu dropped), last %lu us, max %lu us | "
                       "keys %lu (%lu lost), polls %lu (longest %lu us), i2c errors %lu | "
                       "undoc %lu (last $%04X at $%04X), split fields %lu, log dropped %u, "
                       "stacks %lu/%lu %lu/%lu, battery %s, card %s\n",
                       (unsigned long)m->fields, (unsigned long)(rt1000 / 1000u),
                       (unsigned long)(rt1000 % 1000u), (unsigned long)late,
                       (unsigned long)(late - hb.late), (unsigned long)slips,
                       (unsigned long)g_c1.presents, (unsigned long)g_c1.full_presents,
                       (unsigned long)g_pool.dropped, (unsigned long)g_c1.last_us,
                       (unsigned long)g_c1.max_us, (unsigned long)g_c1.key_events,
                       (unsigned long)(kbd_overflows() + k->dropped),
                       (unsigned long)g_c1.polls, (unsigned long)g_c1.poll_max_us,
                       (unsigned long)sb_error_count(), (unsigned long)m->cpu.undoc_count,
                       (unsigned)m->cpu.undoc_op, (unsigned)m->cpu.undoc_pc,
                       (unsigned long)m->split_fields, log_dropped(),
                       (unsigned long)stk.used[0], (unsigned long)stk.size[0],
                       (unsigned long)stk.used[1], (unsigned long)stk.size[1], power_text(),
                       card_present() ? "in" : "out");
            log_printf("  perf         : tier %u, %lu MHz, core 0 busy %s%%, guest %s%% of wall, "
                       "headroom %lu.%02lux, %s host cycles/insn, %s cycles/insn, "
                       "%llu insns, %lu fields, snapshot %s%%\n",
                       (unsigned)PICO_COCO_RAM_TIER, (unsigned long)clk_mhz, busy_s, guest_s,
                       (unsigned long)(head100 / 100u), (unsigned long)(head100 % 100u),
                       hpi_s, cpi_s, (unsigned long long)insns, (unsigned long)fields, take_s);
            window_start(&hb, m, late);
        }
    }
}
