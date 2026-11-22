/* mfmdisk.h — raw MFM tracks, as the WD1793 sees them (design.md §12.4),
 * from pico-oric's mfmdisk.h.
 *
 * A track is COCO_DISK_TRACK_LEN bytes after decoding: gaps, the sync
 * marks as A1 bytes, ID fields (A1 A1 A1 FE, track, side, sector, size,
 * CRC) and data fields (A1 A1 A1 FB, or F8 deleted, the data and its
 * CRC). The marks are found as pico-oric finds them, walking the track
 * and stepping over each data field, so data bytes that happen to look
 * like a mark are never taken for one.
 *
 * The card holds sector images (jvc.h), not tracks: a track is built from
 * a side's sectors when the controller asks for it, and its sectors are
 * taken back out when it has been written (mfm_build_track,
 * mfm_extract_track). The layout built is RS-DOS's own, DSKINI's, read
 * off its Write Track stream on the host (mfm_build_track; test_disk).
 */
#ifndef PICO_COCO_MFMDISK_H
#define PICO_COCO_MFMDISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* The byte an unformatted track holds throughout, and a gap's. */
#define MFM_UNFORMATTED 0x4Eu

/* The WD1793's CRC: CCITT, x^16 + x^12 + x^5 + 1, from $FFFF over the
 * marks' three A1s, the mark and the field. */
uint16_t mfm_crc(uint16_t crc, uint8_t b);
uint16_t mfm_crc_block(uint16_t crc, const uint8_t *p, size_t n);

/* One sector a track holds: where its ID mark (the FE) and its data
 * mark (FB or F8) are, as byte positions in the track, and the ID. */
typedef struct {
    uint16_t id;       /* the FE                                     */
    uint16_t data;     /* the FB or F8; MFM_NO_DATA if none follows   */
    uint8_t  c, h, r, n;
} mfm_sector_t;

#define MFM_NO_DATA 0xFFFFu

/* A data mark further than this past its ID's CRC belongs to no ID: the
 * WD1793 gives up after 43 bytes in MFM. */
#define MFM_DAM_WINDOW 43u

/* Bytes in a sector of size code n (128, 256, 512 or 1024). */
static inline uint16_t mfm_size(uint8_t n) { return (uint16_t)(128u << (n & 3u)); }

/* The sectors of one raw track, in the order they pass the head, up to
 * max. Returns how many. */
unsigned mfm_index(const uint8_t *trk, mfm_sector_t *out, unsigned max);

/* The shape of the sectors a track is built from: `count` sectors of size
 * code `n`, numbered from `first`. */
typedef struct {
    uint8_t count, n, first;
} mfm_layout_t;

/* Will a track of this shape fit, in RS-DOS's layout? */
bool mfm_layout_fits(mfm_layout_t lay);

/* Track c, side h, formatted as DSKINI formats it, with each sector's
 * data from `data`, the sectors in order of number, size bytes each;
 * `have` bytes of it are there and the rest read as zeros, for an image
 * cut short (jvc.h). Every CRC is good. The layout must fit. */
void mfm_build_track(uint8_t *trk, mfm_layout_t lay, uint8_t c, uint8_t h, const uint8_t *data,
                     uint32_t have);

/* The sectors of the shape `lay` found on a written track, their data
 * copied into `data` in order of number, size bytes each: an ID of that
 * number and size with a data field. got[i] says whether sector first + i
 * was found; the bytes of one that was not are left. The ID's track and
 * side are not compared, as the card's image has no room for another.
 * Returns how many were found. */
unsigned mfm_extract_track(const uint8_t *trk, mfm_layout_t lay, uint8_t *data, bool *got);

#endif /* PICO_COCO_MFMDISK_H */
