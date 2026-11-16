/* cas.c — the CAS tape image (cas.h). */

#include "cas.h"

#include <string.h>

uint8_t cas_checksum(uint8_t type, uint8_t len, const uint8_t *data) {
    unsigned s = (unsigned)type + len;
    for (unsigned i = 0; i < len; i++) s += data[i];
    return (uint8_t)s;
}

bool cas_next_block(const uint8_t *img, uint32_t len, uint32_t from, cas_block_t *b) {
    for (uint32_t i = from; i < len; i++) {
        if (img[i] != CAS_SYNC) continue;
        if (len - i < 3u) return false;
        uint8_t n = img[i + 2u];
        if ((uint64_t)i + 3u + n + 1u > len) return false;
        b->at = i;
        b->type = img[i + 1u];
        b->len = n;
        b->data = i + 3u;
        b->next = i + 3u + n + 1u;
        b->sum_ok = cas_checksum(b->type, n, &img[b->data]) == img[b->data + n];
        return true;
    }
    return false;
}

/* The ROM's comparison (Color BASIC's $A69C): the eight characters CLOAD
 * was given, padded with spaces, against the namefile's eight. */
static bool name_matches(const uint8_t *nf, const uint8_t *want, unsigned want_len) {
    for (unsigned i = 0; i < CAS_NAME_LEN; i++) {
        uint8_t w = i < want_len ? want[i] : (uint8_t)' ';
        if (nf[i] != w) return false;
    }
    return true;
}

static bool next_namefile(const uint8_t *img, uint32_t len, uint32_t *from, const uint8_t **nf) {
    cas_block_t b;
    while (cas_next_block(img, len, *from, &b)) {
        if (b.sum_ok && b.type == CAS_BLOCK_NAME && b.len >= CAS_NAMEFILE_LEN) {
            *nf = &img[b.data];
            *from = b.next;
            return true;
        }
        /* A sync byte that does not begin a good block: look on from the
         * byte after it, as the ROM's bit-by-bit search would. */
        *from = b.sum_ok ? b.next : b.at + 1u;
    }
    return false;
}

bool cas_first_name(const uint8_t *img, uint32_t len, char out[CAS_NAME_LEN + 1]) {
    uint32_t at = 0;
    const uint8_t *nf;
    if (!next_namefile(img, len, &at, &nf)) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, nf, CAS_NAME_LEN);
    out[CAS_NAME_LEN] = '\0';
    for (int i = (int)CAS_NAME_LEN - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
    return true;
}

bool cas_has_name(const uint8_t *img, uint32_t len, const uint8_t *want, unsigned want_len) {
    uint32_t at = 0;
    const uint8_t *nf;
    while (next_namefile(img, len, &at, &nf))
        if (name_matches(nf, want, want_len)) return true;
    return false;
}
