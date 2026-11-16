/* handoff.h — what the two cores share (design.md §6).
 *
 * The snapshot pool, with every transition under one SIO spinlock
 * (snappool.h says why the lock is the port's), the boot's ROMs, and the
 * counters each core keeps for the other's heartbeat and perf line. Each
 * counter is a 32-bit word with one writer, so a torn read is not
 * possible.
 *
 * Adapted from pico-oric's M7. The settings and the menu's state are
 * M9's.
 */
#ifndef PICO_COCO_HANDOFF_H
#define PICO_COCO_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "card.h"
#include "config.h"
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
    uint8_t    ext[COCO_ROM_HALF];     /* Extended's, for $8000           */
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
} core0_perf_t;

extern volatile core0_perf_t g_c0;

/* What the menu will change (design.md §9.6, §14), until M9 brings the
 * menu and the settings file. Core 1 reads it. */
typedef struct {
    volatile bool perf_line;   /* the top line (status.h)             */
} ui_t;

extern ui_t g_ui;

/* The board, identified by main() before core 1 starts. */
extern board_info_t g_board;

#endif /* PICO_COCO_HANDOFF_H */
