/* bench_main.c — M2's firmware: the 6809 alone on the board, timed
 * (design.md §7.5, §17), adapted from pico-oric's.
 *
 * No LCD, keyboard or audio, and one core. Each workload in src/bench/ is
 * loaded afresh and run for BENCH_RUN_CYCLES, ten seconds of an NTSC
 * CoCo, with nothing printed while the clock runs. Each run reports host
 * cycles per guest instruction, mean guest cycles per instruction, and
 * the share of core 0 the guest at its E clock would take at that rate,
 * against the gate's 60 % (design.md §7.5). The cycle and instruction
 * counts must equal test_bench's on the host, which bench.h records: the
 * same code ran.
 *
 * Host cycles are microseconds times clk_sys, which is set to 150 MHz
 * (design.md §4, D3) and reported in the banner.
 */
#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "bench.h"
#include "board.h"
#include "pico_coco_version.h"

static board_info_t g_board;
static bench_t      g_bench;

static void run(const char *name, unsigned pass, uint32_t host_cycles,
                uint32_t host_insns) {
    uint64_t t0 = time_us_64();
    uint64_t c = bench_run(&g_bench, BENCH_RUN_CYCLES);
    uint64_t us = time_us_64() - t0;

    uint64_t insns = g_bench.m.cpu.instructions;
    double host = (double)us * clock_get_hz(clk_sys) / 1e6;
    double guest_s = (double)c / BENCH_GUEST_HZ;
    bool same = c == host_cycles && insns == host_insns &&
                g_bench.m.cpu.undoc_count == 0;
    printf("bench %-5s pass %u: %llu cyc %llu insn %llu us | %.1f host cyc/insn "
           "%.3f cyc/insn | core 0 %.1f %% at %.4f MHz | %s host\n",
           name, pass, (unsigned long long)c, (unsigned long long)insns,
           (unsigned long long)us, host / (double)insns, (double)c / (double)insns,
           100.0 * ((double)us / 1e6) / guest_s, BENCH_GUEST_HZ / 1e6,
           same ? "=" : "NOT =");
}

int main(void) {
    bool clocks_ok = board_init_clocks();
    stdio_init_all();
    board_temp_init();
    board_identify(&g_board);

    board_log_banner(&g_board);
    printf("  firmware     : %s (M2 bench)\n", PICO_COCO_VERSION);
    printf("  SRAM tier    : %d\n", PICO_COCO_RAM_TIER);
    printf("  indexed      : %s\n", PICO_COCO_IDX_OUTLINE ? "out of line" : "inlined");
    if (!clocks_ok)
        printf("  WARNING: clk_sys is not at 150 MHz; the numbers below are not M2's\n");

    /* One core and no deadline: a blocking printf between runs costs
     * nothing that is timed (hardware-notes.md §2.7). */
    for (unsigned pass = 1;; pass++) {
        bench_basic_load(&g_bench);
        run("basic", pass, BENCH_BASIC_RUN_CYCLES, BENCH_BASIC_RUN_INSNS);
        bench_mix_load(&g_bench);
        run("mix", pass, BENCH_MIX_RUN_CYCLES, BENCH_MIX_RUN_INSNS);
        printf("bench die %d C\n", board_temp_c());
    }
}
