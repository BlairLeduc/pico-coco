/* xroar_cpu.c — the driver for XRoar's MC6809 (xroar_cpu.h).
 *
 * Compiled with XRoar's include paths and linked with its mc6809.c and
 * portalib's delegate.c from the pinned checkout. It also supplies the
 * four symbols mc6809.c needs from the rest of XRoar: part_new and three
 * debugger type descriptors the oracle never uses.
 */

#include "top-config.h"

#include <stdlib.h>

#include "delegate.h"
#include "debug.h"
#include "part.h"
#include "mc6809/mc6809.h"

#include "xroar_cpu.h"

/* ---- what mc6809.c needs from the rest of XRoar ---------------------- */

void *part_new(size_t psize) {
    void *p = calloc(1, psize);
    if (!p) abort();
    return p;
}

const struct debug_feature_type debug_feature_type_uint8;
const struct debug_feature_type debug_feature_type_uint16;
const struct debug_feature_type debug_feature_type_code_ptr16;

extern const struct partdb_entry mc6809_part;

/* ---- the driver ------------------------------------------------------ */

static struct MC6809 *cpu;
static xr_hooks_t hooks;
static unsigned long ncycles, limit;
static bool stop_at_boundary;

static void mem_cycle(void *sptr, bool rnw, uint16_t a) {
    (void)sptr;
    if (rnw) cpu->D = hooks.read(a);
    else     hooks.write(a, cpu->D);
    ncycles++;
    if (hooks.cycle) hooks.cycle(ncycles);
    if (ncycles >= limit) cpu->running = false;
}

static void posthook(void *sptr) {
    (void)sptr;
    if (hooks.boundary) hooks.boundary(ncycles);
    if (stop_at_boundary) cpu->running = false;
}

void xr_open(const xr_hooks_t *h) {
    hooks = *h;
    if (!cpu) {
        cpu = (struct MC6809 *)mc6809_part.funcs->allocate();
        mc6809_part.funcs->initialise(&cpu->part, NULL);
    }
    cpu->mem_cycle = DELEGATE_AS2(void, bool, uint16, mem_cycle, NULL);
    cpu->instruction_posthook = DELEGATE_AS0(void, posthook, NULL);
}

void xr_load(const xr_regs_t *r, bool irq, bool firq, bool nmi) {
    cpu->reg_cc = r->cc;
    MC6809_REG_A(cpu) = r->a;
    MC6809_REG_B(cpu) = r->b;
    cpu->reg_dp = r->dp;
    cpu->reg_x = r->x;
    cpu->reg_y = r->y;
    cpu->reg_u = r->u;
    cpu->reg_s = r->s;
    cpu->reg_pc = r->pc;
    cpu->nmi_armed = r->nmi_armed;

    cpu->halt = false;
    cpu->irq = irq;
    cpu->firq = firq;
    cpu->nmi = nmi;
    cpu->irq_latch = cpu->irq_active = irq;
    cpu->firq_latch = cpu->firq_active = firq;
    cpu->nmi_latch = cpu->nmi_active = nmi && r->nmi_armed;

    cpu->state = mc6809_state_label_a;
    cpu->page = 0;
    ncycles = 0;
}

void xr_save(xr_regs_t *r) {
    r->cc = cpu->reg_cc;
    r->a = MC6809_REG_A(cpu);
    r->b = MC6809_REG_B(cpu);
    r->dp = cpu->reg_dp;
    r->x = cpu->reg_x;
    r->y = cpu->reg_y;
    r->u = cpu->reg_u;
    r->s = cpu->reg_s;
    r->pc = cpu->reg_pc;
    r->nmi_armed = cpu->nmi_armed;
}

void xr_lines(bool irq, bool firq, bool nmi) {
    cpu->irq = irq;
    cpu->firq = firq;
    cpu->nmi = nmi;
}

void xr_reset(void) {
    cpu->reset(cpu);
}

unsigned long xr_run(unsigned long budget, bool one) {
    if (ncycles >= budget) return ncycles;
    limit = budget;
    stop_at_boundary = one;
    cpu->running = true;
    cpu->run(cpu);
    return ncycles;
}
