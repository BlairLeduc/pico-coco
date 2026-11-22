/* core0.c — core 0's loop: the guest (design.md §6, §11.1, §13).
 *
 * Pico-oric's loop, in the CoCo's clock: the guest is counted in
 * master-oscillator ticks (§13.2). Paced on the audio queue (§10, M8),
 * or with PICO_COCO_AUDIO=OFF on the timer against a deadline from the
 * ticks run, the control for audio's cost (EL §6.3). The park, between
 * two fields, serves the disk, the tape, the menu and pause (park.h).
 */

#include "core0.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#if PICO_COCO_AUDIO
#include "audio.h"
#endif
#include "card.h"
#include "fieldlog.h"
#include "handoff.h"
#include "kbd.h"
#include "keymapio.h"
#include "log.h"
#include "dskio.h"
#include "park.h"
#include "roms.h"
#include "rtc.h"
#include "southbridge.h"
#include "status.h"
#include "tapeio.h"
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
/* US asks for pause and RS for the menu, as the siblings' do; GS holds
 * the guest for a card job (park.h). Returns the park asked for. */
#define UART_PAUSE 0x1Fu
#define UART_MENU  0x1Eu

static uint32_t uart_keys(keymatrix_t *k, const coco_t *m) {
#if PICO_COCO_UART
    if (k->q_len + k->n_open + 2u * COCO_KEY_TEXT_EVENTS > COCO_KEY_EVENT_QUEUE) return PARK_NONE;
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) return PARK_NONE;
    if (ch == UART_SCREEN_DUMP) {
        dump_screen(m);
        return PARK_NONE;
    }
    if (ch == UART_HOLD) return PARK_HOLD;
    if (ch == UART_PAUSE) return PARK_PAUSE;
    if (ch == UART_MENU) return PARK_MENU;
    picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
    unsigned n = keymap_picocalc_text((uint8_t)ch, ev);
    for (unsigned i = 0; i < n; i++) keymatrix_event(k, ev[i].state, ev[i].code);
#else
    (void)k;
    (void)m;
#endif
    return PARK_NONE;
}

/* The keys that ask the emulator rather than the guest for something
 * (§14): the menu and pause park the guest at the next boundary; Alt+K
 * presses the reset button: RESET, which Color BASIC takes as a warm
 * start, the program kept (§8.3, §13.3). Returns the park, or
 * PARK_NONE. */
static uint32_t requests(keymatrix_t *k, coco_t *m) {
    uint32_t why = PARK_NONE;
    if (k->reset_request) {
        coco_reset(m);
        log_printf("  keys         : the reset button\n");
    }
    if (k->menu_request) why = PARK_MENU;
    else if (k->pause_request) why = PARK_PAUSE;
    if (k->shot_request) log_printf("  keys         : screenshots are not in this firmware yet\n");
    k->reset_request = k->pause_request = k->menu_request = k->shot_request = false;
    return why;
}

void core0_power_on(coco_t *m, const machine_t *mc) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = mc->ram_k;
    /* The RS-DOS cartridge, never in a 4 KB CoCo (roms.h). */
    bool rsdos = mc->rsdos && mc->cart && g_boot.cart_len > 0 && mc->ram_k >= 16u;
    cfg.rsdos = rsdos;
    coco_init(m, &cfg);
    /* Color BASIC 1.1 kept apart for a 4 KB CoCo (roms.h); otherwise the
     * job's image, 1.3 or 1.1 without it. */
    bool use11 = mc->basic == ROM_COCO_BAS11 && g_boot.job.bas11;
    coco_load_rom(m, 0xA000u, use11 ? g_boot.basic11 : g_boot.basic, COCO_ROM_HALF);
    if (mc->extended) coco_load_rom(m, 0x8000u, g_boot.ext, COCO_ROM_HALF);
    bool cart = mc->cart && g_boot.cart_len > 0;
    if (cart) coco_load_rom(m, 0xC000u, g_boot.cart, g_boot.cart_len);
    /* Disk BASIC is started by Color BASIC's look at $C000, not by the
     * CART line (§12.2). */
    if (rsdos) coco_cart_autostart(m, false);
    coco_power_on(m);
    tapeio_attach(m);
    dskio_attach(m);
