/* test_mc6809_behaviour.c — what an instruction-by-instruction comparison
 * cannot see (design.md §7.4): the run contract, NMI's arming and edge,
 * FIRQ against IRQ, E and RTI, SYNC and CWAI with each line masked and
 * not, HALT, the trap, a read-modify-write's read, and the skip held to a
 * stepping machine register for register (EL §3.4). Each rule stands
 * beside a control that must fail.
 *
 * The machine is 64 KB of RAM on the page table's fast path, except page
 * $FF00-$FFEF's neighbour $FE00, left unmapped so its accesses are seen.
 */

#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "coco.h"
#include "mc6809.h"
#include "test_util.h"

static uint8_t ram[65536];

#define IO_PAGE 0xFEu
#define MAX_IO  16
typedef struct { bool write; uint16_t a; uint8_t v; } io_t;
static io_t io_log[MAX_IO];
static int io_n;

uint8_t bus_read_slow(coco_t *m, uint16_t a) {
    (void)m;
    if (io_n < MAX_IO) io_log[io_n++] = (io_t){ false, a, ram[a] };
    return ram[a];
}

void bus_write_slow(coco_t *m, uint16_t a, uint8_t v) {
    (void)m;
    if (io_n < MAX_IO) io_log[io_n++] = (io_t){ true, a, v };
    ram[a] = v;
}

static void map(coco_t *m) {
    for (unsigned p = 0; p < COCO_PAGE_COUNT; p++) {
        bool io = p == IO_PAGE;
        m->page[p].read = io ? NULL : &ram[p * COCO_PAGE_SIZE];
        m->page[p].write = io ? NULL : &ram[p * COCO_PAGE_SIZE];
    }
}

/* A machine at power-on with RESET taken, S loaded at $7F00, and vectors
 * to handlers at $E000 (IRQ), $E100 (FIRQ), $E200 (NMI), $E300 (SWI). */
static void boot(coco_t *m, uint16_t start) {
    memset(m, 0, sizeof *m);
    map(m);
    ram[0xFFFE] = (uint8_t)(start >> 8); ram[0xFFFF] = (uint8_t)start;
    ram[0xFFF8] = 0xE0; ram[0xFFF9] = 0x00;
    ram[0xFFF6] = 0xE1; ram[0xFFF7] = 0x00;
    ram[0xFFFC] = 0xE2; ram[0xFFFD] = 0x00;
    ram[0xFFFA] = 0xE3; ram[0xFFFB] = 0x00;
    mc6809_init(&m->cpu);
    mc6809_reset(m);
}

static void put(uint16_t a, const uint8_t *bytes, size_t n) {
    memcpy(&ram[a], bytes, n);
}

#define PUT(a, ...) do { const uint8_t b_[] = { __VA_ARGS__ }; put((a), b_, sizeof b_); } while (0)

static void step_n(coco_t *m, int n) { while (n--) mc6809_step(m); }

static coco_t ma, mb;

/* ---- the run contract ------------------------------------------------ */

static int test_run_contract(void) {
    memset(ram, 0x12, sizeof ram);                 /* NOPs */
    /* A slow instruction among NOPs: PSHS with every register, 17 cycles,
     * then PULS likewise. */
    PUT(0x1000, 0x10, 0xCE, 0x7F, 0x00,            /* LDS #$7F00 */
                0x34, 0xFF, 0x35, 0xFF);          /* PSHS all; PULS all */
    boot(&ma, 0x1000);

    uint64_t before = ma.cpu.cycles;
    uint32_t sum = 0;
    for (int i = 0; i < 2000; i++) {
        uint32_t ask = 1u + (uint32_t)(i % 23);
        uint32_t got = mc6809_run(&ma, ask);
        CHECK(got >= ask, "run(%u) returned %u, less than asked", ask, got);
        CHECK(got < ask + 20u, "run(%u) returned %u: more than one instruction over", ask, got);
        sum += got;
    }
    CHECK(ma.cpu.cycles - before == sum, "the returns sum to %u, the CPU counted %llu",
          sum, (unsigned long long)(ma.cpu.cycles - before));

    /* Control: run(1) is not "exactly 1". LDS #imm takes 4. */
    boot(&ma, 0x1000);
    CHECK(mc6809_run(&ma, 1) == 4u, "run(1) over LDS # did not return its 4 cycles");
    CHECK(mc6809_run(&ma, 0) == 0u, "run(0) ran something");
    return 0;
}

