/* jvc.h — JVC and DSK disk images (design.md §12.4, §18).
 *
 * The CoCo's sector images: an optional header, then every sector in
 * order, track by track and within a track side by side, sector by
 * sector. The header's length is the file's length mod 256, and what it
 * holds, byte by byte, with the default when it is too short to say:
 *
 *   0  sectors a track      18
 *   1  sides                1
 *   2  sector size code     1, 256 bytes (0 to 3: 128 to 1,024)
 *   3  first sector's ID    1
 *   4  sector attributes    0; anything else is refused
 *
 * A DSK is a JVC with no header: 18 sectors of 256 bytes on one side.
 * The tracks are as many as the data fills, the last perhaps short, which
 * reads as zeros past the file's end; a track beyond them is
 * unformatted, and a write to it grows the file (dskio.c). Those rules
 * are the community's, and XRoar's loader's, not yet held to a corpus
 * (§18: low).
 *
 * OS-9's disks are often DSKs of other shapes, two sides of 40 tracks
 * the commonest: 368,640 bytes, which is also one side of 80. A file
 * with no header whose first sector is OS-9's LSN0 is taken at its word,
 * as XRoar takes it by default (its vdisk.c): DD.SPT sectors a track
 * when DD.TKS says the same, DD.FMT's bit 0 for the sides, if DD.TOT's
 * sectors are at least the file's. The image is then an OS-9 one, and
 * keeps no header when it grows. One whose LSN0 asks for tracks too long
 * for a disk (mfm_layout_fits) is read as a plain DSK.
 *
 * A one-sided image whose second side is written becomes two-sided, as
 * a disk formatted on both sides is: jvc_two_sided gives its new shape,
 * every track's first side moved to its place and the second side zeros
 * until written, with the header that says so (two bytes, or longer if
 * it was), or none for an OS-9 image, as XRoar writes it.
 */
#ifndef PICO_COCO_JVC_H
#define PICO_COCO_JVC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mfmdisk.h"

typedef struct {
    uint8_t      header;   /* its length, 0 to 4                       */
    uint8_t      sides;    /* 1 or 2                                   */
    mfm_layout_t lay;      /* each side of a track                     */
    uint8_t      tracks;   /* tracks the data reaches, the last perhaps
                              short; up to COCO_DISK_TRACKS_MAX         */
    bool         os9;      /* no header, the shape read from LSN0       */
} jvc_geom_t;

/* The longest header read, and the bytes of a file's start jvc_parse
 * wants: the header, or LSN0 where there is none. */
#define JVC_HEADER_MAX 5u
#define JVC_HEAD_LEN   256u

/* The image of a file `size` bytes long whose first bytes are `head`
 * (head_len of them, at least the header's, and JVC_HEAD_LEN or the
 * whole file for OS-9's LSN0 to be looked at). NULL and g filled, or why
 * it is refused. */
const char *jvc_parse(const uint8_t *head, size_t head_len, uint32_t size, jvc_geom_t *g);

/* g's tracks for a file now `size` bytes long, its shape kept: false if
 * they would be more than COCO_DISK_TRACKS_MAX. */
bool jvc_resize(jvc_geom_t *g, uint32_t size);

/* The two-sided image a one-sided g becomes (above), its tracks the
 * same. */
jvc_geom_t jvc_two_sided(const jvc_geom_t *g);

/* Bytes in one side of a track. */
static inline uint32_t jvc_side_bytes(const jvc_geom_t *g) {
    return (uint32_t)g->lay.count * mfm_size(g->lay.n);
}

/* Where side h of track c starts in the file. */
static inline uint32_t jvc_offset(const jvc_geom_t *g, unsigned c, unsigned h) {
    return g->header + ((uint32_t)c * g->sides + h) * jvc_side_bytes(g);
}

/* A header for g, g->header bytes of it. */
void jvc_header(uint8_t *hdr, const jvc_geom_t *g);

#endif /* PICO_COCO_JVC_H */
