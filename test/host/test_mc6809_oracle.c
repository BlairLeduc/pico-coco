/* test_mc6809_oracle.c — the core held to XRoar's MC6809, instruction by
 * instruction (design.md §7.4, §15.2, D2).
 *
 * Both CPUs start from the same registers over the same memory: a
 * pattern hashed from a seed and the address, under the instruction's own
 * bytes. Each runs, and the two are compared on every register, NMI's
 * arming, the cycle count (XRoar calls back once a bus cycle), every
 * write in order, and the reads: ours must be a subsequence of XRoar's,
 * which has the dummy cycles besides, so the core reads nothing the chip
 * does not. This is the cycle table asserted by execution (EL §3.3).
 *
 *   instructions  every opcode on each page, documented or not, 2048
 *                 trials each over random registers; an indexed opcode's
 *                 postbyte runs through all 256 values eight times, TFR's
 *                 and EXG's likewise
 *   chains        every opcode after two prefixes, in each of the four
 *                 orders, 64 trials each
 *   hcf           $14, $15 and $CD lock the CPU until RESET, interrupts
 *                 asserted
 *   flat          a sample again with our core on the page table's fast
 *                 path over a real 64 KB, memory compared whole
 *   interrupts    IRQ, FIRQ and NMI against masks and NMI's arming
 *   waits         SYNC and CWAI, lines asserted before and at a chosen
 *                 cycle of the wait, each instruction boundary compared
 *   reset         the reset sequence
 *
 * Undocumented opcodes, indexed postbytes and TFR/EXG register pairs are
 * executed by both, as the NMOS chip executes them, and compared (D12;
 * the owner, 2026-11-16). SYNC and CWAI reached through a prefix are
 * left to test_waits, which takes them on page 1.
 *
 * Without XRoar's checkout this test is built as a skip; a skip is an
 * unverified CPU (§15.1).
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "coco.h"
#include "mc6809.h"
#include "test_util.h"
#include "xroar_cpu.h"

/* ---- random numbers and the memory pattern --------------------------- */

static uint64_t rng_state = 0x6809C0C0DEADBEEFull;

static uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t rnd(void) {
    rng_state += 0x9E3779B97F4A7C15ull;
    return mix(rng_state);
}

/* Values that make flags interesting, given an eighth of the time. */
static const uint8_t special8[8] = { 0x00, 0x01, 0x7F, 0x80, 0x81, 0xFF, 0x99, 0x0F };

static uint8_t pattern(uint64_t h) {
    if (((h >> 8) & 7u) == 0) return special8[(h >> 11) & 7u];
    return (uint8_t)h;
}

static uint8_t rnd8(void) { return pattern(rnd()); }
static uint16_t rnd16(void) { return (uint16_t)((rnd8() << 8) | rnd8()); }

/* ---- memory: a pattern, the instruction's bytes, and logged writes ---- */

typedef struct { uint16_t a; uint8_t v; } access_t;

#define MAX_PRE    32
#define MAX_WRITES 64
#define MAX_READS  256

typedef struct { uint16_t a, len; uint8_t v; } range_t;

typedef struct {
    uint64_t seed;
    int      nrange;
    range_t  range[4];       /* runs of one byte: NOPs where code must be */
    int      npre;
    access_t pre[MAX_PRE];
    int      nw, nr;
    access_t w[MAX_WRITES];
    uint16_t r[MAX_READS];
    bool     overflow;
} mem_t;

static uint8_t mem_base(const mem_t *mm, uint16_t a) {
    for (int i = mm->npre - 1; i >= 0; i--)
        if (mm->pre[i].a == a) return mm->pre[i].v;
    for (int i = 0; i < mm->nrange; i++)
        if ((uint16_t)(a - mm->range[i].a) < mm->range[i].len) return mm->range[i].v;
    return pattern(mix(mm->seed ^ ((uint64_t)a * 0x9E3779B97F4A7C15ull)));
}

static uint8_t mem_read(mem_t *mm, uint16_t a) {
    if (mm->nr < MAX_READS) mm->r[mm->nr++] = a;
    else mm->overflow = true;
    for (int i = mm->nw - 1; i >= 0; i--)
        if (mm->w[i].a == a) return mm->w[i].v;
    return mem_base(mm, a);
}