/* ---- reset and NMI --------------------------------------------------- */

static int test_reset_and_nmi(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x12, 0x12, 0x10, 0xCE, 0x7F, 0x00, 0x12);   /* NOP NOP LDS # NOP */
    boot(&ma, 0x1000);
    ma.cpu.dp = 0x55;
    ma.cpu.cc = 0;
    mc6809_reset(&ma);
    CHECK(ma.cpu.pc == 0x1000 && ma.cpu.dp == 0 &&
          (ma.cpu.cc & (MC6809_CC_F | MC6809_CC_I)) == (MC6809_CC_F | MC6809_CC_I),
          "reset left PC=%04X DP=%02X CC=%02X", ma.cpu.pc, ma.cpu.dp, ma.cpu.cc);

    /* An NMI edge before S is loaded waits... */
    mc6809_set_nmi(&ma.cpu, true);
    step_n(&ma, 2);
    CHECK(ma.cpu.pc == 0x1002, "NMI taken before S was loaded (PC=%04X)", ma.cpu.pc);
    /* ...and is taken once LDS has run. */
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0x1006 && ma.cpu.nmi_armed, "LDS did not arm NMI");
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0xE200, "NMI not taken after LDS (PC=%04X)", ma.cpu.pc);
    CHECK(ma.cpu.s == 0x7F00 - 12u, "NMI stacked %u bytes, not 12", 0x7F00u - ma.cpu.s);

    /* The level held high does not retake it (RTI from the handler)... */
    PUT(0xE200, 0x3B);                             /* RTI */
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0x1006, "RTI from NMI went to %04X", ma.cpu.pc);
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0x1007, "a held NMI line was taken again (PC=%04X)", ma.cpu.pc);
    /* ...a second edge is. Control for the one above. */
    mc6809_set_nmi(&ma.cpu, false);
    mc6809_set_nmi(&ma.cpu, true);
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0xE200, "a second NMI edge was not taken (PC=%04X)", ma.cpu.pc);
    return 0;
}

/* ---- FIRQ, IRQ, E and RTI -------------------------------------------- */

