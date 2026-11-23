/* dskio.h — disk images in /coco/disks/ on the card (design.md §12.4,
 * §12.6), from pico-oric's discio.h.
 *
 * The WD1793 in the core knows each drive's type and holds one raw
 * track; when a command needs another, or has written to the one it
 * holds, it posts a request (wd1793.h) and waits, busy, as a drive does
 * while its head settles. This serves it from the image file: the
 * written track's sectors put back, then the wanted track built from the
 * image's sectors (jvc.h, mfmdisk.h), or an unformatted one where the
 * image has none. Core 1 only, with core 0 parked (park.h): card latency
 * can outlast both the field and the audio deadline (hardware-notes.md
 * §7.1).
 *
 * The drives are the port's, not the machine's: which file is in each
 * and whether it is protected stay across a power-on, the Machine page's
 * restart and a snapshot's load, and dskio_attach puts them back into a
 * machine that has just been built. Images are JVC, .dsk or .jvc; one
 * that is not is refused by name. A file with the read-only attribute is
 * a write-protected disk. A track written beyond the image's end grows
 * the file, the tracks between filled with zeros; a side the image does
 * not have keeps nothing, and the log says so.
 */
#ifndef PICO_COCO_DSKIO_H
#define PICO_COCO_DSKIO_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"
#include "config.h"
#include "dirlist.h"
#include "jvc.h"
#include "snapshot.h"

#define DSKIO_DIR COCO_DISK_DIR

/* The request coco_disk_request names, put and got: mounts the card,
 * does the work and unmounts. False if the card failed it, in which case
 * the track read is unformatted and the track written is lost
 * (wd_served). The time taken is in *us. */
bool dskio_serve(coco_t *m, uint32_t *us);

/* The folders and images in `dir`, DSKIO_DIR or one inside it, into l,
 * sorted (dirlist.h): as many as it holds, l->total counting them all.
 * Hidden and system entries, macOS's "._" shadows and other files are
 * not listed. False if the folder could not be opened. The card must be
 * mounted (storage.h). */
bool dskio_list(const char *dir, dirlist_t *l);

/* A written track the controller has in hand put back now, as before a
 * power-on or a state saved (wd_flush). The card must be mounted, and
 * core 0 parked. */
void dskio_flush(coco_t *m);

/* Put an image in a drive, 0 to 3; NULL or "" empties it. An image in
 * another drive is refused, as a disk is in one drive at a time. The card must
 * be mounted, and core 0 parked or not yet running. m is the machine to
 * tell, or NULL at boot, before it exists. A track the drive has written
 * and not yet put back goes first. A bare name is in DSKIO_DIR, as the
 * settings file writes it. Returns NULL, or why the disk did not go in,
 * and the drive is then empty. */
const char *dskio_insert(coco_t *m, unsigned drive, const char *path);

/* Would this image go in a drive? As dskio_insert judges it, changing
 * nothing: a save state's disks are checked before the load (snapio.h).
 * NULL, or why not. */
const char *dskio_probe(const char *path, jvc_geom_t *g, bool *protect);
const char *dskio_inserted(unsigned drive);   /* "" when none           */
bool        dskio_protected(unsigned drive);

/* The drives into a machine just powered on (core0_power_on). */
void dskio_attach(coco_t *m);

/* A new disk for a drive, name.dsk in `dir` (DSKIO_DIR or a folder in
 * it): empty, no tracks at all, for DSKINI to format, as a new floppy
 * is; then put in that drive. name is the menu's, up to
 * COCO_MEDIA_NAME_MAX characters; a disk of that name already there is
 * refused. The card must be mounted, and core 0 parked. NULL, or why
 * not. dskio_next_name gives the menu its first offer for `dir`, DISKnn,
 * the first number free; "" if none is. */
const char *dskio_new(coco_t *m, unsigned drive, const char *dir, const char *name);
void        dskio_next_name(const char *dir, char out[COCO_MEDIA_NAME_MAX + 1]);

/* The media a state carries (snapshot.h): each drive's path. */
void dskio_media(snap_media_t *md);

/* Tracks read and written since boot, and written sectors no image could
 * keep, for the heartbeat. */
uint32_t dskio_tracks(bool written);
uint32_t dskio_lost(void);

#endif /* PICO_COCO_DSKIO_H */