static void mem_write(mem_t *mm, uint16_t a, uint8_t v) {
    if (mm->nw < MAX_WRITES) mm->w[mm->nw++] = (access_t){ a, v };
    else mm->overflow = true;
}

static void mem_start(mem_t *mm, uint64_t seed) {
    mm->seed = seed;
    mm->npre = mm->nw = mm->nr = mm->nrange = 0;
    mm->overflow = false;
}

static void mem_pre(mem_t *mm, uint16_t a, uint8_t v) {
    if (mm->npre < MAX_PRE) mm->pre[mm->npre++] = (access_t){ a, v };
    else mm->overflow = true;
}

static mem_t ours_mem, theirs_mem;

/* Our core's slow path: every page is unmapped, so every access is seen. */
uint8_t bus_read_slow(coco_t *m, uint16_t a) { (void)m; return mem_read(&ours_mem, a); }
void bus_write_slow(coco_t *m, uint16_t a, uint8_t v) { (void)m; mem_write(&ours_mem, a, v); }

static uint8_t xr_read(uint16_t a) { return mem_read(&theirs_mem, a); }
static void xr_write(uint16_t a, uint8_t v) { mem_write(&theirs_mem, a, v); }

/* ---- the two CPUs ---------------------------------------------------- */

static coco_t ours;

static void ours_load(const xr_regs_t *r, bool irq, bool firq, bool nmi) {
    mc6809_t *c = &ours.cpu;
    mc6809_init(c);
    c->cc = r->cc; c->a = r->a; c->b = r->b; c->dp = r->dp;
    c->x = r->x; c->y = r->y; c->u = r->u; c->s = r->s; c->pc = r->pc;
    c->nmi_armed = r->nmi_armed;
    mc6809_set_irq(c, 1, irq);
    mc6809_set_firq(c, 1, firq);
    mc6809_set_nmi(c, nmi);
}

static void ours_save(xr_regs_t *r) {
    const mc6809_t *c = &ours.cpu;
    *r = (xr_regs_t){ c->cc, c->a, c->b, c->dp, c->x, c->y, c->u, c->s, c->pc, c->nmi_armed };
}

static void random_regs(xr_regs_t *r) {
    r->cc = (uint8_t)rnd();
    r->a = rnd8(); r->b = rnd8(); r->dp = (uint8_t)rnd();
    r->x = rnd16(); r->y = rnd16(); r->u = rnd16(); r->s = rnd16();
    r->pc = (uint16_t)rnd();
    r->nmi_armed = (rnd() & 1u) != 0;
}

/* ---- comparison ------------------------------------------------------ */

static int failures;
static unsigned long compared;

static void print_regs(const char *who, const xr_regs_t *r, unsigned long cyc) {
    fprintf(stderr, "    %-6s CC=%02X A=%02X B=%02X DP=%02X X=%04X Y=%04X U=%04X S=%04X PC=%04X armed=%d cycles=%lu\n",
            who, r->cc, r->a, r->b, r->dp, r->x, r->y, r->u, r->s, r->pc, r->nmi_armed, cyc);
}

static void print_bytes(const mem_t *mm) {
    fprintf(stderr, "    bytes:");
    for (int i = 0; i < mm->npre; i++) fprintf(stderr, " $%04X=%02X", mm->pre[i].a, mm->pre[i].v);
    fprintf(stderr, "\n");
}

static bool regs_equal(const xr_regs_t *a, const xr_regs_t *b) {
    return a->cc == b->cc && a->a == b->a && a->b == b->b && a->dp == b->dp &&
           a->x == b->x && a->y == b->y && a->u == b->u && a->s == b->s &&
           a->pc == b->pc && a->nmi_armed == b->nmi_armed;
}

/* Our reads, in order, must appear in XRoar's, in order. */
static bool reads_within(const mem_t *ours_m, const mem_t *theirs_m) {
    int j = 0;
    for (int i = 0; i < ours_m->nr; i++) {
        while (j < theirs_m->nr && theirs_m->r[j] != ours_m->r[i]) j++;
        if (j == theirs_m->nr) return false;
        j++;
    }
    return true;
}

static bool writes_equal(const mem_t *a, const mem_t *b) {
    if (a->nw != b->nw) return false;
    for (int i = 0; i < a->nw; i++)
        if (a->w[i].a != b->w[i].a || a->w[i].v != b->w[i].v) return false;
    return true;
}