static int test_firq_irq(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x10, 0xCE, 0x7F, 0x00,            /* LDS #$7F00     */
                0x86, 0x11,                       /* LDA #$11       */
                0x1C, 0xAF,                       /* ANDCC #$AF: F, I clear */
                0x12, 0x12, 0x12);
    PUT(0xE000, 0x86, 0x22, 0x3B);                /* IRQ: LDA #$22; RTI */
    PUT(0xE100, 0x86, 0x33, 0x3B);                /* FIRQ: LDA #$33; RTI */
    boot(&ma, 0x1000);
    step_n(&ma, 3);
    CHECK(ma.cpu.pc == 0x1008, "setup ran to %04X", ma.cpu.pc);

    /* Both asserted: FIRQ first, 3 bytes, E clear, F and I set. */
    mc6809_set_irq(&ma.cpu, 1, true);
    mc6809_set_firq(&ma.cpu, 1, true);
    uint32_t n = mc6809_step(&ma);
    CHECK(ma.cpu.pc == 0xE100, "FIRQ did not win over IRQ (PC=%04X)", ma.cpu.pc);
    CHECK(n == 10u, "FIRQ entry took %u cycles, not 10", n);
    CHECK(ma.cpu.s == 0x7F00 - 3u, "FIRQ stacked %u bytes, not 3", 0x7F00u - ma.cpu.s);
    CHECK(!(ram[ma.cpu.s] & MC6809_CC_E), "FIRQ stacked CC with E set");
    CHECK((ma.cpu.cc & (MC6809_CC_F | MC6809_CC_I)) == (MC6809_CC_F | MC6809_CC_I),
          "FIRQ left CC=%02X", ma.cpu.cc);

    /* RTI with E clear pulls PC and CC only: A keeps the handler's $33. */
    mc6809_set_firq(&ma.cpu, 1, false);
    step_n(&ma, 2);
    CHECK(ma.cpu.pc == 0x1008 && ma.cpu.a == 0x33 && ma.cpu.s == 0x7F00,
          "RTI after FIRQ: PC=%04X A=%02X S=%04X", ma.cpu.pc, ma.cpu.a, ma.cpu.s);

    /* IRQ, still asserted: 12 bytes, E set; RTI restores A to $33. */
    n = mc6809_step(&ma);
    CHECK(ma.cpu.pc == 0xE000 && n == 19u, "IRQ entry: PC=%04X, %u cycles", ma.cpu.pc, n);
    CHECK(ma.cpu.s == 0x7F00 - 12u && (ram[ma.cpu.s] & MC6809_CC_E),
          "IRQ stacked %u bytes, CC=%02X", 0x7F00u - ma.cpu.s, ram[ma.cpu.s]);
    CHECK((ma.cpu.cc & MC6809_CC_I) && !(ma.cpu.cc & MC6809_CC_F), "IRQ left CC=%02X", ma.cpu.cc);
    mc6809_set_irq(&ma.cpu, 1, false);
    step_n(&ma, 1);
    CHECK(ma.cpu.a == 0x22, "IRQ handler did not run");
    n = mc6809_step(&ma);
    CHECK(n == 15u && ma.cpu.a == 0x33 && ma.cpu.pc == 0x1008,
          "RTI with E set: %u cycles, A=%02X PC=%04X", n, ma.cpu.a, ma.cpu.pc);

    /* Control: the same RTI with E cleared in the stacked CC pulls three
     * bytes and leaves A alone. */
    mc6809_set_irq(&ma.cpu, 1, true);
    step_n(&ma, 1);
    mc6809_set_irq(&ma.cpu, 1, false);
    step_n(&ma, 1);
    ram[ma.cpu.s] &= (uint8_t)~MC6809_CC_E;
    n = mc6809_step(&ma);
    CHECK(n == 6u && ma.cpu.a == 0x22 && ma.cpu.s == 0x7F00 - 12u + 3u,
          "RTI did not follow the stacked E: %u cycles, A=%02X", n, ma.cpu.a);

    /* Masked: I set, IRQ asserted, nothing taken. */
    boot(&ma, 0x1000);
    step_n(&ma, 2);                               /* LDS, LDA; CC still F|I */
    mc6809_set_irq(&ma.cpu, 1, true);
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0x1008, "IRQ taken through I (PC=%04X)", ma.cpu.pc);
    return 0;
}

/* ---- SYNC and CWAI --------------------------------------------------- */

static int test_sync(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x10, 0xCE, 0x7F, 0x00, 0x13, 0x12);   /* LDS; SYNC; NOP */
    PUT(0xE000, 0x3B);

    /* Masked IRQ: SYNC ends and runs on without taking it. */
    boot(&ma, 0x1000);
    step_n(&ma, 2);
    CHECK(ma.cpu.wait == MC6809_SYNC, "SYNC did not wait");
    uint32_t n = mc6809_run(&ma, 1000);
    CHECK(n == 1000u && ma.cpu.pc == 0x1005, "SYNC with no line: %u cycles, PC=%04X", n, ma.cpu.pc);
    /* One poll latches the line, the next sees it and ends the wait. */
    mc6809_set_irq(&ma.cpu, 1, true);
    step_n(&ma, 1);
    CHECK(ma.cpu.wait == MC6809_SYNC, "SYNC ended on the poll that latched the line");
    step_n(&ma, 1);
    CHECK(ma.cpu.wait == MC6809_RUNNING && ma.cpu.pc == 0x1005,
          "masked IRQ: wait=%d PC=%04X", ma.cpu.wait, ma.cpu.pc);
    step_n(&ma, 1);
    CHECK(ma.cpu.pc == 0x1006, "masked IRQ was taken after SYNC (PC=%04X)", ma.cpu.pc);

    /* Unmasked: taken once SYNC ends. */
    boot(&ma, 0x1000);
    ma.cpu.cc = 0;
    step_n(&ma, 2);
    mc6809_set_irq(&ma.cpu, 1, true);
    step_n(&ma, 3);
    CHECK(ma.cpu.pc == 0xE000, "unmasked IRQ not taken after SYNC (PC=%04X)", ma.cpu.pc);

    /* Already asserted when SYNC runs: SYNC takes 4 in all. */
    boot(&ma, 0x1000);
    step_n(&ma, 1);
    mc6809_set_irq(&ma.cpu, 1, true);
    uint64_t t0 = ma.cpu.cycles;
    step_n(&ma, 2);
    CHECK(ma.cpu.cycles - t0 == 4u && ma.cpu.wait == MC6809_RUNNING,
          "SYNC with a line already asserted took %llu", (unsigned long long)(ma.cpu.cycles - t0));

    /* NMI before arming does not end SYNC. Control for the arming rule. */
    PUT(0x2000, 0x13, 0x12);
    boot(&ma, 0x2000);
    step_n(&ma, 1);
    mc6809_set_nmi(&ma.cpu, true);
    mc6809_run(&ma, 100);
    CHECK(ma.cpu.wait == MC6809_SYNC, "an unarmed NMI ended SYNC");
    return 0;
}

