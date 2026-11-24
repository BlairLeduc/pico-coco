/* handoff.h — what the two cores share (design.md §6).
 *
 * The snapshot pool, with every transition under one SIO spinlock
 * (snappool.h says why the lock is the port's), the boot's ROMs, and the
 * counters each core keeps for the other's heartbeat and perf line. Each
 * counter is a 32-bit word with one writer, so a torn read is not
 * possible.
 *
 * Adapted from pico-oric's.
 */
#ifndef PICO_COCO_HANDOFF_H
#define PICO_COCO_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "card.h"
#include "config.h"
#include "settings.h"
#include "snappool.h"

extern snappool_t g_pool;

void handoff_init(void);

/* snappool.h's transitions, each under the lock. */
int  pool_claim(void);
void pool_publish(int i);
int  pool_take(void);
void pool_release(int i);

/* Core 1's, read by core 0. */
typedef struct {
    bool     ready;          /* bring-up finished, the fields below valid */
    uint32_t i2c_hz, spi_hz;
    int32_t  sb_version;     /* SB_REG_VER's byte, -1 if unread           */
    uint32_t presents;
    uint32_t full_presents;
    uint32_t last_us;        /* the last present, wall time               */
    uint32_t max_us;         /* the longest since boot                    */
    uint32_t polls;
    uint32_t key_events;
    uint32_t poll_max_us;    /* the longest keyboard poll since boot      */
    int32_t  battery;        /* SB_REG_BAT's byte, -1 until read          */
    int32_t  temp_c;         /* the die, INT32_MIN until read             */
    uint32_t card_jobs;      /* the ROM job, at boot and on insertion     */
} core1_stats_t;

extern volatile core1_stats_t g_c1;

/* What core 1 measured during bring-up (design.md §17 M6, M7), written
 * before g_c1.ready and only read after it, so it needs no lock. Wall
 * times, since boot where it says so. */
typedef struct {
    uint32_t i2c_us;         /* one register read, SB_REG_VER             */
    uint32_t lcd_us;         /* lcd_init: the panel's reset and sleep-out */
    uint32_t card_us;        /* the ROM job: mount, list, hash, load      */
    uint32_t start_us;       /* core 1 started, since boot                */
} bringup_t;

extern bringup_t g_bringup;

/* The boot's ROMs (design.md §12.1). Core 1 reads the card and leaves
 * what it loaded here before g_c1.ready.
 *
 * While the missing-ROM page is up the guest has not started, so a card
 * that goes in or out is read again (core1.c), and job and images change
 * under core 0. Two flags keep them apart, each with a barrier between
 * its store and the other's load: core 1 sets `busy`, then runs a job
 * only if `claimed` is clear; core 0 sets `claimed`, then waits for
 * `busy` to clear, and from then on job and images are its own.
 * `generation` counts the jobs, so core 0 knows to look again. */
typedef struct {
    card_job_t job;          /* what the card had, and what was loaded    */
    uint8_t    basic[COCO_ROM_HALF];   /* job.basic's bytes, for $A000    */
    uint8_t    basic11[COCO_ROM_HALF]; /* Color BASIC 1.1's, for 4 KB     */
    uint8_t    ext[COCO_ROM_HALF];     /* Extended's, for $8000           */
    /* The settings file at boot (settings.h), and the cartridge it or
     * the Machine page names, read off the card: cart_len 0 for none. */
    settings_t settings;
    uint8_t    cart[COCO_CART_SIZE];
    uint32_t   cart_len;
    char       cart_path[COCO_PATH_MAX];
    uint32_t   ready_us;     /* core 1's bring-up done, since boot        */
    volatile bool     busy;        /* core 1: a job is running          */
    volatile bool     claimed;     /* core 0: job and images are mine   */
    volatile uint32_t generation;  /* jobs finished                     */
} boot_report_t;

extern boot_report_t g_boot;

/* Core 0's per-second window for the perf line (design.md §7.5, §9.6),
 * in thousandths and hundredths so that core 1 formats without floats. */
typedef struct {
    uint32_t seconds;        /* windows closed; 0 until the first        */
    uint32_t busy1000;       /* core 0 outside the pacing wait, of wall  */
    uint32_t head100;        /* times real time it would run unpaced     */
    uint32_t hz;             /* fields a second, rounded                 */
    uint32_t underruns;      /* underrun samples since boot (§10)        */
    uint32_t late_refills;   /* late DMA refills since boot              */
    uint32_t tape_unsaved;   /* the deck holds a recording not on the card */
    uint32_t deck;           /* the signal: a STATUS_DECK_ state (status.h) */
    int32_t  disk;           /* the drive whose head is loaded, -1 none (rsdos.h) */
    uint32_t resets;         /* RESETs and power-ons since boot: core 1
                              * chooses a random artefact phase on each (§9.5) */
} core0_perf_t;