/* Compare one trial; report the first few failures in full. */
static bool compare(const char *what, const xr_regs_t *start,
                    const xr_regs_t *o, unsigned long ocyc,
                    const xr_regs_t *t, unsigned long tcyc, bool check_memory) {
    compared++;
    const char *why = NULL;
    if (ours_mem.overflow || theirs_mem.overflow) why = "access log overflow";
    else if (!regs_equal(o, t))                   why = "registers";
    else if (ocyc != tcyc)                        why = "cycles";
    else if (check_memory && !writes_equal(&ours_mem, &theirs_mem)) why = "writes";
    else if (check_memory && !reads_within(&ours_mem, &theirs_mem)) why = "a read XRoar does not make";
    if (!why) return true;

    if (++failures <= 12) {
        fprintf(stderr, "FAIL %s: %s\n", what, why);
        print_bytes(&ours_mem);
        print_regs("start", start, 0);
        print_regs("ours", o, ocyc);
        print_regs("xroar", t, tcyc);
        fprintf(stderr, "    writes ours:");
        for (int i = 0; i < ours_mem.nw; i++) fprintf(stderr, " $%04X=%02X", ours_mem.w[i].a, ours_mem.w[i].v);
        fprintf(stderr, "\n    writes xroar:");
        for (int i = 0; i < theirs_mem.nw; i++) fprintf(stderr, " $%04X=%02X", theirs_mem.w[i].a, theirs_mem.w[i].v);
        fprintf(stderr, "\n    reads ours:");
        for (int i = 0; i < ours_mem.nr; i++) fprintf(stderr, " $%04X", ours_mem.r[i]);
        fprintf(stderr, "\n");
    }
    return false;
}

static void both_pre(uint16_t a, uint8_t v) {
    mem_pre(&ours_mem, a, v);
    mem_pre(&theirs_mem, a, v);
}

static void both_range(uint16_t a, uint16_t len, uint8_t v) {
    mem_t *mms[2] = { &ours_mem, &theirs_mem };
    for (int i = 0; i < 2; i++) {
        mem_t *mm = mms[i];
        if (mm->nrange < 4) mm->range[mm->nrange++] = (range_t){ a, len, v };
        else mm->overflow = true;
    }
}

static void both_start(uint64_t seed) {
    mem_start(&ours_mem, seed);
    mem_start(&theirs_mem, seed);
}

/* After loading, before running, the logs hold only the run's accesses. */
static void clear_logs(void) {
    ours_mem.nw = ours_mem.nr = 0;
    theirs_mem.nw = theirs_mem.nr = 0;
}

/* ---- instructions ---------------------------------------------------- */

static bool has_indexed(uint16_t op) {
    uint8_t lo = (uint8_t)op;
    if ((op >> 8) == 0 && lo >= 0x30 && lo <= 0x33) return true;
    uint8_t hi = lo & 0xF0u;
    return hi == 0xA0u || hi == 0xE0u || hi == 0x60u;
}

/* What test_instructions enumerates on each page: every byte but the
 * prefixes, which chain (test_chains), and SYNC and CWAI (test_waits). */
static bool enumerated(uint8_t lo) {
    return lo != 0x10 && lo != 0x11 && lo != 0x13 && lo != 0x3C;
}

static bool has_regpair(uint16_t op) { return op == 0x1E || op == 0x1F; }

/* Place the instruction at PC: prefix, opcode, and a chosen postbyte. */
static void place(uint16_t pc, uint16_t op, int postbyte) {
    uint16_t a = pc;
    if (op >> 8) both_pre(a++, (uint8_t)(op >> 8));
    both_pre(a++, (uint8_t)op);
    if (postbyte >= 0) both_pre(a, (uint8_t)postbyte);
}

static unsigned long run_ours_one(void) {
    uint64_t before = ours.cpu.cycles;
    mc6809_step(&ours);
    return (unsigned long)(ours.cpu.cycles - before);
}

#define TRIALS_PER_OP 2048

static int per_op_failures[3][256];

