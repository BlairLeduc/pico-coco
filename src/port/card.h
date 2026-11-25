/* card.h — the SD card's jobs and its slot (design.md §6, §12.1, §12.6).
 *
 * Core 1 only. A job mounts the card, does its work and unmounts
 * (storage.h), so a card changed between two jobs is simply a new card.
 * A job runs only with core 0 waiting at boot or parked (design.md §6):
 * card latency can outlast both the field and the audio deadline
 * (hardware-notes.md §7.1).
 *
 * The slot's card detect is polled from core 1's loop, and a change is
 * logged and counted. While the missing-ROM page is up the guest has not
 * started, so core 1 runs the ROM job again when a card goes in
 * (handoff.h); once the guest runs, card work comes from a park (M9).
 */
#ifndef PICO_COCO_CARD_H
#define PICO_COCO_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "romset.h"

typedef enum {
    CARD_NONE,          /* card detect says the slot is empty      */
    CARD_UNUSABLE,      /* a card, but no FAT volume FatFs mounts  */
    CARD_MOUNTED,       /* a card, mounted for the last job        */
} card_state_t;

typedef struct {
    card_state_t state;
    int          fresult;     /* FatFs's, from the mount; 0 if mounted   */
    uint32_t     mount_us;    /* sd_init and f_mount                     */
    bool         dir;         /* COCO_ROM_DIR is there                   */
    unsigned     files;       /* files in it, less macOS's "._" ones      */
    unsigned     unknown;     /* of those, the ones no SHA-1 recognises   */
    bool         found[ROM_IMAGE_COUNT];  /* each known image, by hash   */
    /* What was loaded: the Color BASIC for $A000, 1.3 if the card has
     * it and 1.1 if not, ROM_UNKNOWN for neither; and Extended Color
     * BASIC for $8000. */
    rom_id_t     basic;
    bool         extended;
    bool         bas11;       /* Color BASIC 1.1 kept apart, for 4 KB     */
    uint32_t     load_us;     /* reading the loaded images again          */
    /* Where Disk BASIC's ROM is, by its SHA-1, for the RS-DOS cartridge
     * (rsdos.h); "" if the card has none. Read when the cartridge is
     * chosen (card_disk_rom_load), not at boot. */
    char         disk_path[COCO_PATH_MAX];
    /* The Speech/Sound Cartridge's two, by their SHA-1, likewise
     * (card_ssc_roms_load); "" for either the card lacks. */
    char         ssc_pic_path[COCO_PATH_MAX];
    char         ssc_al2_path[COCO_PATH_MAX];
    /* The Orchestra-90's, likewise (card_orch90_load). */
    char         orch90_path[COCO_PATH_MAX];
    /* The Deluxe RS-232 Program Pak's, likewise (card_rs232_load). */
    char         rs232_path[COCO_PATH_MAX];
} card_job_t;

/* The ROMs on the card (design.md §12.1): list COCO_ROM_DIR, read each
 * file a sector at a time, hash it, name what it is, and log a line for
 * each; then read the images the CoCo boots from (Color BASIC 1.3, or
 * 1.1 without it, and Extended Color BASIC 1.1) into `basic` and `ext`,
 * and Color BASIC 1.1 into `basic11` as well, each chosen by its SHA-1
 * whatever the file is called; any of the three may be NULL. Without a
 * usable card the job says so. */
void card_roms(card_job_t *job, uint8_t basic[COCO_ROM_HALF], uint8_t basic11[COCO_ROM_HALF],
               uint8_t ext[COCO_ROM_HALF]);

/* The same with the card already mounted, for the menu (About) and the
 * boot's job, which mount it once for everything they read. */
void card_roms_mounted(card_job_t *job, uint8_t basic[COCO_ROM_HALF], uint8_t basic11[COCO_ROM_HALF],
                       uint8_t ext[COCO_ROM_HALF]);

/* Cartridge images in COCO_CART_DIR, .ccc or .rom, up to max, in
 * directory order, and each file's size into sizes unless it is NULL.
 * The card must be mounted. */
unsigned card_cart_list(char paths[][COCO_PATH_MAX], uint32_t *sizes, unsigned max);

/* A cartridge image into dst, at most 16 KB and at most max bytes, the
 * room left in the pool (carts.h): a path, or a bare name in
 * COCO_CART_DIR as the settings file writes it. NULL, or why not. The
 * card must be mounted. */
const char *card_cart_load(const char *path, uint8_t *dst, uint32_t max, uint32_t *len);

/* The RS-DOS cartridge's ROM, Disk Extended Color BASIC 1.1, into dst
 * from where the job found it, and checked by its SHA-1 again. NULL, or
 * why not. The card must be mounted. */
const char *card_disk_rom_load(const card_job_t *job, uint8_t *dst, uint32_t *len);

/* The settings' and the media's word for the RS-DOS cartridge in the
 * cartridge slot (settings.h): `cart = rsdos`. */
#define CARD_RSDOS "rsdos"
/* The Speech/Sound Cartridge's ROMs (design.md §12.9), read into dst:
 * the TMS7040's 4 KB, then the SP0256-AL2's 2 KB; NULL, or why not. */
const char *card_ssc_roms_load(const card_job_t *job, uint8_t *dst);
/* The Orchestra-90's ROM (design.md §12.10), 8 KB read into dst, its
 * length in *len; NULL, or why not. */
const char *card_orch90_load(const card_job_t *job, uint8_t *dst, uint32_t *len);
/* The Deluxe RS-232 Program Pak's ROM (design.md §12.11), 4 KB read
 * into dst, its length in *len, which is 0 if the card has none: the
 * pak's 6551 works without it. NULL, or why not. */
const char *card_rs232_load(const card_job_t *job, uint8_t *dst, uint32_t *len);

bool card_is_rsdos(const char *name);

/* The slot, debounced: true when card detect has settled at a new level
 * since the last call. Polled from core 1's loop. */
bool card_poll(void);
bool card_present(void);       /* the settled level                  */
uint32_t card_changes(void);   /* settled changes since boot         */

const char *card_state_str(card_state_t st);

#endif /* PICO_COCO_CARD_H */
