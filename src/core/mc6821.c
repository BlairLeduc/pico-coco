/* mc6821.c — the MC6821 PIA (mc6821.h). */

#include "mc6821.h"

#include "hot.h"

void mc6821_reset(mc6821_t *p) {
    p->a.out = p->a.ddr = p->a.cr = 0;
    p->b.out = p->b.ddr = p->b.cr = 0;
    p->a.irq_out = p->b.irq_out = false;
}

static uint8_t data_read(mc6821_port_t *port) {
    if (!(port->cr & MC6821_CR_DATA)) return port->ddr;
    port->cr &= (uint8_t)~(MC6821_CR_IRQ1 | MC6821_CR_IRQ2);
    port->irq_out = false;
    return (uint8_t)((port->out & port->ddr) | (port->in & (uint8_t)~port->ddr));
}

uint8_t COCO_HOT1(mc6821_read)(mc6821_t *p, unsigned reg) {
    switch (reg & 3u) {
    case 0:  return data_read(&p->a);
    case 1:  return p->a.cr;
    case 2:  return data_read(&p->b);
    default: return p->b.cr;
    }
}

static void data_write(mc6821_port_t *port, uint8_t v) {
    if (port->cr & MC6821_CR_DATA) port->out = v;
    else                           port->ddr = v;
}

static bool c1_enabled(const mc6821_port_t *port) {
    return (port->cr & MC6821_CR_C1_IRQ) != 0;
}

static bool c2_enabled(const mc6821_port_t *port) {
    return (port->cr & (MC6821_CR_C2_OUT | MC6821_CR_C2_B3)) == MC6821_CR_C2_B3;
}

/* The flags are read only; the rest is written. The output is then each
 * flag gated by its enable, as the datasheet's IRQ logic has it:
 * (IRQ1 and CR0) or (IRQ2 and CR3, C2 an input). XRoar's write_cr instead
 * keeps the output up when C1's enable is set and leaves C2 unexamined;
 * no ROM booted so far tells the two apart (2026-11-15, §18). */
static void cr_write(mc6821_port_t *port, uint8_t v) {
    port->cr = (uint8_t)((port->cr & (MC6821_CR_IRQ1 | MC6821_CR_IRQ2)) | (v & 0x3Fu));
    /* C2 made an output cannot keep an input's flag. */
    if (port->cr & MC6821_CR_C2_OUT) port->cr &= (uint8_t)~MC6821_CR_IRQ2;
    port->irq_out = (c1_enabled(port) && (port->cr & MC6821_CR_IRQ1)) ||
                    (c2_enabled(port) && (port->cr & MC6821_CR_IRQ2));
}

/* tRS3 after an enabled edge: the output rises if that line's interrupt
 * is still enabled, even if a read has cleared the flag meanwhile, as
 * XRoar's trace needs (§18); one disabled meanwhile stays down, as the
 * datasheet's gating has it. */
void COCO_HOT1(mc6821_raise_c1)(mc6821_port_t *port) {
    if (c1_enabled(port)) port->irq_out = true;
}

void mc6821_raise_c2(mc6821_port_t *port) {
    if (c2_enabled(port)) port->irq_out = true;
}

void COCO_HOT1(mc6821_write)(mc6821_t *p, unsigned reg, uint8_t v) {
    switch (reg & 3u) {
    case 0:  data_write(&p->a, v); break;
    case 1:  cr_write(&p->a, v);   break;
    case 2:  data_write(&p->b, v); break;
    default: cr_write(&p->b, v);   break;
    }
}

/* An active edge with its interrupt disabled lowers the output, as XRoar
 * has it. */
bool COCO_HOT1(mc6821_set_c1)(mc6821_port_t *port, bool level) {
    bool raise = false;
    if (level != port->c1) {
        bool rise = (port->cr & MC6821_CR_C1_RISE) != 0;
        if (level == rise) {
            port->cr |= MC6821_CR_IRQ1;
            if (port->cr & MC6821_CR_C1_IRQ) raise = true;
            else port->irq_out = false;
        }
    }
    port->c1 = level;
    return raise;
}

bool mc6821_set_c2(mc6821_port_t *port, bool level) {
    bool raise = false;
    if (level != port->c2 && !(port->cr & MC6821_CR_C2_OUT)) {
        bool rise = (port->cr & MC6821_CR_C2_B4) != 0;
        if (level == rise) {
            port->cr |= MC6821_CR_IRQ2;
            if (port->cr & MC6821_CR_C2_B3) raise = true;
            else port->irq_out = false;
        }
    }
    port->c2 = level;
    return raise;
}