static void test_instructions(void) {
    int ops = 0;
    for (int page = 0; page < 3; page++) {
        for (int lo = 0; lo < 256; lo++) {
            uint16_t op = (uint16_t)((page == 0 ? 0 : page == 1 ? 0x1000 : 0x1100) | lo);
            if (!enumerated((uint8_t)lo)) continue;
            ops++;
            for (int t = 0; t < TRIALS_PER_OP; t++) {
                xr_regs_t start, o, th;
                random_regs(&start);
                both_start(rnd());
                int pb = (has_indexed(op) || has_regpair(op)) ? (t & 0xFF) : -1;
                place(start.pc, op, pb);

                ours_load(&start, false, false, false);
                xr_load(&start, false, false, false);
                clear_logs();

                unsigned long ocyc = run_ours_one();
                unsigned long tcyc = xr_run(200, true);
                ours_save(&o);
                xr_save(&th);

                char what[48];
                snprintf(what, sizeof what, "op $%04X postbyte %d trial %d", op, pb, t);
                if (!compare(what, &start, &o, ocyc, &th, tcyc, true))
                    per_op_failures[page][lo]++;
            }
        }
    }
    printf("instructions: %d opcodes, %d trials each\n", ops, TRIALS_PER_OP);
    for (int page = 0; page < 3; page++)
        for (int lo = 0; lo < 256; lo++)
            if (per_op_failures[page][lo])
                fprintf(stderr, "  $%s%02X: %d of %d trials differ\n",
                        page == 0 ? "" : page == 1 ? "10" : "11", lo,
                        per_op_failures[page][lo], TRIALS_PER_OP);
}

/* ---- prefixes chained ------------------------------------------------ */

/* Two prefixes, then an opcode: the first chose the page, the second is
 * a cycle (D12). */
static void test_chains(void) {
    static const uint8_t pre[4][2] = { { 0x10, 0x10 }, { 0x10, 0x11 }, { 0x11, 0x10 }, { 0x11, 0x11 } };
    int n = 0;
    for (int k = 0; k < 4; k++) {
        for (int lo = 0; lo < 256; lo++) {
            if (!enumerated((uint8_t)lo)) continue;
            for (int t = 0; t < 64; t++) {
                xr_regs_t start, o, th;
                random_regs(&start);
                both_start(rnd());
                both_pre(start.pc, pre[k][0]);
                both_pre((uint16_t)(start.pc + 1u), pre[k][1]);
                both_pre((uint16_t)(start.pc + 2u), (uint8_t)lo);
                both_pre((uint16_t)(start.pc + 3u), (uint8_t)rnd());

                ours_load(&start, false, false, false);
                xr_load(&start, false, false, false);
                clear_logs();

                unsigned long ocyc = run_ours_one();
                unsigned long tcyc = xr_run(200, true);
                ours_save(&o);
                xr_save(&th);

                char what[48];
                snprintf(what, sizeof what, "chain $%02X%02X%02X trial %d", pre[k][0], pre[k][1], lo, t);
                (void)compare(what, &start, &o, ocyc, &th, tcyc, true);
                n++;
            }
        }
    }
    printf("chains: %d trials\n", n);
}

/* ---- HCF ------------------------------------------------------------- */

/* After $14, $15 or $CD, alone or after a prefix, the CPU stands until
 * RESET: a hundred cycles more with every interrupt line asserted, and
 * the registers, the cycles and the bus as XRoar's (D12). */
static void test_hcf(void) {
    static const uint16_t ops[] = { 0x14, 0x15, 0xCD, 0x1014, 0x1115, 0x10CD };
    int n = 0;
    for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        for (int t = 0; t < 64; t++) {
            xr_regs_t start, o, th;
            random_regs(&start);
            both_start(rnd());
            place(start.pc, ops[i], -1);

            ours_load(&start, false, false, false);
            xr_load(&start, false, false, false);
            clear_logs();
            unsigned long ocyc = run_ours_one();
            unsigned long tcyc = xr_run(200, true);
            ours_save(&o);
            xr_save(&th);
            char what[48];
            snprintf(what, sizeof what, "HCF $%04X trial %d", ops[i], t);
            if (!compare(what, &start, &o, ocyc, &th, tcyc, true)) continue;

            /* Locked: the lines asserted, a hundred cycles each. */
            ours_load(&o, true, true, true);
            ours.cpu.wait = MC6809_HCF;
            mc6809_attn(&ours.cpu);
            xr_lines(true, true, true);
            clear_logs();
            uint64_t before = ours.cpu.cycles;
            mc6809_run(&ours, 100);
            ocyc = (unsigned long)(ours.cpu.cycles - before);
            tcyc = xr_run(tcyc + 100, false) - tcyc;
            xr_regs_t o2, th2;
            ours_save(&o2);
            xr_save(&th2);
            snprintf(what, sizeof what, "HCF $%04X locked, trial %d", ops[i], t);
            (void)compare(what, &o, &o2, ocyc, &th2, tcyc, true);
            if (o2.pc != o.pc && ++failures <= 12) fprintf(stderr, "FAIL %s: PC moved\n", what);
            n++;
        }
    }
    printf("hcf: %d trials\n", n);
}

