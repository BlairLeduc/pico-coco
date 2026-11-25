/* cart.c — the cartridge port and the Multi-Pak (cart.h, design.md §12.8). */

#include "cart.h"

#include <string.h>

#include "coco.h"
#include "hot.h"
#include "romset.h"
#include "sha1.h"

#define SCS_FIRST       0x40u    /* $FF40-$FF5F                            */
#define SCS_END         0x60u
#define MPI_REG         0xFF7Fu
#define SMARTWATCH_LOW  0x58u    /* $FF58-$FF5F on SCS (§12.7)             */
#define ORCH90_LEFT     0xFF7Au  /* §12.10                                 */
#define ORCH90_RIGHT    0xFF7Bu

static inline bool is_scs(unsigned off) { return off - SCS_FIRST < SCS_END - SCS_FIRST; }

/* ---- set-up ------------------------------------------------------------ */

void cart_init(coco_t *m) {
    cart_port_t *p = &m->port;
    const coco_config_t *cfg = &m->cfg;
    p->mpi = cfg->cart == CART_MPI;
    p->rsdos_slot = -1;
    p->ssc_slot = -1;
    p->ssc_roms = false;
    p->rs232_slot = -1;
    p->cart_low = false;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_kind_t k = CART_NONE;
        if (p->mpi) k = cfg->slot[s];
        else if (s == 0) k = cfg->cart;
        if (k == CART_MPI || k >= CART_KINDS) k = CART_NONE;   /* not in a slot */
        if (k == CART_RSDOS) {
            if (p->rsdos_slot >= 0) k = CART_NONE;   /* one controller (cart.h) */
            else p->rsdos_slot = (int8_t)s;
        }
        if (k == CART_SSC) {
            if (p->ssc_slot >= 0) k = CART_NONE;     /* one cartridge (cart.h) */
            else p->ssc_slot = (int8_t)s;
        }
        if (k == CART_RS232) {
            if (p->rs232_slot >= 0) k = CART_NONE;   /* one 6551 (cart.h) */
            else p->rs232_slot = (int8_t)s;
        }
        p->slot[s].kind = k;
        if (k == CART_SMARTWATCH) smartwatch_init(&p->slot[s].u.clock);
    }
    if (p->rsdos_slot >= 0) wd_init(&p->dk.fdc);
    if (p->rs232_slot >= 0) acia_init(&p->acia, 0);
    p->switch_slot = p->mpi ? (uint8_t)(cfg->mpi_switch & 3u) : 0u;
    p->switch_on = true;
    p->cts = p->scs = p->switch_slot;
}

/* ---- ROMs -------------------------------------------------------------- */

static bool pow2(size_t n) { return n && (n & (n - 1u)) == 0; }

uint32_t cart_room(size_t len) {
    if (len == 0 || len > COCO_CART_SIZE) return 0;
    if (!pow2(len)) return COCO_CART_SIZE;
    return len < COCO_PAGE_SIZE ? COCO_PAGE_SIZE : (uint32_t)len;
}

void cart_pool_finish(uint8_t *room, size_t len) {
    uint32_t r = cart_room(len);
    if (!r) return;
    if (pow2(len)) {
        for (uint32_t k = (uint32_t)len; k < r; k += (uint32_t)len) memcpy(room + k, room, len);
    } else {
        memset(room + len, 0xFF, r - len);
    }
}

static bool is_image(const uint8_t *h, size_t len, rom_id_t id) {
    return len == romset_images[id].size && memcmp(h, romset_images[id].sha1, SHA1_DIGEST_LEN) == 0;
}

/* Slot s's bookkeeping for an image of len bytes already whole at `at`:
 * an empty slot becomes a ROM, or an Orchestra-90 or the RS-232 pak by
 * its SHA-1; one the configuration fitted stays one, whatever its ROM. */
static void take(coco_t *m, cart_t *c, uint32_t at, uint32_t room, size_t len) {
    cart_port_t *p = &m->port;
    int8_t s = (int8_t)(c - p->slot);
    c->rom_at = at;
    c->rom_room = room;
    c->rom = true;
    if (c->kind == CART_NONE || c->kind == CART_ROM) {
        uint8_t h[SHA1_DIGEST_LEN];
        sha1(&m->cart[at], len, h);
        /* The Orchestra-90's ROM, Tandy's 26-3143 (§12.10); the Deluxe
         * RS-232 Program Pak's, 26-2226, if the port has no 6551 yet
         * (§12.11). */
        if (is_image(h, len, ROM_ORCH90)) {
            c->kind = CART_ORCH90;
        } else if (is_image(h, len, ROM_RS232) && p->rs232_slot < 0) {
            c->kind = CART_RS232;
            p->rs232_slot = s;
            acia_init(&p->acia, coco_ticks(m));
        } else {
            c->kind = CART_ROM;
        }
    }
    /* Both tie CART to the Q clock (§12.2; MAME's coco_orch90.cpp); the
     * RS-232 pak's is its 6551's /IRQ (MAME's coco_rs232.cpp). */
    c->autostart = c->kind == CART_ROM || c->kind == CART_ORCH90;
    if (at + room > p->pool_used) p->pool_used = at + room;
}

