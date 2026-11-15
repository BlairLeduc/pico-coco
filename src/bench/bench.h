/* bench.h — M2's workloads: the 6809 alone on a flat bus (design.md §7.5,
 * §17), adapted from pico-oric's bench.
 *
 * Portable, like src/core/: no SDK header and no allocation. The host test
 * (test/host/test_bench.c) and the bench firmware (src/port/bench_main.c)
 * build and run the same programs, so the cycle and instruction counts the
 * board reports are the ones the host checked.
 *
 *   basic  Color BASIC's character fetch, in the direct page where the ROM
 *          copies it, walking a line of sums, and a floating-point add on
 *          unpacked accumulators: a stand-in for the ROM's interpreter
 *          until M3 runs the real one. Written to its shape, not copied.
 *   mix    a long straight line of random documented instructions, every
 *          addressing mode among them and indexed postbytes drawn from
 *          all the documented ones, the oracle's mix: a control that is
 *          not BASIC-shaped. Its code
 *          pages send writes to the slow path, which drops them, so a
 *          random store cannot rewrite it.
 *
 * The CPU's own run loop is what is timed (mc6809_run), in slices of one
 * NTSC field, the shape the machine's loop will have (design.md §13.1).
 * Only the bus is flat: the bench supplies the slow path (coco.h).
 */
#ifndef PICO_COCO_BENCH_H
#define PICO_COCO_BENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coco.h"

/* One NTSC field: 262 lines of 57 cycles (design.md §3.4, §18: medium).
 * The slice each mc6809_run call is given. */
#define BENCH_SLICE_CYCLES   (262u * 57u)

/* One timed run: 600 fields, ten seconds of an NTSC CoCo. */
#define BENCH_RUN_CYCLES     (600u * BENCH_SLICE_CYCLES)

/* The E clock the share of core 0 is projected at: the NTSC master
 * oscillator over 16, the SAM's rate 0 (design.md §8.2, §13.2). */
#define BENCH_GUEST_HZ       (14318180.0 / 16.0)

/* The basic program's results. Each statement of its line stores its sum
 * as a packed five-byte float at BENCH_BASIC_RESULT + 5n; each pass of the
 * line adds one to the 16-bit count at BENCH_BASIC_PASSES. */
#define BENCH_BASIC_RESULT   0x0900u
#define BENCH_BASIC_PASSES   0x0910u
#define BENCH_BASIC_STMTS    3u

/* The mix: this many random instructions, then a jump back. */
#define BENCH_MIX_INSNS      4000u

/* What one timed run (BENCH_RUN_CYCLES) of each workload counts, as
 * test_bench measured them on the host. The firmware compares its own
 * counts with these, and they must be equal: the same code ran. */
#define BENCH_BASIC_RUN_CYCLES   8960403u
#define BENCH_BASIC_RUN_INSNS    2118076u
#define BENCH_MIX_RUN_CYCLES     8960403u
#define BENCH_MIX_RUN_INSNS      1760687u

/* The machine's own RAM serves as the flat bus's (coco.h). */
typedef struct {
    coco_t  m;
} bench_t;

/* Clear the machine, assemble the basic program, and point the CPU at it.
 * Every page takes the fast path. */
void bench_basic_load(bench_t *b);

/* Clear the machine, generate the mix from its fixed seed, and point the
 * CPU at it. */
void bench_mix_load(bench_t *b);

/* Run in BENCH_SLICE_CYCLES slices, each slice's overshoot carried as debt
 * into the next, until at least `cycles` have passed; return the cycles
 * run. */
uint64_t bench_run(bench_t *b, uint64_t cycles);

#endif /* PICO_COCO_BENCH_H */