/* ---- the fast path, over a real 64 KB -------------------------------- */

static uint8_t flat[65536], flat_expect[65536], flat_base[65536];
#define FLAT_SEED 0xF1A7F1A7F1A7F1A7ull

static void map_flat(void) {
    for (unsigned p = 0; p < COCO_PAGE_COUNT; p++) {
        ours.page[p].read = &flat[p * COCO_PAGE_SIZE];
        ours.page[p].write = &flat[p * COCO_PAGE_SIZE];
    }
}

static void unmap(void) {
    for (unsigned p = 0; p < COCO_PAGE_COUNT; p++)
        ours.page[p] = (page_t){ NULL, NULL };
}

static void test_flat(void) {
    int n = 0;
    map_flat();
    both_start(FLAT_SEED);
    for (unsigned a = 0; a < 65536u; a++) flat_base[a] = mem_base(&theirs_mem, (uint16_t)a);
    for (int page = 0; page < 3; page++) {
        for (int lo = 0; lo < 256; lo++) {
            uint16_t op = (uint16_t)((page == 0 ? 0 : page == 1 ? 0x1000 : 0x1100) | lo);
            if (!enumerated((uint8_t)lo)) continue;
            for (int t = 0; t < 32; t++) {
                xr_regs_t start, o, th;
                random_regs(&start);
                both_start(FLAT_SEED);
                int pb = (has_indexed(op) || has_regpair(op)) ? (int)(rnd() & 0xFF) : -1;
                place(start.pc, op, pb);
                memcpy(flat, flat_base, sizeof flat);
                for (int i = 0; i < theirs_mem.npre; i++) flat[theirs_mem.pre[i].a] = theirs_mem.pre[i].v;

                ours_load(&start, false, false, false);
                xr_load(&start, false, false, false);
                clear_logs();
                memcpy(flat_expect, flat, sizeof flat);

                unsigned long ocyc = run_ours_one();
                unsigned long tcyc = xr_run(200, true);
                ours_save(&o);
                xr_save(&th);
                for (int i = 0; i < theirs_mem.nw; i++) flat_expect[theirs_mem.w[i].a] = theirs_mem.w[i].v;

                char what[48];
                snprintf(what, sizeof what, "flat op $%04X trial %d", op, t);
                if (compare(what, &start, &o, ocyc, &th, tcyc, false) &&
                    memcmp(flat, flat_expect, sizeof flat) != 0) {
                    if (++failures <= 12) fprintf(stderr, "FAIL %s: memory differs\n", what);
                }
                n++;
            }
        }
    }
    unmap();
    printf("flat: %d trials on the fast path\n", n);
}

/* ---- interrupts at an instruction boundary --------------------------- */

static const uint16_t vectors[3] = { MC6809_VEC_NMI, MC6809_VEC_FIRQ, MC6809_VEC_IRQ };

/* Whether [a, a+len) and [b, b+blen) overlap, modulo 64 KB. */
static bool overlap(uint16_t a, unsigned len, uint16_t b, unsigned blen) {
    return (uint16_t)(b - a) < len || (uint16_t)(a - b) < blen;
}

/* NOPs at each handler and after the code at PC, `len` bytes each, as
 * ranges, so code runs where it was put until well past any horizon. The
 * trial is skipped (false) if a run would lie under the stack frame, the
 * vectors, or the `code` bytes at PC, or a vector under the frame. */
