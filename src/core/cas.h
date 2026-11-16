/* cas.h — the CAS tape image (design.md §12.3).
 *
 * A CAS is the bytes the ROM writes to tape, without the signal. Each
 * block is a leader of $55s, the sync byte $3C, its type, its length, that
 * many data bytes, a checksum (the type, the length and the data summed
 * modulo 256), and one more $55. A file is a namefile block (type 0, 15
 * bytes), its data blocks (type 1, up to 255 bytes each) and an end
 * block (type $FF, no data). Color BASIC's BLKOUT ($A7F4) writes exactly
 * this, and its BLKIN ($A70B) reads a block wherever the next $3C is
 * (tape.h).
 *
 * The namefile's 15 bytes: the name, eight characters padded with
 * spaces; the file type (0 BASIC, 1 data, 2 machine code); the ASCII flag
 * ($FF for an ASCII file); the gap flag ($FF when each block has its own
 * leader and the motor stops between them); the execution address and
 * the load address, each high byte first.
 *
 * Nothing here touches the machine or the card.
 */
#ifndef PICO_COCO_CAS_H
#define PICO_COCO_CAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAS_LEADER_BYTE  0x55u
#define CAS_SYNC         0x3Cu

#define CAS_BLOCK_NAME   0x00u
#define CAS_BLOCK_DATA   0x01u
#define CAS_BLOCK_EOF    0xFFu

#define CAS_NAME_LEN        8u
#define CAS_NAMEFILE_LEN   15u

/* The namefile's fields, by their place in its data. */
#define CAS_NF_TYPE         8u
#define CAS_NF_ASCII        9u
#define CAS_NF_GAP         10u
#define CAS_NF_EXEC        11u
#define CAS_NF_LOAD        13u

typedef struct {
    uint32_t at;       /* the sync byte's offset                       */
    uint8_t  type;
    uint8_t  len;
    uint32_t data;     /* the first data byte's offset                 */
    uint32_t next;     /* the offset after the checksum                */
    bool     sum_ok;
} cas_block_t;

/* The next whole block at or after `from`, in an image of `len` bytes.
 * False when no sync byte is left, or the block after the last one runs
 * past the end. */
bool    cas_next_block(const uint8_t *img, uint32_t len, uint32_t from, cas_block_t *b);

/* The checksum of a block: type + length + data, modulo 256. */
uint8_t cas_checksum(uint8_t type, uint8_t len, const uint8_t *data);

/* The first namefile's name, as eight characters and a NUL, trailing
 * spaces trimmed. False if the image has no namefile block. */
bool    cas_first_name(const uint8_t *img, uint32_t len, char out[CAS_NAME_LEN + 1]);

/* Whether the image holds a namefile whose name is `want` (`want_len`
 * characters, compared as the ROM does: the eight characters with the
 * want padded with spaces). */
bool    cas_has_name(const uint8_t *img, uint32_t len, const uint8_t *want, unsigned want_len);

#endif /* PICO_COCO_CAS_H */
