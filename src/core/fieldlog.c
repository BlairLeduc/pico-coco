/* fieldlog.c — see fieldlog.h. */

#include "fieldlog.h"

#include <stdio.h>

void fieldlog_take(fieldlog_t *r, const coco_t *m) {
    const mc6809_t *c = &m->cpu;
    r->field = m->fields;
    r->pc = c->pc;
    r->x = c->x;
    r->y = c->y;
    r->u = c->u;
    r->s = c->s;
    r->a = c->a;
    r->b = c->b;
    r->dp = c->dp;
    r->cc = c->cc;
    r->cycles = c->cycles;
    r->insns = c->instructions;
    r->ticks = coco_ticks(m);
}

int fieldlog_format(const fieldlog_t *r, char *out, size_t n) {
    return snprintf(out, n,
                    "field %lu pc=%04X a=%02X b=%02X x=%04X y=%04X u=%04X s=%04X dp=%02X cc=%02X "
                    "cyc=%llu ins=%llu t=%llu",
                    (unsigned long)r->field, r->pc, r->a, r->b, r->x, r->y, r->u, r->s, r->dp,
                    r->cc, (unsigned long long)r->cycles, (unsigned long long)r->insns,
                    (unsigned long long)r->ticks);
}