static int test_cwai(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x10, 0xCE, 0x7F, 0x00,           /* LDS #$7F00      */
                0x3C, 0xEF,                       /* CWAI #$EF: I clear, F kept */
                0x86, 0x44);                      /* LDA #$44        */
    PUT(0xE000, 0x86, 0x22, 0x3B);                /* IRQ: LDA #$22; RTI */
    PUT(0xE100, 0x3B);
    PUT(0xE200, 0x3B);

    boot(&ma, 0x1000);
    step_n(&ma, 1);
    uint32_t n = mc6809_step(&ma);
    CHECK(n == 18u && ma.cpu.wait == MC6809_CWAI && ma.cpu.s == 0x7F00 - 12u,
          "CWAI: %u cycles, wait=%d, %u bytes stacked", n, ma.cpu.wait, 0x7F00u - ma.cpu.s);
    CHECK(ram[ma.cpu.s] & MC6809_CC_E, "CWAI stacked CC without E");

    /* FIRQ is masked by the F CWAI kept: it waits on. */
    mc6809_set_firq(&ma.cpu, 1, true);
    mc6809_run(&ma, 200);
    CHECK(ma.cpu.wait == MC6809_CWAI, "masked FIRQ ended CWAI");
    mc6809_set_firq(&ma.cpu, 1, false);

    /* IRQ is not: vectored without stacking again; RTI returns after CWAI
     * with everything restored, the 12 bytes pulled. */
    mc6809_set_irq(&ma.cpu, 1, true);
    step_n(&ma, 3);
    CHECK(ma.cpu.pc == 0xE000 && ma.cpu.s == 0x7F00 - 12u,
          "IRQ after CWAI: PC=%04X, %u bytes stacked", ma.cpu.pc, 0x7F00u - ma.cpu.s);
    mc6809_set_irq(&ma.cpu, 1, false);
    step_n(&ma, 2);
    CHECK(ma.cpu.pc == 0x1006 && ma.cpu.s == 0x7F00 && ma.cpu.a == 0x00,
          "RTI after CWAI: PC=%04X S=%04X A=%02X", ma.cpu.pc, ma.cpu.s, ma.cpu.a);

    /* NMI ends any CWAI. */
    boot(&ma, 0x1000);
    PUT(0x1004, 0x3C, 0xFF);                      /* CWAI #$FF: masks kept */
    step_n(&ma, 2);
    mc6809_set_irq(&ma.cpu, 1, true);
    mc6809_run(&ma, 100);
    CHECK(ma.cpu.wait == MC6809_CWAI, "IRQ ended CWAI #$FF");
    /* A poll to latch it, a poll to see it, then the vector. */
    mc6809_set_nmi(&ma.cpu, true);
    step_n(&ma, 3);
    CHECK(ma.cpu.wait == MC6809_RUNNING && ma.cpu.pc == 0xE200 && ma.cpu.s == 0x7F00 - 12u,
          "NMI did not end CWAI #$FF (wait=%d PC=%04X)", ma.cpu.wait, ma.cpu.pc);
    return 0;
}

/* ---- HALT ------------------------------------------------------------ */

