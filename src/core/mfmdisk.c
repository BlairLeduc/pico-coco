/* mfmdisk.c — raw MFM tracks (mfmdisk.h). */

#include "mfmdisk.h"

#include <string.h>

#define L COCO_DISK_TRACK_LEN

uint16_t mfm_crc(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)(b << 8);
    for (unsigned i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t mfm_crc_block(uint16_t crc, const uint8_t *p, size_t n) {
    while (n--) crc = mfm_crc(crc, *p++);
    return crc;
}

static bool sync_at(const uint8_t *t, unsigned i) {
    return t[i] == 0xA1u && t[i + 1u] == 0xA1u && t[i + 2u] == 0xA1u;
}

unsigned mfm_index(const uint8_t *trk, mfm_sector_t *out, unsigned max) {
    unsigned n = 0, i = 0;
    while (n < max && i + 10u <= L) {
        if (!(sync_at(trk, i) && trk[i + 3u] == 0xFEu)) {
            i++;
            continue;
        }
        mfm_sector_t *s = &out[n++];
        s->id = (uint16_t)(i + 3u);
        s->c = trk[i + 4u];
        s->h = trk[i + 5u];
        s->r = trk[i + 6u];
        s->n = trk[i + 7u];
        s->data = MFM_NO_DATA;
        /* After the ID's CRC, the data mark's sync within the window. */
        unsigned from = i + 10u;
        for (unsigned j = from; j <= from + MFM_DAM_WINDOW && j + 4u <= L; j++) {
            if (sync_at(trk, j) && (trk[j + 3u] == 0xFBu || trk[j + 3u] == 0xF8u)) {
                s->data = (uint16_t)(j + 3u);
                break;
            }
        }
        /* Step over the data field, so that its bytes are never taken
         * for a mark (mfmdisk.h). */
        i = s->data != MFM_NO_DATA ? s->data + 1u + mfm_size(s->n) + 2u : from;
    }
    return n;
}

/* RS-DOS's layout, DSKINI's, read off the track Disk Extended Color
 * BASIC 1.1's DSKINI0 wrote on the host (test_disk, 2026-11-16): the gap
 * before the first sector, then for each sector its sync, the ID, the
 * gap to the data, the data field's sync, the data field ($FF) and the
 * gap after it; gap bytes from there to the index. */
#define LEAD_GAP    32u
#define ID_SYNC      8u
#define ID_GAP      22u
#define DATA_SYNC   12u
#define DATA_GAP    24u
#define SECTOR_BYTES(size) (ID_SYNC + 3u + 1u + 4u + 2u + ID_GAP + DATA_SYNC + 3u + 1u + (size) + 2u + DATA_GAP)

bool mfm_layout_fits(mfm_layout_t lay) {
    if (lay.count == 0 || lay.count > COCO_DISK_SECTORS_MAX || lay.n > 3u) return false;
    return LEAD_GAP + (uint32_t)lay.count * SECTOR_BYTES(mfm_size(lay.n)) <= L;
}

/* The order of the sectors round the track, DSKINI's skip factor of 4:
 * each sector five places on from the last, sector 1 at the index, so
 * 1, 12, 5, 16, 9, 2, ... of 18 (test_disk). Where that place is taken,
 * as it is for a count that 5 divides, the next free one: no DOS here
 * formats such a track, and this keeps every count a permutation. */
static void order(mfm_layout_t lay, uint8_t *at) {
    bool used[COCO_DISK_SECTORS_MAX] = { false };
    unsigned p = 0;
    for (unsigned s = 0; s < lay.count; s++) {
        if (s) p = (p + 5u) % lay.count;
        while (used[p]) p = (p + 1u) % lay.count;
        used[p] = true;
        at[p] = (uint8_t)s;
    }
}

void mfm_build_track(uint8_t *trk, mfm_layout_t lay, uint8_t c, uint8_t h, const uint8_t *data,
                     uint32_t have) {
    const unsigned size = mfm_size(lay.n);
    uint8_t at_place[COCO_DISK_SECTORS_MAX];
    order(lay, at_place);
    unsigned p = 0;
#define PUT(b, k) do { memset(&trk[p], (b), (k)); p += (k); } while (0)
    memset(trk, MFM_UNFORMATTED, L);
    PUT(MFM_UNFORMATTED, LEAD_GAP);
    for (unsigned k = 0; k < lay.count; k++) {
        unsigned s = at_place[k];
        PUT(0x00, ID_SYNC);
        unsigned id = p;
        PUT(0xA1, 3);
        trk[p++] = 0xFEu;
        trk[p++] = c;
        trk[p++] = h;
        trk[p++] = (uint8_t)(lay.first + s);
        trk[p++] = lay.n;
        uint16_t crc = mfm_crc_block(0xFFFFu, &trk[id], 8);
        trk[p++] = (uint8_t)(crc >> 8);
        trk[p++] = (uint8_t)crc;
        PUT(MFM_UNFORMATTED, ID_GAP);
        PUT(0x00, DATA_SYNC);
        unsigned dm = p;
        PUT(0xA1, 3);
        trk[p++] = 0xFBu;
        uint32_t at = (uint32_t)s * size;
        uint32_t n = have > at ? have - at : 0u;
        if (n > size) n = size;
        if (n) memcpy(&trk[p], &data[at], n);
        memset(&trk[p + n], 0, size - n);
        p += size;
        crc = mfm_crc_block(0xFFFFu, &trk[dm], 4u + size);
        trk[p++] = (uint8_t)(crc >> 8);
        trk[p++] = (uint8_t)crc;
        PUT(MFM_UNFORMATTED, DATA_GAP);
    }
#undef PUT
}

unsigned mfm_extract_track(const uint8_t *trk, mfm_layout_t lay, uint8_t *data, bool *got) {
    const unsigned size = mfm_size(lay.n);
    mfm_sector_t sec[COCO_DISK_SECTORS_MAX];
    unsigned ns = mfm_index(trk, sec, COCO_DISK_SECTORS_MAX), found = 0;
    for (unsigned i = 0; i < lay.count; i++) got[i] = false;
    for (unsigned i = 0; i < ns; i++) {
        const mfm_sector_t *s = &sec[i];
        unsigned k = (uint8_t)(s->r - lay.first);
        if (k >= lay.count || s->n != lay.n || s->data == MFM_NO_DATA || got[k]) continue;
        /* A field that would run past the index wraps, as the disc does. */
        for (unsigned b = 0; b < size; b++) data[k * size + b] = trk[(s->data + 1u + b) % L];
        got[k] = true;
        found++;
    }
    return found;
}
