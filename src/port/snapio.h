/* snapio.h — our save states on the card, /coco/snaps/slotN.sav
 * (design.md §12.5, §12.6, §14).
 *
 * pico-oric's snapio with the CoCo's paths and media. Core 1 only, with
 * the guest parked (park.h) and the card mounted (the menu's), like all
 * card work. A save writes slotN.new, closes it, removes slotN.sav and
 * renames the new file into place: a rename alone is not proof of
 * power-loss atomicity (hardware-notes.md §7.1), so the recovery policy
 * is on the load side. A load checks slotN.sav whole — header, CRC,
 * machine, ROMs (snapshot.h) — and if it is missing or damaged falls
 * back to a whole slotN.new, which is what an interrupted publish
 * leaves. Nothing changes in the machine or the deck until a file has
 * passed.
 *
 * The two passes are two calls, so that a state for another machine can
 * be loaded into that machine (EL §8.5): snapio_check names it and
 * snapio_machine says whether the card can power on as it, the menu
 * powers on as it, and snapio_load loads the file the check passed.
 */
#ifndef PICO_COCO_SNAPIO_H
#define PICO_COCO_SNAPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"
#include "handoff.h"
#include "snapshot.h"

#define SNAPIO_SLOTS 4u
#define SNAPIO_DIR   "/coco/snaps"

/* *us is the wall time the save took. SNAP_BUSY, before the card is
 * touched, while a tape request waits. */
snap_status_t snapio_save(const coco_t *m, unsigned slot, uint32_t *us);

/* The first pass over the slot: fills *info as snapshot_check does, and
 * sets *recovered when the slot was its .new. SNAP_OK, or one that
 * snapshot_other_machine names, for a state that is whole; then
 * snapio_load may follow, into a machine powered on as snapio_machine
 * says for the second kind. The machine itself is not checked: one
 * waiting on the tape, which snapshot_load refuses, is powered on first
 * (menu.c). */
snap_status_t snapio_check(const coco_t *m, unsigned slot, snap_info_t *info, bool *recovered);

/* The machine the last checked state needs, from its info and media,
 * onto *mc, and its cartridge read into g_boot's when it has one: NULL,
 * or why the card cannot give it (a ROM missing or unknown, the
 * cartridge gone), and *st the refusal, SNAP_NO_ROM or SNAP_NO_CART.
 * A cartridge that fails to read leaves g_boot's as m runs it. */
const char   *snapio_machine(const coco_t *m, const snap_info_t *info, machine_t *mc,
                             snap_status_t *st);

/* The second pass, over the file the last snapio_check passed, and the
 * deck put back. *changed is set when it failed after the machine had
 * begun to change (SNAP_TORN), the card going or the file changing
 * between the passes: the machine is then part old, part new, and must
 * be powered on again rather than resumed. Any other refusal leaves it
 * as it was. */
snap_status_t snapio_load(coco_t *m, bool *changed);
bool          snapio_exists(unsigned slot);
bool          snapio_delete(unsigned slot);

/* What the menu should say about the last load's media: a tape that
 * has gone, which does not stop the load and leaves the deck empty; ""
 * for nothing. */
const char   *snapio_said(void);

#endif /* PICO_COCO_SNAPIO_H */