static int test_halt(void) {
    memset(ram, 0x12, sizeof ram);
    boot(&ma, 0x1000);
    mc6809_set_halt(&ma.cpu, true);
    uint64_t t0 = ma.cpu.cycles;
    uint32_t n = mc6809_run(&ma, 5000);
    CHECK(n == 5000u && ma.cpu.pc == 0x1000 && ma.cpu.cycles - t0 == 5000u,
          "halted: %u cycles, PC=%04X", n, ma.cpu.pc);
    CHECK(mc6809_step(&ma) == 1u, "a halted step is not one cycle");
    mc6809_set_halt(&ma.cpu, false);
    mc6809_run(&ma, 10);
    CHECK(ma.cpu.pc == 0x1005, "released: PC=%04X", ma.cpu.pc);
    return 0;
}

/* ---- the trap -------------------------------------------------------- */

static int test_trap(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x01,                             /* page 1, undocumented */
                0x10, 0x00,                       /* page 2, undocumented */
                0x11, 0x86,                       /* page 3, undocumented */
                0x86, 0x01,                       /* LDA #1: control       */
                0xA6, 0x87,                       /* LDA A,X with $x7: undocumented postbyte */
                0x1F, 0x81,                       /* TFR A,X: mixed sizes  */
                0x1F, 0x12);                      /* TFR X,Y: control      */
    boot(&ma, 0x1000);
    uint32_t n = mc6809_step(&ma);
    CHECK(ma.cpu.undoc_count == 1 && ma.cpu.undoc_pc == 0x1000 && ma.cpu.undoc_op == 0x01 &&
          ma.cpu.pc == 0x1001 && n == 2u, "page 1 trap: count %u op %04X PC %04X, %u cycles",
          ma.cpu.undoc_count, ma.cpu.undoc_op, ma.cpu.pc, n);
    n = mc6809_step(&ma);
    CHECK(ma.cpu.undoc_count == 2 && ma.cpu.undoc_pc == 0x1001 && ma.cpu.undoc_op == 0x1000 &&
          ma.cpu.pc == 0x1003 && n == 3u, "page 2 trap: op %04X PC %04X, %u cycles",
          ma.cpu.undoc_op, ma.cpu.pc, n);
    step_n(&ma, 1);
    CHECK(ma.cpu.undoc_count == 3 && ma.cpu.undoc_op == 0x1186, "page 3 trap: op %04X", ma.cpu.undoc_op);
    step_n(&ma, 1);
    CHECK(ma.cpu.undoc_count == 3 && ma.cpu.a == 1, "LDA # was counted or failed");
    step_n(&ma, 1);
    CHECK(ma.cpu.undoc_count == 4 && ma.cpu.undoc_pc == 0x1007, "postbyte $87 not counted");
    step_n(&ma, 1);
    CHECK(ma.cpu.undoc_count == 5 && ma.cpu.undoc_op == 0x1F, "TFR A,X not counted");
    step_n(&ma, 1);
    CHECK(ma.cpu.undoc_count == 5, "TFR X,Y counted");
    return 0;
}

/* ---- a read-modify-write reads first --------------------------------- */

static int test_rmw(void) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x7F, IO_PAGE, 0x20,              /* CLR $FE20 */
                0xB7, IO_PAGE, 0x21);             /* STA $FE21: control */
    boot(&ma, 0x1000);
    ram[0xFE20] = 0x5A;
    io_n = 0;
    step_n(&ma, 1);
    CHECK(io_n == 2 && !io_log[0].write && io_log[0].a == 0xFE20 &&
          io_log[1].write && io_log[1].a == 0xFE20 && io_log[1].v == 0,
          "CLR did not read then write $FE20 (%d accesses)", io_n);
    io_n = 0;
    step_n(&ma, 1);
    CHECK(io_n == 1 && io_log[0].write, "STA made %d accesses, not one write", io_n);
    return 0;
}

/* ---- the skip held to a stepping machine ----------------------------- */

/* A program that idles in SYNC with IRQ masked, counting wake-ups, and in
 * CWAI waiting for FIRQ, whose handler counts too; lines are raised and
 * dropped on a schedule. */
