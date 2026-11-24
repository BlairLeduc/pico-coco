/* carts.c — the cartridge port on the card and in the pool (carts.h,
 * design.md §12.8). */

#include "carts.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "log.h"
#include "roms.h"
#include "romset.h"

cart_kind_t carts_kind(const char *name) {
    if (!name[0]) return CART_NONE;
    if (card_is_rsdos(name)) return CART_RSDOS;
    if (strcasecmp(name, CARTS_SMARTWATCH) == 0) return CART_SMARTWATCH;
    if (strcasecmp(name, CARTS_MPI) == 0) return CART_MPI;
    return CART_ROM;
}

static void name_slot(slot_media_t *sm, const char *name) {
    memset(sm, 0, sizeof *sm);
    snprintf(sm->name, sizeof sm->name, "%s", name);
}

void carts_single(port_media_t *pm, const char *name) {
    memset(pm, 0, sizeof *pm);
    if (carts_kind(name) == CART_MPI) return;
    name_slot(&pm->slot[0], name);
}

void carts_from_settings(port_media_t *pm, const settings_t *st) {
    if (carts_kind(st->cart) != CART_MPI) {
        carts_single(pm, st->cart);
        return;
    }
    memset(pm, 0, sizeof *pm);
    pm->mpi = true;
    pm->mpi_switch = (uint8_t)((st->mpi_switch ? st->mpi_switch : COCO_MPI_SLOTS) - 1u);
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        /* A Multi-Pak in a slot is not a slot's (cart.h). */
        if (carts_kind(st->slot[s]) != CART_MPI) name_slot(&pm->slot[s], st->slot[s]);
    }
}

const char *carts_load(port_media_t *pm, const card_job_t *job) {
    static char why[COCO_TEXT_COLS + 1];
    const char *first = NULL;
    uint32_t at = 0;
    bool rsdos = false;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        slot_media_t *sm = &pm->slot[s];
        sm->at = sm->len = 0;
        cart_kind_t k = carts_kind(sm->name);
        if (k != CART_ROM && k != CART_RSDOS) continue;
        const char *err = NULL;
        uint32_t len = 0;
        if (k == CART_RSDOS && rsdos) {
            err = "ONE RS-DOS AT MOST";   /* cart.h */
        } else if (k == CART_RSDOS) {
            if (COCO_CART_POOL - at < romset_images[ROM_COCO_DISK11].size) err = "NO ROOM";
            else err = card_disk_rom_load(job, g_cart_pool + at, &len);
            rsdos = err == NULL;
        } else {
            err = card_cart_load(sm->name, g_cart_pool + at, COCO_CART_POOL - at, &len);
        }
        uint32_t room = err ? 0 : cart_room(len);
        if (!err && (!room || room > COCO_CART_POOL - at)) err = "NO ROOM IN THE MULTI-PAK";
        if (err) {
            log_core1("  cart         : %s%u %s: %s; empty\n", pm->mpi ? "slot " : "",
                      pm->mpi ? s + 1u : 0u, sm->name, err);
            if (!first) {
                snprintf(why, sizeof why, "%.12s: %.18s", sm->name, err);
                first = why;
            }
            name_slot(sm, "");
            continue;
        }
        cart_pool_finish(g_cart_pool + at, len);
        sm->at = at;
        sm->len = len;
        at += room;
    }
    return first;
}

bool carts_has(const port_media_t *pm, cart_kind_t k) {
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++)
        if (carts_kind(pm->slot[s].name) == k) return true;
    return false;
}

bool carts_drop_rsdos(port_media_t *pm) {
    bool any = false;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        if (carts_kind(pm->slot[s].name) != CART_RSDOS) continue;
        name_slot(&pm->slot[s], "");
        any = true;
    }
    return any;
}

void carts_config(const port_media_t *pm, coco_config_t *cfg) {
    if (pm->mpi) {
        cfg->cart = CART_MPI;
        cfg->mpi_switch = pm->mpi_switch;
        for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
            cart_kind_t k = carts_kind(pm->slot[s].name);
            /* A ROM that did not load leaves its slot empty. */
            cfg->slot[s] = k == CART_ROM && !pm->slot[s].len ? CART_NONE : k;
        }
    } else {
        cart_kind_t k = carts_kind(pm->slot[0].name);
        cfg->cart = k == CART_RSDOS && pm->slot[0].len ? CART_RSDOS : CART_NONE;
    }
}

bool carts_place(const port_media_t *pm, coco_t *m) {
    bool ok = true;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const slot_media_t *sm = &pm->slot[s];
        if (sm->len) ok &= coco_slot_rom_placed(m, s, sm->at, sm->len);
    }
    return ok;
}

static const char *base_of(const char *p) {
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

const char *carts_label(const port_media_t *pm) {
    const slot_media_t *sm = &pm->slot[pm->mpi ? pm->mpi_switch : 0];
    switch (carts_kind(sm->name)) {
    case CART_NONE:       return pm->mpi ? "MULTI-PAK" : "";
    case CART_RSDOS:      return "RS-DOS";
    case CART_SMARTWATCH: return "SMARTWATCH";
    default:              return base_of(sm->name);
    }
}

void carts_to_settings(const port_media_t *pm, settings_t *st) {
    if (!pm->mpi) {
        settings_card_name(SETTINGS_CART_DIR, pm->slot[0].name, st->cart);
        return;
    }
    snprintf(st->cart, sizeof st->cart, "%s", CARTS_MPI);
    st->mpi_switch = pm->mpi_switch + 1u;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++)
        settings_card_name(SETTINGS_CART_DIR, pm->slot[s].name, st->slot[s]);
}
