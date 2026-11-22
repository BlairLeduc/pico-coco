/* snapshot.h — our own save states, .sav (design.md §12.5; EL §8.5).
 *
 * pico-oric's format with the 6809's, the PIAs', the SAM's and the
 * field's fields. Explicit, field by field and little-endian, never a
 * struct dumped from memory: coco_t holds pointers and padding, and a
 * state has to outlive the build that wrote it. Not to be confused with
 * the VDG's snapshot (coco_video_snapshot, §9.3), which is a field's
 * picture.
 *
 *   header  20 bytes: "COCOSNAP" (8), version (2), header length (2),
 *           payload length (4), CRC-32 of the payload (4)
 *   payload the state (SNAP_STATE_LEN), all 64 KB of coco_t's RAM, then
 *           the media (SNAP_MEDIA_LEN)
 *
 * The media are the files the port had in the deck and the cartridge
 * slot, the four drives', and the deck's place (snap_media_t):
 * names, not contents, which stay on the card. The core carries them and
 * never opens them; the port fills them on a save and puts them back
 * after a load.
 *
 * No ROM is in a state: what is, is the SHA-1 of each ROM half at $8000
 * and $A000 and of the cartridge's 16 KB window, and a state loads only
 * into a machine whose ROMs hash the same and whose machine, RAM, CART
 * line, tape trap and field are the same, since every count in it is in
 * that machine's ticks. The field is the build's (coco_init's); the rest
 * a power-on chooses, and snap_info_t says what a state needs, so that
 * the port can power on as that machine and load it (snapio.h). Unused
 * state bytes are written zero and read as reserved, so a later version
 * can add fields whose zero is their reset value.
 *
 * States are saved between fields, where the port parks the guest; the
 * time is absolute ticks (§13.2), so the field resumes where it stood
 * and the overshoot of its last instruction is in the tick count. Keys
 * are not state: a load releases them all, and the sound's sample grid
 * starts again at the restored tick (coco_restored).
 *
 * I/O is through a callback, so the core never sees a file. Loading is
 * two passes: snapshot_check reads the whole stream and verifies the
 * header, the CRC and the machine without touching it; only then does
 * snapshot_load, over the same bytes again, change anything. A torn or
 * foreign file therefore leaves the running machine exactly as it was.
 * The calls share one buffer, so one runs at a time.
 */
#ifndef PICO_COCO_SNAPSHOT_H
#define PICO_COCO_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coco.h"
#include "romset.h"

#define SNAP_VERSION     1u
#define SNAP_HEADER_LEN  20u
#define SNAP_STATE_LEN   256u
/* The deck's path, the cartridge's, COCO_SNAP_DRIVES drives', each
 * COCO_PATH_MAX bytes NUL-padded; then the deck's
 * place, its rewind mark and flags (4 bytes each), and 4 reserved. */
#define COCO_SNAP_DRIVES COCO_DISK_DRIVES
#define SNAP_MEDIA_LEN   ((2u + COCO_SNAP_DRIVES) * COCO_PATH_MAX + 16u)
#define SNAP_PAYLOAD_LEN (SNAP_STATE_LEN + COCO_ADDR_SPACE + SNAP_MEDIA_LEN)
#define SNAP_FILE_LEN    (SNAP_HEADER_LEN + SNAP_PAYLOAD_LEN)

