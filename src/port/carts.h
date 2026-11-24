/* carts.h — what is in the cartridge port, as the card and the settings
 * name it and as the machine's pool holds it (design.md §12.8).
 *
 * One description, port_media_t, for the boot, the Machine page and
 * snapshots: a cartridge, or a Multi-Pak and its four slots. Each slot
 * is named as the settings file writes it: "" empty, "rsdos" the RS-DOS
 * controller, "smartwatch" the SmartWatch (§12.7), else a ROM image in
 * /coco/carts/. Core 1 reads the ROMs off the card straight into the
 * machine's pool, coco_t.cart, with the guest parked, and records where
 * each went; core 0's power-on, after coco_init, which leaves the pool,
 * registers them there (coco_slot_rom_placed). There is no second copy.
 */
#ifndef PICO_COCO_CARTS_H
#define PICO_COCO_CARTS_H

#include <stdbool.h>
#include <stdint.h>

#include "card.h"
#include "coco.h"
#include "settings.h"

#define CARTS_MPI        "mpi"
#define CARTS_SMARTWATCH "smartwatch"
/* Why a ROM that fits the window does not fit what the pool has left. */
#define CARTS_NO_ROOM    "NO ROOM: 32K FOR ALL"

typedef struct {
    char     name[COCO_PATH_MAX];   /* as above; "" for an empty slot    */
    uint32_t at, len;               /* its ROM in the pool; len 0: none  */
} slot_media_t;

typedef struct {
    bool         mpi;               /* a Multi-Pak; else slot 0 is the port */
    uint8_t      mpi_switch;        /* its front switch, 0 to 3          */
    slot_media_t slot[COCO_MPI_SLOTS];
} port_media_t;

/* The machine's pool, for core 1 to read ROMs into while the guest is
 * parked (main.c). */
extern uint8_t *const g_cart_pool;

/* What a name is. */
cart_kind_t carts_kind(const char *name);

/* The port as the settings name it: cart, and with "mpi" its slots and
 * switch. Nothing is read. */
void carts_from_settings(port_media_t *pm, const settings_t *st);

/* One cartridge in the port, by name ("" for none). */
void carts_single(port_media_t *pm, const char *name);

/* Every slot's ROM read off the card into the pool, in slot order, on
 * core 1 with the guest parked. NULL, or why the first that failed did,
 * naming it; a slot that failed is emptied, and the others load. */
const char *carts_load(port_media_t *pm, const card_job_t *job);

/* Whether a slot holds this kind, and which. */
bool carts_has(const port_media_t *pm, cart_kind_t k);

/* Every RS-DOS controller taken out, where Disk BASIC cannot run
 * (roms_rsdos_why): true if one was. */
bool carts_drop_rsdos(port_media_t *pm);

/* Into the machine's configuration, before coco_init; and the ROMs
 * registered where they lie, after it: false if one would not go,
 * which carts_load's rooms rule out. Either core: no logging. */
void carts_config(const port_media_t *pm, coco_config_t *cfg);
bool carts_place(const port_media_t *pm, coco_t *m);

/* The name the status line and the log give: the cartridge, or the
 * slot the switch starts, "MULTI-PAK" if it is empty. */
const char *carts_label(const port_media_t *pm);

/* Into the settings to save: cart, slots and switch. */
void carts_to_settings(const port_media_t *pm, settings_t *st);

#endif /* PICO_COCO_CARTS_H */
