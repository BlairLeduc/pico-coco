/* coco-trace.c — this core's half of the trace diff against XRoar
 * (design.md §15.2, EL §11.4).
 *
 *   coco-trace [-ram K] [-ext] [-fields N] [-type STRING] > ours.trace
 *
 * Boots a CoCo on the real ROMs (found as test/host/guest.c finds them)
 * and prints a line after each instruction: its address and every
 * register after it, in the form XRoar's -trace prints, so that
 * tools/trace/trace-diff.py can hold the two together. Interrupt entries
 * and wait cycles print nothing, as XRoar's instruction lines do not.
 *
 * Typing goes in as XRoar's -type puts it in, so the two take the same
 * path through the ROM (give ENTER as a carriage return): while text is queued, reaching POLCAT returns the
 * next character in A with Z clear, and reaching the 1 ms delay returns at
 * once, each as an RTS that takes no time, at a boundary where no
 * interrupt is due (XRoar's auto_kbd.c and breakpoints, read 2026-11-14).
 * The matrix path is test_boot's to test, not this tool's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"

/* Color BASIC 1.3's entry points, from XRoar's symbol table (§18). */
#define BAS13_POLCAT    0xA1CBu
#define BAS13_DELAY_1MS 0xA7D3u

typedef struct {
    uint16_t pc;          /* PC before the step that is about to run */
    uint64_t insns;
    const char *type;     /* what is still to type, NULL when done   */
} trace_t;

static guest_t g;
static trace_t t;

static uint16_t rd(coco_t *m, uint16_t a) {
    return (uint16_t)((coco_slow_read(m, a) << 8) | coco_slow_read(m, (uint16_t)(a + 1u)));
}

/* What the CPU will do at this boundary: take an interrupt? Then a
 * breakpoint does not fire, as XRoar checks them only before a fetch. */
static bool interrupt_due(const mc6809_t *c) {
    if (c->wait != MC6809_RUNNING) return true;
    if (c->nmi_pending && c->nmi_armed) return true;
    if (c->firq && !(c->cc & MC6809_CC_F)) return true;
    return c->irq && !(c->cc & MC6809_CC_I);
}

static void rts(coco_t *m) {
    m->cpu.pc = rd(m, m->cpu.s);
    m->cpu.s = (uint16_t)(m->cpu.s + 2u);
}

static void hook(coco_t *m) {
    mc6809_t *c = &m->cpu;
    if (c->instructions != t.insns) {
        t.insns = c->instructions;
        printf("%04x| cc=%02x a=%02x b=%02x dp=%02x x=%04x y=%04x u=%04x s=%04x\n",
               t.pc, c->cc, c->a, c->b, c->dp, c->x, c->y, c->u, c->s);
    }
    /* XRoar's breakpoints, while typing. */
    while (t.type && !interrupt_due(c)) {
        if (c->pc == BAS13_POLCAT) {
            c->a = 0;
            c->cc |= MC6809_CC_Z;
            if (*t.type) {
                c->a = (uint8_t)*t.type;   /* as XRoar: no translation */
                c->cc &= (uint8_t)~MC6809_CC_Z;
                t.type++;
            }
            if (!*t.type) t.type = NULL;
            rts(m);
        } else if (c->pc == BAS13_DELAY_1MS) {
            rts(m);
        } else {
            break;
        }
    }
    t.pc = c->pc;
}

int main(int argc, char **argv) {
    unsigned ram_k = 64, fields = 300;
    bool ext = false;
    const char *type = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram_k = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ext")) ext = true;
        else if (!strcmp(argv[i], "-fields") && i + 1 < argc) fields = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-type") && i + 1 < argc) type = argv[++i];
        else {
            fprintf(stderr, "usage: %s [-ram K] [-ext] [-fields N] [-type STRING]\n", argv[0]);
            return 2;
        }
    }
    const char *dir;
    if (!guest_find_roms(&dir)) {
        fprintf(stderr, "coco-trace: no Color BASIC 1.3 in %s\n", dir);
        return 1;
    }
    guest_build(&g, ram_k, ext);
    t.pc = g.m.cpu.pc;
    t.insns = g.m.cpu.instructions;
    t.type = (type && *type) ? type : NULL;
    g.m.step_hook = hook;
    for (unsigned f = 0; f < fields; f++) coco_run_field(&g.m);
    fflush(stdout);
    for (int r = 0; r < 16; r++) fprintf(stderr, "|%-32s|\n", guest_row(&g.m, r));
    return 0;
}