static void skip_program(coco_t *m) {
    memset(ram, 0x12, sizeof ram);
    PUT(0x1000, 0x10, 0xCE, 0x7F, 0x00,           /* LDS #$7F00            */
                0x13,                             /* 1004: SYNC            */
                0x7C, 0x20, 0x00,                 /* INC $2000             */
                0x3C, 0xBF,                       /* CWAI #$BF: F clear    */
                0x7C, 0x20, 0x01,                 /* INC $2001             */
                0x20, 0xF3);                      /* BRA $1004             */
    PUT(0xE100, 0x7C, 0x20, 0x02, 0x3B);          /* FIRQ: INC $2002; RTI  */
    boot(m, 0x1000);
}

typedef struct { uint64_t at; uint8_t irq, firq; } event_t;

#define EVENTS 400

static bool cpus_equal(const mc6809_t *x, const mc6809_t *y) {
    return x->pc == y->pc && x->cc == y->cc && x->a == y->a && x->b == y->b &&
           x->dp == y->dp && x->x == y->x && x->y == y->y && x->u == y->u &&
           x->s == y->s && x->cycles == y->cycles && x->wait == y->wait &&
           x->latch == y->latch && x->active == y->active && x->stale == y->stale;
}

/* Machine A runs in slices to each event, skipping; then machine B, from
 * the same start over the same RAM, steps one instruction or one poll at
 * a time to the same events, `shift` cycles late. Returns the cycle of
 * the first event where they differ, or 0. */
static uint64_t skip_run(int shift, uint32_t *wakes) {
    static event_t ev[EVENTS];
    static mc6809_t a_cpu[EVENTS];
    static uint8_t a_count[EVENTS][3];
    static uint8_t ram_start[65536];
    uint64_t t = 50;
    uint32_t seed = 12345;
    for (int i = 0; i < EVENTS; i++) {
        seed = seed * 1103515245u + 12345u;
        t += 1u + (seed >> 16) % 300u;
        ev[i] = (event_t){ t, (uint8_t)((seed >> 3) & 1u), (uint8_t)((seed >> 5) & 1u) };
    }

    skip_program(&ma);
    skip_program(&mb);
    memcpy(ram_start, ram, sizeof ram);

    for (int i = 0; i < EVENTS; i++) {
        uint64_t now = ma.cpu.cycles;
        if (ev[i].at > now) mc6809_run(&ma, (uint32_t)(ev[i].at - now));
        a_cpu[i] = ma.cpu;
        memcpy(a_count[i], &ram[0x2000], 3);
        mc6809_set_irq(&ma.cpu, 1, ev[i].irq);
        mc6809_set_firq(&ma.cpu, 1, ev[i].firq);
    }

    memcpy(ram, ram_start, sizeof ram);
    uint64_t diff = 0;
    for (int i = 0; i < EVENTS; i++) {
        uint64_t at = ev[i].at + (uint64_t)shift;
        while (mb.cpu.cycles < at) mc6809_step(&mb);
        bool same = cpus_equal(&a_cpu[i], &mb.cpu) &&
                    memcmp(a_count[i], &ram[0x2000], 3) == 0;
        if (!same && diff == 0) diff = ev[i].at;
        mc6809_set_irq(&mb.cpu, 1, ev[i].irq);
        mc6809_set_firq(&mb.cpu, 1, ev[i].firq);
    }
    *wakes = (uint32_t)ram[0x2000] + ram[0x2001] + ram[0x2002];
    return diff;
}

static int test_skip(void) {
    uint32_t wakes;
    uint64_t diff = skip_run(0, &wakes);
    CHECK(diff == 0, "the skipping machine parted from the stepping one at cycle %llu",
          (unsigned long long)diff);
    CHECK(wakes > 100, "the program woke only %u times: the schedule exercised nothing", wakes);

    /* Control: the stepping machine one cycle late must part. */
    diff = skip_run(1, &wakes);
    CHECK(diff != 0, "a one-cycle shift went unseen: the comparison is blind");
    return 0;
}

int main(void) {
    if (test_run_contract()) return 1;
    if (test_reset_and_nmi()) return 1;
    if (test_firq_irq()) return 1;
    if (test_sync()) return 1;
    if (test_cwai()) return 1;
    if (test_halt()) return 1;
    if (test_trap()) return 1;
    if (test_rmw()) return 1;
    if (test_skip()) return 1;
    TEST_DONE();
}
