/* cart.c — the cartridge port and the Multi-Pak (cart.h, design.md §12.8). */

#include "cart.h"

#include <string.h>

#include "coco.h"
#include "hot.h"

#define SCS_FIRST       0x40u    /* $FF40-$FF5F                            */
#define SCS_END         0x60u
#define MPI_REG         0xFF7Fu
#define SMARTWATCH_LOW  0x58u    /* $FF58-$FF5F on SCS (§12.7)             */

static inline bool is_scs(unsigned off) { return off - SCS_FIRST < SCS_END - SCS_FIRST; }

/* ---- set-up ------------------------------------------------------------ */

void cart_init(coco_t *m) {
    cart_port_t *p = &m->port;
    const coco_config_t *cfg = &m->cfg;
    p->mpi = cfg->cart == CART_MPI;
    p->rsdos_slot = -1;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_kind_t k = CART_NONE;
        if (p->mpi) k = cfg->slot[s];
        else if (s == 0) k = cfg->cart;
        if (k == CART_MPI || k >= CART_KINDS) k = CART_NONE;   /* not in a slot */
        if (k == CART_RSDOS) {
            if (p->rsdos_slot >= 0) k = CART_NONE;   /* one controller (cart.h) */
            else p->rsdos_slot = (int8_t)s;
        }
        p->slot[s].kind = k;
        if (k == CART_SMARTWATCH) smartwatch_init(&p->slot[s].u.clock);
    }
    if (p->rsdos_slot >= 0) wd_init(&p->dk.fdc);
    p->switch_slot = p->mpi ? (uint8_t)(cfg->mpi_switch & 3u) : 0u;
    p->switch_on = true;
    p->cts = p->scs = p->switch_slot;
}

/* ---- ROMs -------------------------------------------------------------- */

static bool pow2(size_t n) { return n && (n & (n - 1u)) == 0; }

bool cart_load_rom(coco_t *m, unsigned s, const uint8_t *data, size_t len) {
    cart_port_t *p = &m->port;
    if (s >= COCO_MPI_SLOTS || len == 0 || len > COCO_CART_SIZE) return false;
    cart_t *c = &p->slot[s];
    uint32_t room = pow2(len) ? (len < COCO_PAGE_SIZE ? COCO_PAGE_SIZE : (uint32_t)len)
                              : COCO_CART_SIZE;
    uint32_t at;
    if (c->rom && c->rom_room >= room) {
        at = c->rom_at;   /* its own room again */
    } else {
        if (c->rom && c->rom_at + c->rom_room == p->pool_used) p->pool_used = c->rom_at;
        if (p->pool_used + room > COCO_CART_POOL) return false;
        at = p->pool_used;
        p->pool_used += room;
    }
    uint8_t *dst = &m->cart[at];
    memset(dst, 0xFF, room);
    memcpy(dst, data, len);
    if (pow2(len))
        for (uint32_t k = (uint32_t)len; k < room; k += (uint32_t)len) memcpy(dst + k, data, len);
    c->rom_at = at;
    c->rom_room = room;
    c->rom = true;
    if (c->kind == CART_NONE) c->kind = CART_ROM;
    c->autostart = c->kind == CART_ROM;
    return true;
}

void cart_remove_rom(coco_t *m, unsigned s) {
    cart_port_t *p = &m->port;
    if (s >= COCO_MPI_SLOTS) return;
    cart_t *c = &p->slot[s];
    if (c->rom && c->rom_at + c->rom_room == p->pool_used) p->pool_used = c->rom_at;
    if (c->kind == CART_ROM) c->kind = CART_NONE;
    c->rom = false;
    c->autostart = false;
    c->rom_at = c->rom_room = 0;
}

uint8_t cart_window_byte(const coco_t *m, unsigned s, uint32_t i) {
    const cart_t *c = &m->port.slot[s];
    if (!c->rom) return 0xFFu;
    return m->cart[c->rom_at + (i & (c->rom_room - 1u))];
}