static bool nops_at_handlers(const xr_regs_t *r, unsigned code, unsigned len) {
    uint16_t frame = (uint16_t)(r->s - 16u);
    if (overlap(r->pc, code + len, frame, 16)) return false;
    if (overlap(r->pc, code + len, MC6809_VEC_FIRQ, 8)) return false;
    for (int i = 0; i < 3; i++) {
        uint16_t target = (uint16_t)((mem_base(&ours_mem, vectors[i]) << 8) |
                                     mem_base(&ours_mem, (uint16_t)(vectors[i] + 1u)));
        if (overlap(target, len, frame, 16)) return false;
        if (overlap(target, len, r->pc, code)) return false;
        if (overlap(vectors[i], 2, frame, 16)) return false;
        for (int j = 0; j < 3; j++)
            if (overlap(target, len, vectors[j], 2)) return false;
        both_range(target, (uint16_t)len, 0x12);
    }
    both_range((uint16_t)(r->pc + code), (uint16_t)len, 0x12);
    return true;
}

static void test_interrupts(void) {
    int n = 0, taken = 0;
    for (int t = 0; t < 40000; t++) {
        xr_regs_t start, o, th;
        random_regs(&start);
        both_start(rnd());
        bool irq = rnd() & 1u, firq = rnd() & 1u, nmi = (rnd() & 3u) == 0;
        both_pre(start.pc, 0x12);
        if (!nops_at_handlers(&start, 1, 8)) continue;

        ours_load(&start, irq, firq, nmi);
        xr_load(&start, irq, firq, nmi);
        clear_logs();

        /* XRoar runs an entry on into the handler's first instruction. */
        uint8_t take = (uint8_t)((nmi && start.nmi_armed) ||
                                 (firq && !(start.cc & MC6809_CC_F)) ||
                                 (irq && !(start.cc & MC6809_CC_I)));
        unsigned long ocyc = run_ours_one();
        if (take) { ocyc += run_ours_one(); taken++; }
        unsigned long tcyc = xr_run(200, true);
        ours_save(&o);
        xr_save(&th);

        char what[64];
        snprintf(what, sizeof what, "interrupt irq=%d firq=%d nmi=%d trial %d", irq, firq, nmi, t);
        compare(what, &start, &o, ocyc, &th, tcyc, true);
        n++;
    }
    printf("interrupts: %d trials, %d taken\n", n, taken);
}

/* ---- SYNC and CWAI --------------------------------------------------- */

/* Each CPU's instruction boundaries, and the end of SYNC, with the
 * registers and cycle count there. */
typedef struct { xr_regs_t r; unsigned long cyc; } event_t;

#define MAX_EVENTS 64
static event_t xr_events[MAX_EVENTS], our_events[MAX_EVENTS];
static int n_xr_events, n_our_events;

static unsigned long change_at, release_at;
static bool change_irq, change_firq, change_nmi, change_armed_now, release_armed;

static void xr_cycle_hook(unsigned long n) {
    if (change_armed_now && n == change_at) xr_lines(change_irq, change_firq, change_nmi);
    if (release_armed && n == release_at) xr_halt(false);
}

static void xr_boundary_hook(unsigned long n) {
    if (n_xr_events < MAX_EVENTS) {
        xr_save(&xr_events[n_xr_events].r);
        xr_events[n_xr_events].cyc = n;
        n_xr_events++;
    }
}

static void our_event(unsigned long cyc) {
    if (n_our_events < MAX_EVENTS) {
        ours_save(&our_events[n_our_events].r);
        our_events[n_our_events].cyc = cyc;
        n_our_events++;
    }
}

/* One step of ours, recording a boundary as XRoar would: after an
 * instruction, and when SYNC ends; not after a poll, an entry, or the SYNC
 * instruction itself, whose boundary XRoar gives only when the wait ends. */
static void ours_step_recording(unsigned long *cyc) {
    mc6809_t *c = &ours.cpu;
    uint8_t wait = c->wait;
    uint8_t lines = (uint8_t)((c->irq ? 1 : 0) | (c->firq ? 2 : 0) | ((c->nmi_pending && c->nmi_armed) ? 4 : 0));
    uint8_t open = (uint8_t)(4 | ((c->cc & MC6809_CC_F) ? 0 : 2) | ((c->cc & MC6809_CC_I) ? 0 : 1));
    uint8_t avail = c->stale ? c->active : lines;     /* as the core decides */
    bool entry = wait == MC6809_RUNNING && !c->halt && (avail & open);
    bool halted = c->halt;
    *cyc += mc6809_step(&ours);
    if (halted) return;   /* a cycle of HALT: no boundary */
    if (wait == MC6809_SYNC && c->wait == MC6809_RUNNING) our_event(*cyc);
    else if (wait == MC6809_RUNNING && !entry && c->wait != MC6809_SYNC) our_event(*cyc);
}

