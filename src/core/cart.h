/* cart.h — what plugs into the cartridge port (design.md §12.8).
 *
 * The machine sees the port only through this: coco.c calls the
 * functions here rather than any kind's own. The kinds are an
 * enumeration and their states a union, dispatched by a switch, with no
 * function pointers: a machine is copied by coco_copy and written whole
 * into a save state, and an enumeration survives both where a pointer
 * would not. Nothing here allocates.
 *
 * The lines a cartridge sees (§12.8):
 *   CTS   $C000-$FEFF, the ROM
 *   SCS   $FF40-$FF5F, the I/O select
 *   the rest of the I/O page from $FF60, which a cartridge decodes for
 *   itself
 * and the lines it drives:
 *   CART  FIRQ through PIA1 CB1; a ROM's is tied to the Q clock (§12.2)
 *   NMI, HALT
 *
 * The port is four slots. Without a Multi-Pak only slot 0 is used, and
 * CTS and SCS both reach it. With one (Tandy's 26-3024, as XRoar's
 * mpi.c has it, §18):
 *   $FF7F write   bits 5-4 the CTS slot, bits 1-0 the SCS slot; the
 *                 front switch no longer counts
 *   $FF7F read    the register as written, the other bits clear
 *   CTS           to the CTS slot only;  SCS to the SCS slot only
 *   the rest of the I/O page  to every slot
 *   CART          from the CTS slot only;  NMI and HALT from any slot
 *   RESET         to every slot; both fields set to the switch's slot
 *
 * The kinds a slot holds:
 *   CART_NONE        empty: its lines read the bus
 *   CART_ROM         a ROM on CTS, which autostarts
 *   CART_RSDOS       the RS-DOS controller (rsdos.h): Disk BASIC's ROM, a
 *                    WD1793 and a latch on SCS; its CART line is not on
 *                    Q. One in the port at most: its chips are the
 *                    port's, `dk`, and a second is fitted empty
 *   CART_SMARTWATCH  a DS1216 on SCS at $FF58-$FF5F (smartwatch.h, §12.7)
 * CART_MPI is a Multi-Pak, and only ever the port's.
 *
 * The slots' ROMs are in coco_t.cart, the pool of COCO_CART_POOL bytes:
 * each in a power of two of room, at least a page, repeated through
 * the window as the chip ignores the address lines above it; an image
 * of any other size has the whole window, followed by $FF (§12.2).
 */
#ifndef PICO_COCO_CART_H
#define PICO_COCO_CART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "rsdos.h"
#include "smartwatch.h"

struct coco_s;

typedef enum {
    CART_NONE,
    CART_ROM,
    CART_RSDOS,
    CART_SMARTWATCH,
    CART_MPI,
    CART_KINDS
} cart_kind_t;

typedef struct cart_s {
    cart_kind_t kind;        /* never CART_MPI                            */
    bool        rom;         /* an image on CTS, at rom_at in the pool    */
    bool        autostart;   /* its CART line on the Q clock (§12.2)      */
    bool        nmi, halt;   /* the lines it drives, as it last set them  */
    uint32_t    rom_at;      /* its room in the pool: where, and how much */
    uint32_t    rom_room;
    union {
        smartwatch_t clock;  /* CART_SMARTWATCH                           */
    } u;
} cart_t;

typedef struct {
    bool     mpi;            /* a Multi-Pak: four slots, else slot 0      */
    uint8_t  cts, scs;       /* the slots selected, 0 to 3                */
    uint8_t  switch_slot;    /* the front switch, 0 to 3                  */
    bool     switch_on;      /* no write to $FF7F since RESET             */
    int8_t   rsdos_slot;     /* the RS-DOS controller's, or -1            */
    uint32_t pool_used;      /* of coco_t.cart                            */
    cart_t   slot[COCO_MPI_SLOTS];
    rsdos_t  dk;             /* the RS-DOS controller's chips             */
} cart_port_t;

/* The port's cartridges from the configuration (coco.h), for coco_init,
 * after the machine is cleared. */
void cart_init(struct coco_s *m);

/* A ROM image into slot `s` (0 without a Multi-Pak): an empty slot
 * becomes a CART_ROM, which autostarts; the RS-DOS controller's does
 * not. False if it does not fit the pool or the window. */
bool cart_load_rom(struct coco_s *m, unsigned s, const uint8_t *data, size_t len);

/* Slot s's ROM taken out; a CART_ROM leaves it empty. */
void cart_remove_rom(struct coco_s *m, unsigned s);

/* The byte the ROM window shows at offset i ($C000 + i) for slot s's
 * image, $FF with none, mirrored as the pages map it. */
uint8_t cart_window_byte(const struct coco_s *m, unsigned s, uint32_t i);

/* The pool's byte for page p ($C0-$FE) of the CTS slot's ROM, or NULL
 * with none, for the page table (coco.c). */
const uint8_t *cart_page(const struct coco_s *m, unsigned p);

/* An access to the I/O page from $FF40 to $FFBF at tick now; `bus` is
 * what an address nothing drives reads, and a read returns it unless a
 * cartridge answers. */
uint8_t cart_io_read(struct coco_s *m, uint16_t a, uint8_t bus, uint64_t now);
void    cart_io_write(struct coco_s *m, uint16_t a, uint8_t v, uint64_t now);

/* RESET's first part, before the page table is built again: the
 * Multi-Pak's slots back to its switch. */
void cart_reset_select(struct coco_s *m);

/* RESET's second part: each cartridge's own. */
void cart_reset(struct coco_s *m);

/* The tick of the port's next event, UINT64_MAX for none; the run loop
 * stops there and calls cart_run (design.md §6). */
static inline uint64_t cart_due(const cart_port_t *p) {
    return p->rsdos_slot >= 0 ? p->dk.fdc.due : UINT64_MAX;
}

/* The cartridges' events up to tick now, and the lines they move. */
void cart_run(struct coco_s *m, uint64_t now);

/* The port's NMI and HALT onto the CPU: any slot's. */
void cart_lines(struct coco_s *m);

/* The lines worked out again from the cartridges' state, after a state
 * is restored (coco_restored). */
void cart_restored(struct coco_s *m);

/* Whether the CTS slot's CART line is tied to the Q clock. */
static inline bool cart_on_q(const cart_port_t *p) {
    const cart_t *c = &p->slot[p->cts];
    return c->rom && c->autostart;
}

/* The RS-DOS controller's slot, or NULL. */
cart_t       *cart_rsdos(struct coco_s *m);
const cart_t *cart_rsdos_c(const struct coco_s *m);

/* The first SmartWatch's slot, or NULL. */
cart_t       *cart_smartwatch(struct coco_s *m);

/* The host's time to every SmartWatch fitted, given by the port before
 * each field (smartwatch_set_host). */
void cart_clock_host(struct coco_s *m, uint32_t seconds_since_2000);

/* Called by the port when the CTS slot changes: the page table and the
 * CART line again (coco.c). */
void coco_cart_selected(struct coco_s *m);

#endif /* PICO_COCO_CART_H */