bool cart_rom_placed(coco_t *m, unsigned s, uint32_t at, size_t len) {
    uint32_t room = cart_room(len);
    if (s >= COCO_MPI_SLOTS || !room || at > COCO_CART_POOL - room) return false;
    take(m, &m->port.slot[s], at, room, len);
    return true;
}

bool cart_load_rom(coco_t *m, unsigned s, const uint8_t *data, size_t len) {
    cart_port_t *p = &m->port;
    uint32_t room = cart_room(len);
    if (s >= COCO_MPI_SLOTS || !room) return false;
    cart_t *c = &p->slot[s];
    uint32_t at;
    if (c->rom && c->rom_room >= room) {
        at = c->rom_at;   /* its own room again */
    } else {
        if (c->rom && c->rom_at + c->rom_room == p->pool_used) p->pool_used = c->rom_at;
        if (p->pool_used + room > COCO_CART_POOL) return false;
        at = p->pool_used;
    }
    memcpy(&m->cart[at], data, len);
    cart_pool_finish(&m->cart[at], len);
    take(m, c, at, room, len);
    return true;
}

void cart_remove_rom(coco_t *m, unsigned s) {
    cart_port_t *p = &m->port;
    if (s >= COCO_MPI_SLOTS) return;
    cart_t *c = &p->slot[s];
    if (c->rom && c->rom_at + c->rom_room == p->pool_used) p->pool_used = c->rom_at;
    if (c->kind == CART_RS232) p->rs232_slot = -1;
    if (c->kind == CART_ROM || c->kind == CART_ORCH90 || c->kind == CART_RS232) c->kind = CART_NONE;
    memset(&c->u, 0, sizeof c->u);
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

static inline const uint8_t *ssc_rom(const coco_t *m) { return &m->cart[m->port.ssc_rom_at]; }
static inline const uint8_t *ssc_al2(const coco_t *m) { return &m->cart[m->port.ssc_al2_at]; }

uint8_t COCO_HOT1(cart_io_read)(coco_t *m, uint16_t a, uint8_t bus, uint64_t now) {
    cart_port_t *p = &m->port;
    if (is_scs(a & 0xFFu)) return scs_read(m, &p->slot[p->scs], a, bus, now);
    if (p->mpi && a == MPI_REG) return (uint8_t)(p->cts << 4 | p->scs);
    /* The rest of the page reaches every slot: the Speech/Sound
     * Cartridge's two addresses. The Orchestra-90's DACs are written
     * only. */
    if ((a == SSC_RESET || a == SSC_DATA) && cart_ssc_live(p))
        return ssc_read(&p->ssc, ssc_rom(m), ssc_al2(m), a, now);
    if (a - ACIA_FIRST <= ACIA_LAST - ACIA_FIRST && p->rs232_slot >= 0) {
        uint8_t v = acia_read(&p->acia, a, now);
        coco_cart_line(m, now);   /* a status read clears IRQ */
        return v;
    }
    return bus;
}

bool COCO_HOT1(cart_io_write)(coco_t *m, uint16_t a, uint8_t v, uint64_t now) {
    cart_port_t *p = &m->port;
    if (is_scs(a & 0xFFu)) {
        scs_write(m, &p->slot[p->scs], a, v, now);
        return false;
    }
    if ((a == SSC_RESET || a == SSC_DATA) && cart_ssc_live(p)) {
        ssc_write(&p->ssc, ssc_rom(m), ssc_al2(m), a, v, now);
        return false;   /* its sound reaches the bus by itself */
    }
    if (a - ACIA_FIRST <= ACIA_LAST - ACIA_FIRST && p->rs232_slot >= 0) {
        acia_write(&p->acia, a, v, now);
        coco_cart_line(m, now);
        return false;
    }
    if (a == ORCH90_LEFT || a == ORCH90_RIGHT) {
        /* Every slot sees the rest of the page (§12.8). */
        bool moved = false;
        for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
            cart_t *c = &p->slot[s];
            if (c->kind != CART_ORCH90) continue;
            uint8_t *dac = a == ORCH90_LEFT ? &c->u.orch.left : &c->u.orch.right;
            moved |= *dac != v;
            *dac = v;
        }
        return moved;
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
    return false;
}

uint32_t cart_beside(const coco_t *m) {
    uint32_t sum = 0;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const cart_t *c = &m->port.slot[s];
        if (c->kind == CART_ORCH90) sum += (uint32_t)c->u.orch.left + c->u.orch.right;
    }
    if (!sum) return 0;
    /* (L + R) / 2 over 255 of the full scale: a lone DAC at 255 is half
     * of it. */
    uint32_t level = sum * (COCO_SOUND_LEVEL_MAX / 2u) / 255u;
    return level > COCO_SOUND_LEVEL_MAX ? COCO_SOUND_LEVEL_MAX : level;
}

