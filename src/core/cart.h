/* cart.h — what plugs into the cartridge port (design.md §12.8).
 *
 * The machine sees the port only through this: a cartridge is one of
 * the kinds below, and coco.c calls the functions here rather than any
 * kind's own. The kinds are an enumeration and their states a union,
 * dispatched by a switch, with no function pointers: a machine is copied
 * by coco_copy and written whole into a save state, and an enumeration
 * survives both where a pointer would not. Nothing here allocates.
 *
 * The lines a cartridge sees (§12.8):
 *   CTS   $C000-$FEFF, the ROM; its image is in coco_t.cart, the port's
 *         room for ROMs, which the Multi-Pak's slots will share (M17)
 *   SCS   $FF40-$FF5F, the I/O select
 *   the rest of the I/O page from $FF60, which a cartridge decodes for
 *   itself
 * and the lines it drives:
 *   CART  FIRQ through PIA1 CB1; a ROM's is tied to the Q clock (§12.2)
 *   NMI, HALT
 *
 * The kinds:
 *   CART_NONE   an empty port: $C000 and the I/O page read the bus
 *   CART_ROM    a ROM at $C000, which autostarts
 *   CART_RSDOS  the RS-DOS controller (rsdos.h): Disk BASIC's ROM, a
 *               WD1793 and a latch on SCS; its CART line is not on Q
 */
#ifndef PICO_COCO_CART_H
#define PICO_COCO_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "rsdos.h"

struct coco_s;

typedef enum {
    CART_NONE,
    CART_ROM,
    CART_RSDOS,
} cart_kind_t;

typedef struct cart_s {
    cart_kind_t kind;
    bool        rom;         /* an image at $C000, in coco_t.cart         */
    bool        autostart;   /* its CART line on the Q clock (§12.2)      */
    bool        nmi, halt;   /* the lines it drives, as it last set them  */
    union {
        rsdos_t rsdos;       /* CART_RSDOS                                */
    } u;
} cart_t;

/* The kind's chips set up, for coco_init, after the machine is cleared. */
void cart_init(struct coco_s *m);

/* An access to the I/O page from $FF40 to $FFBF at tick now; `bus` is
 * what an address nothing drives reads, and a read returns it unless the
 * cartridge answers. */
uint8_t cart_io_read(struct coco_s *m, uint16_t a, uint8_t bus, uint64_t now);
void    cart_io_write(struct coco_s *m, uint16_t a, uint8_t v, uint64_t now);

/* The RESET line. */
void cart_reset(struct coco_s *m);

/* The tick of the cartridge's next event, UINT64_MAX for none; the run
 * loop stops there and calls cart_run (design.md §6). */
static inline uint64_t cart_due(const cart_t *c) {
    return c->kind == CART_RSDOS ? c->u.rsdos.fdc.due : UINT64_MAX;
}

/* The cartridge's events up to tick now, and the lines they move. */
void cart_run(struct coco_s *m, uint64_t now);

/* The port's NMI and HALT onto the CPU, from what its cartridge drives. */
void cart_lines(struct coco_s *m);

/* The lines worked out again from the cartridge's state, after a state
 * is restored (coco_restored). */
void cart_restored(struct coco_s *m);

/* Whether CART is tied to the Q clock, so that BASIC starts it. */
static inline bool cart_on_q(const cart_t *c) {
    return c->rom && c->autostart;
}

/* The RS-DOS controller in the port, or NULL. */
cart_t       *cart_rsdos(struct coco_s *m);
const cart_t *cart_rsdos_c(const struct coco_s *m);

#endif /* PICO_COCO_CART_H */