extern volatile core0_perf_t g_c0;

/* The counters a build without the UART can give (design.md §15.5, §17
 * M11): core 0 copies them here once a second, and
 * tools/swd-counters.py reads the block over SWD with both cores
 * running, since halting core 0 would starve the audio it measures.
 * They are the heartbeat's that soak-check.py holds the UART's soak to.
 * Every word is cumulative from boot, with one writer. The layout is
 * the script's too: change both, and SWD_LAYOUT with them. From
 * pico-oric's, with the sound bus's changes for the AY's counts and the
 * CoCo's text screen. */
#define SWD_MAGIC  0x434F4331u   /* "COC1" as a word */
#define SWD_LAYOUT 1u
typedef struct {
    uint32_t magic, layout;
    uint32_t updates;        /* one a second; the block is current      */
    uint32_t uptime_ms;      /* the board's clock at the update         */
    uint32_t fields;         /* guest fields run                        */
    uint32_t cycles;         /* guest E cycles, low word: rt is these over time */
    uint32_t late, slips;    /* the timer's pacing; 0 on audio          */
    uint32_t presents, snapshots_dropped;
    uint32_t key_events, keys_lost, polls, i2c_errors;
    uint32_t undoc, log_dropped;
    uint32_t underrun_samples, late_refills, consumed, pcm_overflow;
    uint32_t sound_edges;    /* changes of the sound bus (coco.h)       */
    uint32_t split_fields;   /* §9.4                                    */
    uint32_t card_changes, parks;
    uint32_t busy1000;       /* core 0 outside the pacing wait, last second */
    int32_t  battery;        /* SB_REG_BAT's byte, -1 until read        */
    int32_t  temp_c;         /* the die, INT32_MIN until read           */
    uint32_t screen;         /* the guest's text screen, 512 VDG codes, an address */
} swd_counters_t;

extern volatile swd_counters_t g_swd;

/* The machine as it powers on (design.md §14's Machine page): what
 * core0_power_on builds, with the ROMs and the cartridge from g_boot. */
typedef struct {
    unsigned ram_k;            /* 4, 16, 32 or 64                       */
    rom_id_t basic;            /* the Color BASIC at $A000 (roms.h)     */
    bool     extended;         /* Extended Color BASIC at $8000         */
    bool     cart;             /* g_boot.cart at $C000                  */
    bool     rsdos;            /* it is Disk BASIC, the RS-DOS cartridge's
                                  ROM (rsdos.h), and the controller is in */
} machine_t;

/* The running machine's, written by core 0 at each power-on and read by
 * core 1 while the guest is parked. */
extern machine_t g_machine;

/* What the menu changes (design.md §9.6, §14), applied by the core that
 * owns it when the guest is handed back (EL §2.5). */
struct keylayout_s;
typedef struct {
    volatile unsigned volume;      /* 0-8, as settings_t has it           */
    volatile bool     perf_line;   /* the top line (status.h)             */
    volatile bool     status;      /* the bottom line                     */
    volatile unsigned backlight;   /* 1-15 as the Setup page has it; 0 unread */
    /* The game layout in force, NULL for the standard map (keymapio.h),
     * and fast loading (cassette.h): core 0 takes both after every park. */
    const struct keylayout_s *volatile layout;
    volatile bool     fast_tape;
    /* The MC6847T1 drawn (display.h): core 1's alone. */
    volatile bool     vdg_t1;
    /* The artefacts (§9.5): SETTINGS_ARTEFACTS_* and SETTINGS_PHASE_*,
     * and the phase drawn now, red's or blue's, which "random" chooses
     * at each of core 0's resets. Core 1's alone. */
    volatile uint8_t  artefacts;
    volatile uint8_t  artefact_phase;
    volatile bool     artefact_red;
    /* Text on dark green or orange, or black (§9.5). Core 1's alone. */
    volatile bool     dark_bg;
    /* The drives' steps and motor heard (drivesnd.h): core 0 takes it
     * each field. */
    volatile bool     disk_sounds;
    volatile bool     reset;       /* the menu's Reset: core 0 clears it  */
    volatile bool     power;       /* its Power off/on: core 0 clears it  */
    /* The Machine page's Apply: power on as restart_cfg, with the ROMs
     * and the cartridge core 1 has left in g_boot. Core 0 clears it. */
    volatile bool     restart;
    machine_t         restart_cfg;
} ui_t;

extern ui_t g_ui;

/* The board, identified by main() before core 1 starts. */
extern board_info_t g_board;

#endif /* PICO_COCO_HANDOFF_H */