#if PICO_COCO_AUDIO
    uint32_t rate_num, rate_den;
    audio_rate(&rate_num, &rate_den);
    coco_audio_set_rate(m, rate_num, rate_den);
#endif
    g_machine = *mc;
    g_machine.cart = cart;
    g_machine.rsdos = rsdos;
}

/* While the tape's signal runs the guest runs unpaced: the tape is
 * clocked in guest ticks, so it cannot tell (§12.3; EL §9.3).
 * PICO_COCO_TURBO=OFF paces it, the control for its measurement. */
static bool turbo_now(const coco_t *m) {
    return PICO_COCO_TURBO && coco_cassette_running(m);
}

/* What the menu changed, applied by the core that owns it (EL §2.5).
 * True if the machine was powered on again. */
static bool apply_ui(coco_t *m) {
    coco_cassette_set_fast(m, g_ui.fast_tape);
#if PICO_COCO_AUDIO
    audio_set_volume(g_ui.volume * 32u);
#endif
    if (g_ui.reset) {
        g_ui.reset = false;
        coco_reset(m);
        log_printf("  menu         : reset (the RESET line)\n");
    }
    if (g_ui.restart) {
        machine_t mc = g_ui.restart_cfg;
        g_ui.restart = false;
        core0_power_on(m, &mc);
        coco_cassette_set_fast(m, g_ui.fast_tape);
        log_printf("  menu         : powered on, %u KB, %s (%s)%s%s\n", mc.ram_k,
                   mc.extended ? "Extended Color BASIC" : "Color BASIC",
                   romset_images[mc.basic].file,
                   g_machine.cart ? ", cartridge " : "", g_machine.cart ? g_boot.cart_path : "");
        return true;
    }
    return false;
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

/* The counters, where a debugger can read them (handoff.h). The
 * updates count is written last, so a reader that sees it move knows
 * the words before it are from this second or the next. */
static void swd_update(const coco_t *m, const keymatrix_t *k, uint32_t late, uint32_t slips) {
    volatile swd_counters_t *c = &g_swd;
    c->uptime_ms = to_ms_since_boot(get_absolute_time());
    c->fields = m->fields;
    c->cycles = (uint32_t)m->cpu.cycles;
    c->late = late;
    c->slips = slips;
    c->presents = g_c1.presents;
    c->snapshots_dropped = g_pool.dropped;
    c->key_events = g_c1.key_events;
    c->keys_lost = kbd_overflows() + k->dropped;
    c->polls = g_c1.polls;
    c->i2c_errors = sb_error_count();
    c->undoc = m->cpu.undoc_count;
    c->log_dropped = log_dropped();
#if PICO_COCO_AUDIO
    audio_stats_t au;
    audio_stats(&au, false);
    c->underrun_samples = au.underrun_samples;
    c->late_refills = au.late_refills;
    c->consumed = au.consumed;
#endif
    c->pcm_overflow = m->pcm.overflow;
    c->sound_edges = m->sound_edges;
    c->split_fields = m->split_fields;
    c->card_changes = card_changes();
    c->parks = g_park_stats.parks;
    c->busy1000 = g_c0.busy1000;
    c->battery = g_c1.battery;
    c->temp_c = g_c1.temp_c;
    /* The display offset's first byte in RAM: the text screen while
     * BASIC shows it, $0400 (§9.2). */
    int32_t at = coco_vram_index(m, (uint16_t)(sam6883_f(&m->sam) * 512u));
    c->screen = at >= 0 ? (uint32_t)(uintptr_t)&m->ram[at] : 0u;
    __dmb();
    c->updates++;
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

#if PICO_COCO_AUDIO
/* The heartbeat's audio line: samples consumed a second against the
 * microsecond timer, the control quantity (EL §6.3), and the two
 * counters apart (HW §5.8). */
static void log_audio(const coco_t *m, const audio_stats_t *last, uint64_t wall,
                      uint32_t edges) {
    audio_stats_t au;
    audio_stats(&au, true);
    uint32_t hz100 = (uint32_t)((uint64_t)(au.consumed - last->consumed) * 100000000u / wall);
    log_printf("  audio        : %lu.%02lu Hz consumed, queue %lu (low %lu), underrun samples "
               "%lu (+%lu), late refills %lu (+%lu), bus changes %lu, pcm overflow %lu\n",
               (unsigned long)(hz100 / 100u), (unsigned long)(hz100 % 100u),
               (unsigned long)au.level, (unsigned long)au.low_water,
               (unsigned long)au.underrun_samples,
               (unsigned long)(au.underrun_samples - last->underrun_samples),
               (unsigned long)au.late_refills,
               (unsigned long)(au.late_refills - last->late_refills), (unsigned long)edges,
               (unsigned long)m->pcm.overflow);
}
#endif

void core0_run(coco_t *m, keymatrix_t *k) {
    const uint32_t clk_mhz = clock_get_hz(clk_sys) / 1000000u;
    /* main() powered the guest on just before this (main.c). */
    const uint32_t power_on_us = time_us_32();
    uint32_t late = 0, slips = 0;   /* the timer's pacing; 0 on audio */
#if PICO_COCO_AUDIO
    /* A field's samples, drained after it and pushed to the queue. */
    static int16_t pcm[COCO_AUDIO_BUF_LEN];
    audio_stats_t au_last;
    audio_stats(&au_last, true);
    uint32_t hb_edges = m->sound_edges;
#else
    /* The schedule: the field that starts after `sched_ticks` guest ticks
     * is due at sched_us plus their duration, worked out from the tick
     * count each time, so the rounding does not accumulate (EL §6.3). A
     * whole second of ticks is moved into sched_us as it passes, exactly,
     * so the product never grows. */
    uint64_t sched_us = time_us_64() + 1000u;   /* the first field on time */
    uint64_t sched_ticks = 0;
#endif

    window_t hb, sec;
    window_start(&hb, m, late);
    window_start(&sec, m, late);
    bool ok_seen = false;
    uint32_t why = PARK_NONE;
    unsigned page = 0;
    bool alt = false;
    uint32_t turbo_fields = 0;   /* run unpaced, since boot */
    /* Each stretch the signal runs, measured whole: turbo's speed, or
     * with PICO_COCO_TURBO=OFF core 0's share while it plays. */
    bool deck_was = false;
    uint64_t deck_us = 0, deck_ticks = 0, deck_busy = 0;
    /* The disk's parks, a track each (§12.4). */
    uint32_t disk_parks = 0, disk_park_max = 0;
    uint64_t disk_parked_us = 0;
    park_init(m);
    (void)apply_ui(m);
    keymatrix_set_layout(k, g_ui.layout);

    for (;;) {
        uint64_t now;
#if !PICO_COCO_AUDIO
        uint64_t due = sched_us + ticks_us(sched_ticks);
        now = time_us_64();
        if (turbo_now(m)) {
            /* Unpaced: the schedule starts again when the deck stops. */
            sched_us = now;
            sched_ticks = 0;
        } else if (now < due) {
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
#endif

        /* Between two fields: the one place the guest parks (§6).
         * Guest time does not pass, so the schedule and the measuring
         * windows start again from now. After a hold, the menu or a
         * pause the keys start again from none held: theirs were not the
         * guest's. A tape request goes first. */
        while (why != PARK_NONE || coco_disk_request(m) || tapeio_wanted(m)) {
            /* A disk's track goes first: the controller waits on it,
             * busy, as a drive does while its head settles (dskio.h). */
            bool disk = coco_disk_request(m) != NULL;
            bool tape = !disk && tapeio_wanted(m);
            uint32_t w = disk ? PARK_DISK : tape ? PARK_TAPE : why;
            uint32_t parked = park(w, page, alt);
            if (w == PARK_DISK) {
                disk_parks++;
                disk_parked_us += parked;
                if (parked > disk_park_max) disk_park_max = parked;
            }
            if (w != PARK_TAPE && w != PARK_DISK) {
                keymatrix_init(k);
                why = PARK_NONE;
            }
            if (apply_ui(m)) ok_seen = false;
            /* The menu, or a tape or cartridge a layout names, may have
             * chosen another layout (keymapio.h). */
            keymatrix_set_layout(k, g_ui.layout);
            g_c0.tape_unsaved = tapeio_unsaved(m);
            window_start(&hb, m, late);
            window_start(&sec, m, late);
#if PICO_COCO_AUDIO
            audio_stats(&au_last, true);
            hb_edges = m->sound_edges;
#else
            sched_us = time_us_64() + 1000u;
            sched_ticks = 0;
#endif
            /* A disk's park is every track: the heartbeat counts them. */
            if (w != PARK_DISK)
                log_printf("  park         : %s%lu us parked\n", w == PARK_TAPE ? "tape, " : "",
                           (unsigned long)parked);
        }

        uint32_t t_busy = time_us_32();

        /* Keys first, so the matrix the guest scans this field is the
         * one the events describe (§11.1). */
        uint32_t uart_park = uart_keys(k, m);
        uint8_t state, code;
        while (kbd_pop(&state, &code)) keymatrix_event(k, state, code);
        keymatrix_field(k, m);
        page = k->menu_page;
        alt = k->alt;
        why = requests(k, m);
        if (uart_park != PARK_NONE) {
            why = uart_park;
            page = 0;
            alt = false;
        }

        /* The SmartWatch and the board's time (rtc.h). */
        m->clock.fitted = g_ui.smartwatch;
        smartwatch_set_host(&m->clock, g_clock.secs);

        uint32_t t_run = time_us_32();
        uint32_t ran = coco_run_field(m);
        uint32_t run_us = time_us_32() - t_run;
        g_c0.tape_unsaved = tapeio_unsaved(m);
        g_c0.disk = coco_disk_active(m);
#if PICO_COCO_AUDIO
        (void)ran;
#else
        sched_ticks += ran;
        if (sched_ticks >= COCO_TICK_HZ) {
            sched_ticks -= COCO_TICK_HZ;
            sched_us += 1000000u;
        }
#endif

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
#if PICO_COCO_AUDIO
        size_t n = coco_audio_drain(m, pcm, COCO_AUDIO_BUF_LEN);
#endif
        uint32_t busy = time_us_32() - t_busy;
        bool turbo = turbo_now(m);
        if (turbo) turbo_fields++;
        bool deck = coco_cassette_running(m);
        g_c0.deck = !deck ? STATUS_DECK_IDLE
                  : m->cas.rec_wrote ? STATUS_DECK_RECORDING
                  : m->cas.playing ? STATUS_DECK_PLAYING : STATUS_DECK_IDLE;
        if (deck && !deck_was) {
            deck_us = time_us_64();
            deck_ticks = coco_ticks(m);
            deck_busy = 0;
        } else if (deck) {
            deck_busy += busy;
        } else if (deck_was) {
            uint64_t wall = time_us_64() - deck_us;
            uint64_t gus = ticks_us(coco_ticks(m) - deck_ticks);
            uint32_t x100 = (uint32_t)(gus * 100u / (wall + 1u));
            uint32_t b1000 = (uint32_t)(deck_busy * 1000u / (wall + 1u));
            log_printf("  deck         : ran %llu us of guest in %llu us, %lu.%02lux real time%s, "
                       "core 0 busy %lu.%lu%%\n", (unsigned long long)gus,
                       (unsigned long long)wall, (unsigned long)(x100 / 100u),
                       (unsigned long)(x100 % 100u), PICO_COCO_TURBO ? ", turbo" : ", paced",
                       (unsigned long)(b1000 / 10u), (unsigned long)(b1000 % 10u));
        }
        deck_was = deck;
#if PICO_COCO_AUDIO
        if (turbo) {
            /* Never block: the guest's samples are dropped, and the queue
             * kept at its start depth with silence, so it never runs dry
             * (EL §9.3). */
            static const int16_t silence[256];
            size_t room = audio_room();
            const size_t keep = COCO_PCM_QUEUE_LEN - COCO_PCM_QUEUE_START;
            while (room > keep) {
                size_t kk = room - keep < 256u ? room - keep : 256u;
                audio_push(silence, kk);
                room -= kk;
            }
        } else {
            /* Blocks while the queue is full: this is the throttle, on
             * the PWM wrap, which shares clk_sys with nothing that drifts
             * (EL §6.3). The conversion to compare words inside is not
             * counted as busy; it is ~611 short loops a field. */
            audio_push(pcm, n);
        }
#else
        (void)turbo;
#endif
#if PICO_COCO_FORCE_LATE
        /* A scratch build: core 0's interrupts held off for 9 ms, two
         * DMA halves and more, every 250 fields, so the refill is late
         * (EL §6.4). */
        if (m->fields % 250u == 0u) {
            uint32_t irq = save_and_disable_interrupts();
            busy_wait_us_32(9000u);
            restore_interrupts(irq);
        }
#endif
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
#if PICO_COCO_AUDIO
            audio_stats_t sec_au;
            audio_stats(&sec_au, false);
            g_c0.underruns = sec_au.underrun_samples;
            g_c0.late_refills = sec_au.late_refills;
#endif
            __dmb();
            g_c0.seconds++;
            swd_update(m, k, late, slips);
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
                       "stacks %lu/%lu %lu/%lu, battery %s, card %s (%lu changes), parks %lu\n",
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
                       card_present() ? "in" : "out", (unsigned long)card_changes(),
                       (unsigned long)g_park_stats.parks);
            log_printf("  perf         : tier %u, %lu MHz, core 0 busy %s%%, guest %s%% of wall, "
                       "headroom %lu.%02lux, %s host cycles/insn, %s cycles/insn, "
                       "%llu insns, %lu fields, snapshot %s%%\n",
                       (unsigned)PICO_COCO_RAM_TIER, (unsigned long)clk_mhz, busy_s, guest_s,
                       (unsigned long)(head100 / 100u), (unsigned long)(head100 % 100u),
                       hpi_s, cpi_s, (unsigned long long)insns, (unsigned long)fields, take_s);
            /* The deck's signal (§12.3): rt above is turbo's speed while
             * it runs. */
            const cassette_t *cas = &m->cas;
            log_printf("  tape         : fast %s, relay %s, %s, head %lu of %lu, turbo %lu fields, "
                       "%lu edges played, %lu blocks recorded (%lu errors), %lu requests, "
                       "split changes dropped %lu\n",
                       cas->fast ? "on" : "off", cas->motor ? "closed" : "open",
                       cas->playing ? "playing" : cas->ended ? "at its end" : "stopped",
                       (unsigned long)m->tape.pos, (unsigned long)m->tape.len,
                       (unsigned long)turbo_fields, (unsigned long)cas->edges,
                       (unsigned long)cas->rec_blocks, (unsigned long)cas->rec_errors,
                       (unsigned long)m->tape.requests, (unsigned long)m->split_dropped);
            if (m->cfg.rsdos) {
                const wd1793_t *f = &m->dk.fdc;
                log_printf("  disk         : %lu sectors read, %lu written, %lu tracks formatted; "
                           "%lu tracks fetched, %lu put back, %lu lost; %lu parks, %llu us, "
                           "longest %lu us\n",
                           (unsigned long)f->sectors_read, (unsigned long)f->sectors_written,
                           (unsigned long)f->tracks_written, (unsigned long)dskio_tracks(false),
                           (unsigned long)dskio_tracks(true), (unsigned long)dskio_lost(),
                           (unsigned long)disk_parks, (unsigned long long)disk_parked_us,
                           (unsigned long)disk_park_max);
            }
#if PICO_COCO_AUDIO
            log_audio(m, &au_last, wall, m->sound_edges - hb_edges);
            audio_stats(&au_last, false);
            hb_edges = m->sound_edges;
#endif
            window_start(&hb, m, late);
        }
    }
}
