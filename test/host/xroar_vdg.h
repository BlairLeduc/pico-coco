/* xroar_vdg.h — XRoar's MC6847 and MC6883, driven field by field, for
 * the VDG oracle (test_vdg_oracle.c, design.md §15.2).
 *
 * The other side of the oracle sees only this header, so nothing of
 * XRoar's reaches this project's own code: xroar_vdg.c is compiled with
 * XRoar's include paths and linked with its mc6847.c, mc6883.c, the
 * fonts, events.c and portalib's delegate.c from the pinned checkout
 * (D2, D8), as xroar_cpu.c is for the CPU's.
 */
#ifndef PICO_COCO_TEST_XROAR_VDG_H
#define PICO_COCO_TEST_XROAR_VDG_H

#include <stdint.h>

/* The active area as XRoar's VDG emits it: 192 lines of 512 entries, two
 * to a pixel, each one of XRoar's VDG colour indices, which are
 * vdg_colour_t's first twelve in the same order. */
#define XV_LINES 192
#define XV_HALF_PIXELS 512

/* A field of a 64 KB machine with `ram` flat, as it is in the SAM's 64K
 * mode with 64K chips: the SAM's register set to `sam_reg` (its memory
 * size bits are forced to 64K), PIA1 port B driving `pb` (the VDG's mode
 * in bits 7-3, INT/EXT from GM0, as the CoCo is wired). Runs the VDG for
 * two whole fields from FS's rise and keeps the second's active area. */
void xv_field(const uint8_t *ram, uint16_t sam_reg, uint8_t pb,
              uint8_t out[XV_LINES][XV_HALF_PIXELS]);

/* XRoar's MC6847T1 in place of its MC6847, as its "-vdg-type 6847t1"
 * fits it: called before the first xv_field, as the part is made once. */
void xv_use_t1(void);

#endif /* PICO_COCO_TEST_XROAR_VDG_H */