const uint8_t *cart_page(const coco_t *m, unsigned p) {
    const cart_t *c = &m->port.slot[m->port.cts];
    if (!c->rom) return NULL;
    uint32_t off = ((p - 0xC0u) * COCO_PAGE_SIZE) & (c->rom_room - 1u);
    return &m->cart[c->rom_at + off];
}

/* ---- the I/O page ------------------------------------------------------ */

/* Slot c's answer on SCS, or `bus`. */
static uint8_t COCO_HOT1(scs_read)(coco_t *m, cart_t *c, uint16_t a, uint8_t bus, uint64_t now) {
    switch (c->kind) {
    case CART_RSDOS:
        return rsdos_read(m, c, a, bus, now);
    case CART_SMARTWATCH:
        return (a & 0xFFu) >= SMARTWATCH_LOW ? smartwatch_read(&c->u.clock, a, bus) : bus;
    default:
        return bus;
    }
}

static void COCO_HOT1(scs_write)(coco_t *m, cart_t *c, uint16_t a, uint8_t v, uint64_t now) {
    if (c->kind == CART_RSDOS) rsdos_write(m, c, a, v, now);
}

uint8_t COCO_HOT1(cart_io_read)(coco_t *m, uint16_t a, uint8_t bus, uint64_t now) {
    cart_port_t *p = &m->port;
    if (is_scs(a & 0xFFu)) return scs_read(m, &p->slot[p->scs], a, bus, now);
    if (p->mpi && a == MPI_REG) return (uint8_t)(p->cts << 4 | p->scs);
    /* The rest of the page reaches every slot; no kind answers there yet
     * (M18: the sound cartridges). */
    return bus;
}

void COCO_HOT1(cart_io_write)(coco_t *m, uint16_t a, uint8_t v, uint64_t now) {
    cart_port_t *p = &m->port;
    if (is_scs(a & 0xFFu)) {
        scs_write(m, &p->slot[p->scs], a, v, now);
        return;
    }
    if (p->mpi && a == MPI_REG) {
        uint8_t cts = (uint8_t)((v >> 4) & 3u);
        p->switch_on = false;
        p->scs = v & 3u;
        if (cts != p->cts) {
            p->cts = cts;
            coco_cart_selected(m);
        }
    }
}

/* ---- RESET, events and lines ------------------------------------------ */

void cart_reset_select(coco_t *m) {
    cart_port_t *p = &m->port;
    p->switch_on = true;
    p->cts = p->scs = p->switch_slot;
}

void cart_reset(coco_t *m) {
    cart_port_t *p = &m->port;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_t *c = &p->slot[s];
        /* The SmartWatch keeps its state: the DS1216 does not see RESET. */
        if (c->kind != CART_RSDOS) c->nmi = c->halt = false;
    }
    if (p->rsdos_slot >= 0) rsdos_reset(m, &p->slot[p->rsdos_slot]);   /* sets the lines */
    else cart_lines(m);
}

void COCO_HOT1(cart_run)(coco_t *m, uint64_t now) {
    cart_port_t *p = &m->port;
    if (p->rsdos_slot >= 0) rsdos_run(m, &p->slot[p->rsdos_slot], now);
}

void COCO_HOT1(cart_lines)(coco_t *m) {
    const cart_port_t *p = &m->port;
    bool nmi = false, halt = false;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        nmi |= p->slot[s].nmi;
        halt |= p->slot[s].halt;
    }
    mc6809_set_nmi(&m->cpu, nmi);
    mc6809_set_halt(&m->cpu, halt);
}

void cart_restored(coco_t *m) {
    cart_port_t *p = &m->port;
    if (p->rsdos_slot >= 0) rsdos_lines(m, &p->slot[p->rsdos_slot]);
}

cart_t *cart_rsdos(coco_t *m) {
    return m->port.rsdos_slot >= 0 ? &m->port.slot[m->port.rsdos_slot] : NULL;
}

const cart_t *cart_rsdos_c(const coco_t *m) {
    return m->port.rsdos_slot >= 0 ? &m->port.slot[m->port.rsdos_slot] : NULL;
}

cart_t *cart_smartwatch(coco_t *m) {
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++)
        if (m->port.slot[s].kind == CART_SMARTWATCH) return &m->port.slot[s];
    return NULL;
}
