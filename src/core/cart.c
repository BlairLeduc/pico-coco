/* cart.c — the cartridge port (cart.h, design.md §12.8). */

#include "cart.h"

#include "coco.h"
#include "hot.h"

void cart_init(coco_t *m) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS) wd_init(&c->u.rsdos.fdc);
}

uint8_t COCO_HOT1(cart_io_read)(coco_t *m, uint16_t a, uint8_t bus, uint64_t now) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS && rsdos_decodes(a & 0xFFu)) return rsdos_read(m, c, a, bus, now);
    return bus;
}

void COCO_HOT1(cart_io_write)(coco_t *m, uint16_t a, uint8_t v, uint64_t now) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS && rsdos_decodes(a & 0xFFu)) rsdos_write(m, c, a, v, now);
}

void cart_reset(coco_t *m) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS) {
        rsdos_reset(m, c);
    } else {
        c->nmi = c->halt = false;
        cart_lines(m);
    }
}

void COCO_HOT1(cart_run)(coco_t *m, uint64_t now) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS) rsdos_run(m, c, now);
}

void COCO_HOT1(cart_lines)(coco_t *m) {
    mc6809_set_nmi(&m->cpu, m->port.nmi);
    mc6809_set_halt(&m->cpu, m->port.halt);
}

void cart_restored(coco_t *m) {
    cart_t *c = &m->port;
    if (c->kind == CART_RSDOS) rsdos_lines(m, c);
}

cart_t *cart_rsdos(coco_t *m) {
    return m->port.kind == CART_RSDOS ? &m->port : NULL;
}

const cart_t *cart_rsdos_c(const coco_t *m) {
    return m->port.kind == CART_RSDOS ? &m->port : NULL;
}
