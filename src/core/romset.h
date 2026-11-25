/* romset.h — the ROM images the CoCo knows (design.md §12.1), from
 * pico-oric.
 *
 * Each image is identified by SHA-1 whatever the file is called: a
 * near-miss dump boots and then misbehaves (EL §8.1). The table is data
 * about images; nothing in the tree contains one (D10). The hashes are the
 * owner's images, each cross-checked with XRoar 1.13's list of known ROMs
 * by its CRC-32 (§18), and written in roms/README.md, which test_romset
 * holds the table to.
 */
#ifndef PICO_COCO_ROMSET_H
#define PICO_COCO_ROMSET_H

#include <stddef.h>
#include <stdint.h>

#include "sha1.h"

typedef enum {
    ROM_COCO_BAS13,     /* Color BASIC 1.3, $A000                     */
    ROM_COCO_EXTBAS11,  /* Extended Color BASIC 1.1, $8000             */
    ROM_COCO_DISK11,    /* Disk Extended Color BASIC 1.1, $C000 (M13)  */
    ROM_COCO_BAS11,     /* Color BASIC 1.1, $A000: the 4 KB CoCo's     */
    ROM_SSC_PIC7040,    /* the Speech/Sound Cartridge's TMS7040 (M18)  */
    ROM_SSC_AL2,        /* its SP0256-AL2's allophones (M18)           */
    ROM_ORCH90,         /* the Orchestra-90 CC's, $C000 (M18)          */
    ROM_IMAGE_COUNT,
    ROM_UNKNOWN = -1,
} rom_id_t;

typedef struct {
    const char *file;    /* its name under /coco/roms/ (§12.1)    */
    uint32_t    size;
    uint8_t     sha1[SHA1_DIGEST_LEN];
} rom_info_t;

extern const rom_info_t romset_images[ROM_IMAGE_COUNT];

/* Which known image is this? ROM_UNKNOWN for anything else, including
 * the right size with the wrong bytes. */
rom_id_t romset_identify(const uint8_t *data, size_t len);

/* The same, for an image hashed as it was read, a piece at a time, as the
 * card's files are: its length and its digest. */
rom_id_t romset_identify_digest(const uint8_t digest[SHA1_DIGEST_LEN], size_t len);

#endif /* PICO_COCO_ROMSET_H */
