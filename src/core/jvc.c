/* jvc.c — JVC and DSK disk images (jvc.h). */

#include "jvc.h"

#include <string.h>

static bool fits(const jvc_geom_t *j) {
    return jvc_side_bytes(j) <= COCO_DISK_TRACK_DATA_MAX && mfm_layout_fits(j->lay);
}

/* OS-9's LSN0, at the start of a file with no header: its shape, if the
 * sector makes sense as one (jvc.h, XRoar's vdisk.c). */
static bool os9_shape(const uint8_t *lsn0, uint32_t size, jvc_geom_t *j) {
    uint32_t tot = (uint32_t)lsn0[0] << 16 | (uint32_t)lsn0[1] << 8 | lsn0[2];
    unsigned tks = lsn0[0x03], spt = (unsigned)lsn0[0x11] << 8 | lsn0[0x12];
    if (tot * 256u < size || tks != spt || tks == 0) return false;
    jvc_geom_t o = *j;
    o.lay.count = (uint8_t)tks;
    o.sides = (uint8_t)((lsn0[0x10] & 1u) + 1u);
    o.os9 = true;
    if (!fits(&o)) return false;
    *j = o;
    return true;
}

const char *jvc_parse(const uint8_t *head, size_t head_len, uint32_t size, jvc_geom_t *g) {
    unsigned header = size % 256u;
    if (header > JVC_HEADER_MAX) return "NOT A DISK IMAGE";
    if (head_len < header) return "HEADER CUT SHORT";
    jvc_geom_t j = { (uint8_t)header, 1u, { 18u, 1u, 1u }, 0u, false };
    if (header > 0) j.lay.count = head[0];
    if (header > 1) j.sides = head[1];
    if (header > 2) j.lay.n = head[2];
    if (header > 3) j.lay.first = head[3];
    if (header > 4 && head[4] != 0) return "SECTOR ATTRIBUTES";
    if (header == 0 && size >= JVC_HEAD_LEN && head_len >= JVC_HEAD_LEN) (void)os9_shape(head, size, &j);
    if (j.sides < 1u || j.sides > 2u || j.lay.n > 3u || j.lay.count == 0) return "BAD GEOMETRY";
    if (!fits(&j)) return "TRACKS TOO LONG";
    /* So many tracks on one side is a two-sided 720K image, as XRoar
     * takes it (its vdisk.c). */
    uint32_t data = size - header;
    if (data / jvc_side_bytes(&j) >= 88u && j.sides == 1u) j.sides = 2u;
    if (!jvc_resize(&j, size)) return "TOO MANY TRACKS";
    *g = j;
    return NULL;
}

bool jvc_resize(jvc_geom_t *g, uint32_t size) {
    uint32_t data = size > g->header ? size - g->header : 0u, cyl = jvc_side_bytes(g) * g->sides;
    uint32_t tracks = data / cyl;
    /* A sector's worth more is a short last track. */
    if (data % cyl >= mfm_size(g->lay.n)) tracks++;
    if (tracks > COCO_DISK_TRACKS_MAX) return false;
    g->tracks = (uint8_t)tracks;
    return true;
}

jvc_geom_t jvc_two_sided(const jvc_geom_t *g) {
    jvc_geom_t w = *g;
    w.sides = 2u;
    if (!w.os9 && w.header < 2u) w.header = 2u;
    return w;
}

void jvc_header(uint8_t *hdr, const jvc_geom_t *g) {
    uint8_t h[JVC_HEADER_MAX] = { g->lay.count, g->sides, g->lay.n, g->lay.first, 0 };
    memcpy(hdr, h, g->header);
}
