/* disk_util.h — disks held in memory for the host tests, served as the
 * port serves the card's (design.md §12.4, rsdos.h), from pico-oric's
 * disc_util.h.
 *
 * A disk is a whole JVC or DSK file in a buffer; a request puts a
 * track's sectors back into it and builds the controller's track from
 * it, as dskio.c does from a file: a track the image does not reach is
 * unformatted, a write to it grows the image, and a one-sided image
 * whose second side is written becomes two-sided (jvc.h). Header-only,
 * as snap_util.h is.
 */
#ifndef PICO_COCO_TEST_DISK_UTIL_H
#define PICO_COCO_TEST_DISK_UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coco.h"
#include "jvc.h"
#include "mfmdisk.h"
#include "rsdos.h"
#include "wd1793.h"

typedef struct {
    uint8_t   *img;
    uint32_t   len;
    jvc_geom_t g;
    bool       protect;
    uint32_t   gets, puts, lost;
} host_disk_t;

/* An empty DSK: 18 sectors of 256 bytes, one side, no tracks, as the
 * Disks page makes a new one, for DSKINI to format. */
static inline void host_disk_new(host_disk_t *d) {
    memset(d, 0, sizeof *d);
    (void)jvc_parse(NULL, 0, 0, &d->g);
}

/* A formatted-looking DSK of `tracks` tracks, every byte `fill`. */
static inline void host_disk_blank(host_disk_t *d, unsigned tracks, uint8_t fill) {
    host_disk_new(d);
    d->len = tracks * jvc_side_bytes(&d->g);
    d->img = (uint8_t *)malloc(d->len);
    memset(d->img, fill, d->len);
    (void)jvc_resize(&d->g, d->len);
}

static inline void host_disk_free(host_disk_t *d) {
    free(d->img);
    memset(d, 0, sizeof *d);
}

static inline void host_disk_copy(host_disk_t *dst, const host_disk_t *src) {
    *dst = *src;
    dst->img = (uint8_t *)malloc(src->len ? src->len : 1u);
    if (src->len) memcpy(dst->img, src->img, src->len);
}

/* An image file; NULL, or why it was refused. */
static inline const char *host_disk_load(host_disk_t *d, const char *path) {
    memset(d, 0, sizeof *d);
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d->img = (uint8_t *)malloc((size_t)n + 1u);
    d->len = (uint32_t)n;
    bool ok = fread(d->img, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) return "cannot read";
    return jvc_parse(d->img, d->len, d->len, &d->g);
}

static inline bool host_disk_save(const host_disk_t *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(d->img, 1, d->len, f) == d->len;
    return fclose(f) == 0 && ok;
}

/* Side h of track c into trk, from the image (dskio.c's rule). */
static inline void host_disk_get(const host_disk_t *d, unsigned c, unsigned h, uint8_t *trk) {
    if (c >= d->g.tracks || h >= d->g.sides) {
        memset(trk, MFM_UNFORMATTED, COCO_DISK_TRACK_LEN);
        return;
    }
    uint32_t at = jvc_offset(&d->g, c, h);
    uint32_t have = d->len > at ? d->len - at : 0u;
    mfm_build_track(trk, d->g.lay, (uint8_t)c, (uint8_t)h, d->img + at, have);
}

/* A one-sided image made two-sided (jvc.h), as dskio.c's widen does on
 * the card: a new image, built afresh. */
static inline void host_disk_widen(host_disk_t *d) {
    jvc_geom_t w = jvc_two_sided(&d->g);
    uint32_t n = jvc_side_bytes(&d->g), len = w.header + (uint32_t)d->g.tracks * 2u * n;
    uint8_t *img = (uint8_t *)calloc(len ? len : 1u, 1);
    jvc_header(img, &w);
    for (unsigned c = 0; c < d->g.tracks; c++) {
        uint32_t at = jvc_offset(&d->g, c, 0), have = d->len > at ? d->len - at : 0u;
        memcpy(img + jvc_offset(&w, c, 0), d->img + at, have < n ? have : n);
    }
    free(d->img);
    d->img = img;
    d->len = len;
    d->g = w;
    (void)jvc_resize(&d->g, d->len);
}

/* A written track's sectors back into the image, growing it as far as
 * the track; false if nothing could be kept. */
static inline bool host_disk_put(host_disk_t *d, unsigned c, unsigned h, const uint8_t *trk) {
    if (h >= 2u) return false;
    static uint8_t data[COCO_DISK_TRACK_DATA_MAX];
    bool got[COCO_DISK_SECTORS_MAX];
    unsigned n = mfm_extract_track(trk, d->g.lay, data, got);
    if (n == 0) return false;
    if (h >= d->g.sides) host_disk_widen(d);
    uint32_t at = jvc_offset(&d->g, c, h), size = mfm_size(d->g.lay.n);
    uint32_t end = at + jvc_side_bytes(&d->g);
    if (end > d->len) {
        d->img = (uint8_t *)realloc(d->img, end);
        memset(d->img + d->len, 0, end - d->len);
        d->len = end;
        (void)jvc_resize(&d->g, d->len);
    }
    for (unsigned i = 0; i < d->g.lay.count; i++)
        if (got[i]) memcpy(d->img + at + i * size, data + i * size, size);
    return true;
}

/* Called with each track put back, before it goes into the image, when
 * set: test_disk reads DSKINI's layout from it. */
static void (*host_disk_put_hook)(const uint8_t *trk, unsigned c, unsigned h);

static inline void host_disk_insert(coco_t *m, unsigned drive, host_disk_t *d) {
    coco_disk_insert(m, drive, d->protect);
}

/* The request, if any, from disks[drive] (NULL for an empty drive). */
static inline void host_disk_serve(coco_t *m, host_disk_t *disks[COCO_DISK_DRIVES]) {
    const wd_req_t *r = coco_disk_request(m);
    if (!r) return;
    bool ok = true;
    if (r->put) {
        if (host_disk_put_hook) host_disk_put_hook(m->dk.fdc.buf, r->put_cyl, r->put_side);
        host_disk_t *d = disks[r->put_drive];
        if (d && !d->protect && host_disk_put(d, r->put_cyl, r->put_side, m->dk.fdc.buf)) d->puts++;
        else if (d) d->lost++;
    }
    if (r->get) {
        host_disk_t *d = disks[r->drive];
        if (d) {
            host_disk_get(d, r->cyl, r->side, m->dk.fdc.buf);
            d->gets++;
        } else {
            ok = false;
        }
    }
    coco_disk_served(m, ok);
}

#endif /* PICO_COCO_TEST_DISK_UTIL_H */