#define WAIT_HORIZON 160u

static void test_waits(uint16_t op) {
    int n = 0, changed = 0;
    for (int t = 0; t < 30000; t++) {
        xr_regs_t start;
        random_regs(&start);
        both_start(rnd());
        int len = op == 0x13 ? 1 : 2;
        both_pre(start.pc, (uint8_t)op);
        if (op == 0x3C) both_pre((uint16_t)(start.pc + 1u), (uint8_t)rnd());
        if (!nops_at_handlers(&start, (unsigned)len, 2u * WAIT_HORIZON)) continue;

        /* Lines before, and a change at a cycle of the wait. */
        bool irq = (rnd() & 3u) == 0, firq = (rnd() & 3u) == 0, nmi = (rnd() & 7u) == 0;
        change_irq = rnd() & 1u;
        change_firq = rnd() & 1u;
        change_nmi = nmi || (rnd() & 3u) == 0;     /* an edge only rises */
        unsigned long first = op == 0x13 ? 2u : 18u;
        unsigned long want = first + (rnd() % 24u);

        ours_load(&start, irq, firq, nmi);
        clear_logs();
        n_our_events = 0;

        /* Ours first, to find the boundary at or after `want`; the change
         * goes in only if ours is still waiting there, since outside a
         * wait the two sample lines at different points (§7.1). */
        unsigned long cyc = 0;
        while (cyc < want) ours_step_recording(&cyc);
        change_armed_now = ours.cpu.wait != MC6809_RUNNING;
        change_at = cyc;
        if (change_armed_now) {
            mc6809_set_irq(&ours.cpu, 1, change_irq);
            mc6809_set_firq(&ours.cpu, 1, change_firq);
            mc6809_set_nmi(&ours.cpu, change_nmi);
            changed++;
        }
        while (cyc < WAIT_HORIZON) ours_step_recording(&cyc);

        n_xr_events = 0;
        xr_load(&start, irq, firq, nmi);
        xr_run(WAIT_HORIZON, false);

        /* Each runs past the horizon only to finish what it is doing, so
         * both have every boundary up to it: the same ones, equal. */
        int m = 0;
        while (m < n_our_events && our_events[m].cyc <= WAIT_HORIZON) m++;
        int k = 0;
        while (k < n_xr_events && xr_events[k].cyc <= WAIT_HORIZON) k++;
        bool ok = true;
        char what[96];
        snprintf(what, sizeof what, "%s irq=%d firq=%d nmi=%d -> %d%d%d at %lu trial %d",
                 op == 0x13 ? "SYNC" : "CWAI", irq, firq, nmi,
                 change_irq, change_firq, change_nmi, change_armed_now ? change_at : 0, t);
        for (int i = 0; i < m && i < k && ok; i++)
            ok = compare(what, &start, &our_events[i].r, our_events[i].cyc,
                         &xr_events[i].r, xr_events[i].cyc, false);
        if (ok && m != k) {
            ok = false;
            if (++failures <= 12) {
                fprintf(stderr, "FAIL %s: %d boundaries ours, %d xroar\n", what, m, k);
                if (m > k) print_regs("ours", &our_events[k].r, our_events[k].cyc);
                else       print_regs("xroar", &xr_events[m].r, xr_events[m].cyc);
            }
        }
        if (ok && !writes_equal(&ours_mem, &theirs_mem)) {
            if (++failures <= 12)
                fprintf(stderr, "FAIL %s trial %d: writes differ\n", op == 0x13 ? "SYNC" : "CWAI", t);
        }
        n++;
    }
    change_armed_now = false;
    printf("%s: %d trials, %d with lines changed during the wait\n",
           op == 0x13 ? "SYNC" : "CWAI", n, changed);
}

/* ---- HALT ------------------------------------------------------------ */

/* HALT asserted at a boundary, the lines changed at a cycle of it, and
 * HALT released then or later: the boundaries after, with the
 * instructions and any entry they make, held to XRoar's (§7.1). The
 * RS-DOS cartridge's read loop ends on exactly this: INTRQ releases HALT
 * and raises NMI together (rsdos.h). */
#define HALT_HORIZON 120u

