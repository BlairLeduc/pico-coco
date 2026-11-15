/* romset.c — the ROM images the machines know (romset.h). */

#include "romset.h"

#include <stdbool.h>
#include <string.h>

/* The owner's images, 2026-11-14; XRoar 1.13's CRC-32s agree: $D8F4D15E,
 * $A82A6254 and $0B9C5415 (§18). */
const rom_info_t romset_images[ROM_IMAGE_COUNT] = {
    [ROM_COCO_BAS13] = { "bas13.rom", 8192u,
        { 0x28, 0xb9, 0x2b, 0xeb, 0xe3, 0x5f, 0xa4, 0xf0, 0x26, 0xa0,
          0x84, 0x41, 0x6d, 0x6e, 0xa3, 0xb1, 0x55, 0x2b, 0x63, 0xd3 } },
    [ROM_COCO_EXTBAS11] = { "extbas11.rom", 8192u,
        { 0xad, 0x92, 0x7f, 0xb4, 0xf3, 0x07, 0x46, 0xd8, 0x20, 0xcb,
          0x8b, 0x86, 0x0e, 0xbb, 0x58, 0x5e, 0x7f, 0x09, 0x5d, 0xea } },
    [ROM_COCO_DISK11] = { "disk11.rom", 8192u,
        { 0x10, 0xbd, 0xc5, 0xaa, 0x2d, 0x7d, 0x7f, 0x20, 0x5f, 0x67,
          0xb4, 0x7b, 0x19, 0x00, 0x3a, 0x4b, 0xd8, 0x9d, 0xef, 0xd1 } },
};

rom_id_t romset_identify(const uint8_t *data, size_t len) {
    for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
        if (len != romset_images[i].size) continue;
        uint8_t d[SHA1_DIGEST_LEN];
        sha1(data, len, d);
        return romset_identify_digest(d, len);
    }
    return ROM_UNKNOWN;
}

rom_id_t romset_identify_digest(const uint8_t digest[SHA1_DIGEST_LEN], size_t len) {
    for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
        if (len == romset_images[i].size &&
            memcmp(digest, romset_images[i].sha1, SHA1_DIGEST_LEN) == 0)
            return (rom_id_t)i;
    }
    return ROM_UNKNOWN;
}