int32_t cart_side(const coco_t *m) {
    int32_t diff = 0;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const cart_t *c = &m->port.slot[s];
        if (c->kind == CART_ORCH90) diff += (int32_t)c->u.orch.left - (int32_t)c->u.orch.right;
    }
    /* Rounded to the nearest, so that each ear, the mono and this
     * together, stays within a level of its own (pcm.h). */
    int32_t v = diff * (int32_t)(COCO_SOUND_LEVEL_MAX / 2u);
    return (v + (v < 0 ? -127 : 127)) / 255;
}

/* ---- RESET, events and lines ------------------------------------------ */

void cart_reset_select(coco_t *m) {
    cart_port_t *p = &m->port;
    p->switch_on = true;
    p->cts = p->scs = p->switch_slot;
}

void cart_ssc_run(coco_t *m, uint64_t now) {
    if (cart_ssc_live(&m->port)) ssc_run(&m->port.ssc, ssc_rom(m), ssc_al2(m), now);
}

static bool ssc_take(coco_t *m, uint32_t rom_at, uint32_t al2_at) {
    cart_port_t *p = &m->port;
    if (p->ssc_slot < 0) return false;
    if (rom_at > COCO_CART_POOL - TMS7000_ROM_SIZE || al2_at > COCO_CART_POOL - SP0256_ROM_SIZE)
        return false;
    p->ssc_rom_at = rom_at;
    p->ssc_al2_at = al2_at;
    p->ssc_roms = true;
    uint32_t end = (rom_at > al2_at ? rom_at + TMS7000_ROM_SIZE : al2_at + SP0256_ROM_SIZE);
    if (end > p->pool_used) p->pool_used = end;
    return true;
}

bool cart_load_ssc(coco_t *m, const uint8_t *rom, const uint8_t *al2) {
    cart_port_t *p = &m->port;
    if (p->ssc_slot < 0) return false;
    uint32_t at = p->ssc_roms ? p->ssc_rom_at : p->pool_used;
    if (at > COCO_CART_POOL - TMS7000_ROM_SIZE - SP0256_ROM_SIZE) return false;
    memcpy(&m->cart[at], rom, TMS7000_ROM_SIZE);
    memcpy(&m->cart[at + TMS7000_ROM_SIZE], al2, SP0256_ROM_SIZE);
    return ssc_take(m, at, at + TMS7000_ROM_SIZE);
}

bool cart_ssc_placed(coco_t *m, uint32_t rom_at, uint32_t al2_at) {
    return ssc_take(m, rom_at, al2_at);
}

void cart_reset(coco_t *m) {
    cart_port_t *p = &m->port;
    /* RESET reaches the Speech/Sound Cartridge's chips, all of them, as
     * MAME resets its devices. */
    if (cart_ssc_live(p)) {
        ssc_init(&p->ssc, ssc_rom(m), coco_ticks(m));
        p->ssc.attached = true;
        ay8912_set_average(&p->ssc.ay, m->pcm.num, m->pcm.den * SSC_TICKS_PER_AY);
    }
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_t *c = &p->slot[s];
        /* The SmartWatch keeps its state: the DS1216 does not see RESET. */
        if (c->kind != CART_RSDOS) c->nmi = c->halt = false;
    }
    if (p->rsdos_slot >= 0) rsdos_reset(m, &p->slot[p->rsdos_slot]);   /* sets the lines */
    else cart_lines(m);
    /* RESET reaches the 6551's RES: the hardware reset, its /IRQ high.
     * A CART line not on Q is high, pulled up, whichever slot is CTS,
     * so the 6551's first interrupt is an edge once it reaches CB1. */
    if (p->rs232_slot >= 0) {
        acia_init(&p->acia, coco_ticks(m));
        p->cart_low = false;
        if (!cart_on_q(p)) m->pia1.b.c1 = true;
    }
}

void COCO_HOT1(cart_run)(coco_t *m, uint64_t now) {
    cart_port_t *p = &m->port;
    if (p->rsdos_slot >= 0) rsdos_run(m, &p->slot[p->rsdos_slot], now);
    if (p->rs232_slot >= 0 && p->acia.due <= now) {
        acia_run(&p->acia, now);
        coco_cart_line(m, now);
    }
}

bool cart_serial_put(coco_t *m, uint8_t c, uint64_t now) {
    cart_port_t *p = &m->port;
    if (p->rs232_slot < 0) return false;
    return acia_rx_put(&p->acia, c, now);
}

int cart_serial_get(coco_t *m) {
    cart_port_t *p = &m->port;
    return p->rs232_slot >= 0 ? acia_tx_get(&p->acia) : -1;
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
    p->cart_low = cart_line_low(p);   /* PIA1's CB1 is in the state */
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

void cart_clock_host(coco_t *m, uint32_t seconds_since_2000) {
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++)
        if (m->port.slot[s].kind == CART_SMARTWATCH)
            smartwatch_set_host(&m->port.slot[s].u.clock, seconds_since_2000);
}