static void test_halts(void) {
    int n = 0;
    for (int t = 0; t < 30000; t++) {
        xr_regs_t start;
        random_regs(&start);
        both_start(rnd());
        /* NOPs from the PC and at each handler. */
        if (!nops_at_handlers(&start, 0, 2u * HALT_HORIZON)) continue;

        bool irq = (rnd() & 3u) == 0, firq = (rnd() & 3u) == 0, nmi = (rnd() & 7u) == 0;
        change_irq = rnd() & 1u;
        change_firq = rnd() & 1u;
        change_nmi = nmi || (rnd() & 1u);
        change_at = 1u + rnd() % 20u;
        release_at = change_at + rnd() % 4u;

        ours_load(&start, irq, firq, nmi);
        mc6809_set_halt(&ours.cpu, true);
        clear_logs();
        n_our_events = 0;
        unsigned long cyc = 0;
        while (cyc < HALT_HORIZON) {
            if (cyc == change_at) {
                mc6809_set_irq(&ours.cpu, 1, change_irq);
                mc6809_set_firq(&ours.cpu, 1, change_firq);
                mc6809_set_nmi(&ours.cpu, change_nmi);
            }
            if (cyc == release_at) mc6809_set_halt(&ours.cpu, false);
            ours_step_recording(&cyc);
        }

        n_xr_events = 0;
        xr_load(&start, irq, firq, nmi);
        xr_halt(true);
        change_armed_now = release_armed = true;
        xr_run(HALT_HORIZON, false);
        change_armed_now = release_armed = false;

        int m = 0;
        while (m < n_our_events && our_events[m].cyc <= HALT_HORIZON) m++;
        int k = 0;
        while (k < n_xr_events && xr_events[k].cyc <= HALT_HORIZON) k++;
        bool ok = true;
        char what[112];
        snprintf(what, sizeof what, "HALT irq=%d firq=%d nmi=%d -> %d%d%d at %lu, released at %lu, trial %d",
                 irq, firq, nmi, change_irq, change_firq, change_nmi, change_at, release_at, t);
        for (int i = 0; i < m && i < k && ok; i++)
            ok = compare(what, &start, &our_events[i].r, our_events[i].cyc,
                         &xr_events[i].r, xr_events[i].cyc, false);
        if (ok && m != k) {
            ok = false;
            if (++failures <= 12) {
                fprintf(stderr, "FAIL %s: %d boundaries ours, %d xroar\n", what, m, k);
                if (m > k) print_regs("ours", &our_events[k].r, our_events[k].cyc);
                else       print_regs("xroar", &xr_events[m].r, xr_events[m].cyc);
            }
        }
        if (ok && !writes_equal(&ours_mem, &theirs_mem)) {
            if (++failures <= 12) fprintf(stderr, "FAIL %s: writes differ\n", what);
        }
        n++;
    }
    printf("HALT: %d trials\n", n);
}

/* ---- reset ----------------------------------------------------------- */

static void test_reset(void) {
    int n = 0;
    for (int t = 0; t < 4000; t++) {
        xr_regs_t start, o, th;
        random_regs(&start);
        both_start(rnd());
        uint16_t target = (uint16_t)((mem_base(&ours_mem, 0xFFFE) << 8) | mem_base(&ours_mem, 0xFFFF));
        if (target >= 0xFFFD) continue;
        both_pre(target, 0x12);

        ours_load(&start, false, false, false);
        xr_load(&start, false, false, false);
        clear_logs();

        uint64_t before = ours.cpu.cycles;
        mc6809_reset(&ours);
        mc6809_step(&ours);
        unsigned long ocyc = (unsigned long)(ours.cpu.cycles - before);
        xr_reset();
        unsigned long tcyc = xr_run(200, true);
        ours_save(&o);
        xr_save(&th);
        char what[32];
        snprintf(what, sizeof what, "reset trial %d", t);
        compare(what, &start, &o, ocyc, &th, tcyc, true);
        n++;
    }
    printf("reset: %d trials\n", n);
}

int main(void) {
    xr_hooks_t h = { xr_read, xr_write, xr_cycle_hook, xr_boundary_hook };
    xr_open(&h);

    test_instructions();
    test_chains();
    test_hcf();
    test_flat();
    test_interrupts();
    test_waits(0x13);
    test_waits(0x3C);
    test_halts();
    test_reset();

    printf("%lu comparisons\n", compared);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
