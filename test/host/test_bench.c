/* test_bench.c — M2's workloads on the host (design.md §7.5, §17).
 *
 * The bench firmware runs these programs on the board and compares its
 * cycle and instruction counts with bench.h's, which this test measured
 * and holds: equal counts are the evidence that the same code ran, so a
 * board's timing belongs to it. Here, besides the counts:
 *
 *   basic  computes what it should: each statement's sum (45, 45, 65) as
 *          a packed float, and the passes counted;
 *   mix    runs no undocumented opcode or postbyte, and two runs from a
 *          fresh load end identical, registers and memory.
 */

#include <math.h>
#include <string.h>

#include "bench.h"
#include "test_util.h"

static bench_t g_a, g_b;

/* A packed five-byte float: exponent + $80, then the mantissa with its
 * top bit standing for the sign. */
static double unpack(const uint8_t *f) {
    if (f[0] == 0) return 0.0;
    uint32_t mant = ((uint32_t)(f[1] | 0x80u) << 24) | ((uint32_t)f[2] << 16) |
                    ((uint32_t)f[3] << 8) | f[4];
    double v = ldexp((double)mant, (int)f[0] - 0x80 - 32);
    return (f[1] & 0x80u) ? -v : v;
}

int main(void) {
    /* ---- basic ------------------------------------------------------ */
    bench_basic_load(&g_a);
    uint64_t c = bench_run(&g_a, BENCH_RUN_CYCLES);
    uint64_t insns = g_a.m.cpu.instructions;
    printf("basic: %llu cycles, %llu instructions, %.3f cycles each\n",
           (unsigned long long)c, (unsigned long long)insns, (double)c / (double)insns);

    CHECK(c >= BENCH_RUN_CYCLES && c < BENCH_RUN_CYCLES + 20u,
          "a run of %u ended at %llu: more than one instruction over",
          BENCH_RUN_CYCLES, (unsigned long long)c);
    CHECK(c == g_a.m.cpu.cycles, "bench_run returned %llu, the CPU counted %llu",
          (unsigned long long)c, (unsigned long long)g_a.m.cpu.cycles);
    CHECK(g_a.m.cpu.undoc_count == 0, "basic ran %u undocumented opcodes, the last $%04X at $%04X",
          g_a.m.cpu.undoc_count, g_a.m.cpu.undoc_op, g_a.m.cpu.undoc_pc);

    static const double sums[BENCH_BASIC_STMTS] = { 45.0, 45.0, 65.0 };
    for (unsigned i = 0; i < BENCH_BASIC_STMTS; i++) {
        double v = unpack(&g_a.ram[BENCH_BASIC_RESULT + 5u * i]);
        CHECK(v == sums[i], "statement %u: %g, not %g", i + 1, v, sums[i]);
    }
    unsigned passes = (unsigned)(g_a.ram[BENCH_BASIC_PASSES] << 8 | g_a.ram[BENCH_BASIC_PASSES + 1]);
    printf("basic: %u passes of the line\n", passes);
    CHECK(passes > 100, "only %u passes of the line in ten seconds", passes);

    CHECK(c == BENCH_BASIC_RUN_CYCLES && insns == BENCH_BASIC_RUN_INSNS,
          "basic counted %llu cycles and %llu instructions; bench.h says %u and %u. "
          "If the program changed on purpose, update bench.h",
          (unsigned long long)c, (unsigned long long)insns,
          BENCH_BASIC_RUN_CYCLES, BENCH_BASIC_RUN_INSNS);

    /* ---- mix -------------------------------------------------------- */
    bench_mix_load(&g_a);
    c = bench_run(&g_a, BENCH_RUN_CYCLES);
    insns = g_a.m.cpu.instructions;
    printf("mix:   %llu cycles, %llu instructions, %.3f cycles each\n",
           (unsigned long long)c, (unsigned long long)insns, (double)c / (double)insns);

    CHECK(c >= BENCH_RUN_CYCLES && c < BENCH_RUN_CYCLES + 25u,
          "a run of %u ended at %llu: more than one instruction over",
          BENCH_RUN_CYCLES, (unsigned long long)c);
    CHECK(g_a.m.cpu.undoc_count == 0, "mix ran %u undocumented opcodes, the last $%04X at $%04X",
          g_a.m.cpu.undoc_count, g_a.m.cpu.undoc_op, g_a.m.cpu.undoc_pc);
    /* The block loops: more instructions ran than it holds. */
    CHECK(insns > 2u * BENCH_MIX_INSNS, "the mix ran %llu instructions, not round its loop",
          (unsigned long long)insns);

    bench_mix_load(&g_b);
    (void)bench_run(&g_b, BENCH_RUN_CYCLES);
    CHECK(memcmp(&g_a.m.cpu, &g_b.m.cpu, sizeof(g_a.m.cpu)) == 0,
          "two runs of the mix ended in different CPU states");
    CHECK(memcmp(g_a.ram, g_b.ram, sizeof(g_a.ram)) == 0,
          "two runs of the mix ended with different memory");

    CHECK(c == BENCH_MIX_RUN_CYCLES && insns == BENCH_MIX_RUN_INSNS,
          "mix counted %llu cycles and %llu instructions; bench.h says %u and %u. "
          "If the generator changed on purpose, update bench.h",
          (unsigned long long)c, (unsigned long long)insns,
          BENCH_MIX_RUN_CYCLES, BENCH_MIX_RUN_INSNS);

    TEST_DONE();
}