typedef enum {
    SNAP_OK = 0,
    SNAP_IO,              /* the callback failed, or the file is short   */
    SNAP_NOT_SNAPSHOT,    /* wrong magic or lengths, or a field no
                             running machine keeps                       */
    SNAP_NEWER,           /* a version this build does not know          */
    SNAP_CORRUPT,         /* the CRC does not match                      */
    SNAP_OTHER_FIELD,     /* another field's shape: the build's          */
    SNAP_OTHER_ROM,       /* a BASIC ROM is not the one it ran on        */
    SNAP_OTHER_RAM,       /* another RAM size                            */
    SNAP_OTHER_MACHINE,   /* another machine, cartridge, CART line or trap */
    SNAP_BUSY,            /* a tape request waiting, or a disk command or
                             track in hand (rsdos.h)                     */
    SNAP_NO_ROM,          /* the port's: a ROM it needs is not on the card
                             or not one the emulator knows               */
    SNAP_NO_CART,         /* the port's: its cartridge is not on the card */
    SNAP_UNSAVED,         /* the port's: a recording is not on the card  */
    SNAP_TORN,            /* snapshot_load's read failed once m had begun
                             to change: it must be powered on again      */
} snap_status_t;

/* The port's media at the save (the header above). Paths are as the
 * port keeps them, "" for an empty deck, slot or drive. */
typedef struct snap_media_s {
    char     tape[COCO_PATH_MAX];
    char     cart[COCO_PATH_MAX];
    char     disk[COCO_SNAP_DRIVES][COCO_PATH_MAX];
    uint32_t tape_pos;            /* where the head stands               */
    uint32_t tape_rewind;         /* the port's rewind mark (tapeio.c)   */
    bool     tape_chosen;         /* put in by the user or boot_tape     */
} snap_media_t;

/* The machine a state was taken on, for naming it in a refusal and for
 * powering on as it. Each ROM half by the SHA-1 in the file: ROM_UNKNOWN
 * when it is none of ours, and the half's `present` false when the
 * machine had none there. */
typedef struct {
    unsigned       ram_k;
    bool           rom_present[2];   /* $8000, $A000                    */
    rom_id_t       rom[2];
    bool           cart;             /* a ROM at $C000                  */
    bool           cart_autostart;   /* its CART line on the Q clock    */
    bool           tape_trap;
    bool           rsdos;            /* the RS-DOS cartridge (rsdos.h)  */
} snap_info_t;

/* Move exactly n bytes; false on any failure. */
typedef bool (*snap_write_fn)(void *ctx, const uint8_t *src, size_t n);
typedef bool (*snap_read_fn)(void *ctx, uint8_t *dst, size_t n);

/* `media` NULL writes an empty deck, slot and drives. SNAP_BUSY, and
 * nothing written, while a tape request waits. */
snap_status_t snapshot_save(const coco_t *m, const snap_media_t *media, snap_write_fn write,
                            void *ctx);

/* The whole stream, checked against m; m is not changed. `info`, if not
 * NULL, says what machine the file is for once its state has been read,
 * whether or not that is m. `media`, if not NULL, is the file's media
 * once it has passed its CRC. */
snap_status_t snapshot_check(const coco_t *m, snap_read_fn read, void *ctx, snap_info_t *info,
                             snap_media_t *media);

/* Every refusal leaves m as it was, but SNAP_TORN: the stream failing
 * in its RAM, after snapshot_check had read it whole, which is the card
 * going or the file changing between the passes. A load does not wait
 * for a tape request: the port powers on first (snapio.h), so one into a
 * machine waiting on the tape is refused SNAP_BUSY. The deck's tape and
 * place are the port's to put back. */
snap_status_t snapshot_load(coco_t *m, snap_read_fn read, void *ctx);

/* A state refused SNAP_OTHER_ROM, SNAP_OTHER_RAM or SNAP_OTHER_MACHINE
 * has passed every other check, the field's first, and loads into a
 * machine powered on as `info` says with the cartridge the media name. */
static inline bool snapshot_other_machine(snap_status_t st) {
    return st == SNAP_OTHER_ROM || st == SNAP_OTHER_RAM || st == SNAP_OTHER_MACHINE;
}

const char *snapshot_status_str(snap_status_t st);

/* CRC-32 (IEEE 802.3, reflected, as zlib computes it), exposed for the
 * test that pins it against a known value. */
uint32_t snapshot_crc32(uint32_t crc, const uint8_t *p, size_t n);

#endif /* PICO_COCO_SNAPSHOT_H */
