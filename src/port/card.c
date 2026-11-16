/* card.c — the SD card's jobs and its slot (card.h, design.md §12.1, §12.6). */

#include "card.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "config.h"
#include "ff.h"
#include "log.h"
#include "sd.h"
#include "sha1.h"
#include "storage.h"

/* Card detect must hold a new level this long before it counts: the
 * contacts bounce as a card goes in, and a card half in does not answer.
 * 500 ms, which hardware-notes.md §7.1 found reliable; pico-oric's 250 ms
 * let a job find the card silent in M6. */
#define CARD_SETTLE_US 500000u

static volatile bool s_present;   /* settled; core 0 reads it */
static bool     s_raw;            /* last read */
static uint32_t s_raw_since;
static volatile uint32_t s_changes;
static bool     s_polled;

static uint8_t s_chunk[COCO_CARD_CHUNK];

/* FatFs's objects are core 1's and large (a FIL carries a sector), so
 * they are not on its 2 KB stack: 1,660 bytes of it were used with them
 * there (docs/milestones.md, M6). */
static FIL     s_file;
static DIR     s_dir;
static FILINFO s_info;

/* Read one file whole, a sector at a time, into a digest. Returns
 * FatFs's result; *len is the bytes read. */
static FRESULT hash_file(const char *path, uint32_t *len, uint8_t digest[SHA1_DIGEST_LEN]) {
    FIL *f = &s_file;
    FRESULT fr = f_open(f, path, FA_READ);
    if (fr != FR_OK) return fr;
    sha1_t s;
    sha1_init(&s);
    *len = 0;
    for (;;) {
        UINT got = 0;
        fr = f_read(f, s_chunk, sizeof s_chunk, &got);
        if (fr != FR_OK || got == 0) break;
        sha1_update(&s, s_chunk, got);
        *len += got;
    }
    f_close(f);
    sha1_final(&s, digest);
    return fr;
}

/* A recognised image read whole into `dst`, which is its size. */
static bool load_file(const char *path, uint8_t *dst, uint32_t size) {
    FIL *f = &s_file;
    if (f_open(f, path, FA_READ) != FR_OK) return false;
    UINT got = 0;
    FRESULT fr = f_read(f, dst, size, &got);
    f_close(f);
    return fr == FR_OK && got == size;
}

/* The images the CoCo boots from (card.h): 1.3 over 1.1, whichever the
 * card lists first. */
static void maybe_load(card_job_t *j, rom_id_t id, const char *path, const char *name,
                       uint8_t *basic, uint8_t *ext) {
    uint8_t *dst = NULL;
    if (id == ROM_COCO_EXTBAS11) dst = ext;
    else if (id == ROM_COCO_BAS13) dst = basic;
    else if (id == ROM_COCO_BAS11 && j->basic != ROM_COCO_BAS13) dst = basic;
    if (!dst) return;
    uint32_t size = romset_images[id].size;
    uint32_t t0 = time_us_32();
    bool ok = size == COCO_ROM_HALF && load_file(path, dst, size);
    j->load_us += time_us_32() - t0;
    if (!ok) {
        log_core1("  rom          : %s: could not be read again to load it\n", name);
        if (dst == ext) j->extended = false;
        else j->basic = ROM_UNKNOWN;   /* the buffer is no longer what it held */
        return;
    }
    if (dst == ext) j->extended = true;
    else j->basic = id;
}

static void one_rom(card_job_t *j, const FILINFO *fi, uint8_t *basic, uint8_t *ext) {
    char path[COCO_PATH_MAX];
    j->files++;
    if (snprintf(path, sizeof path, "%s/%s", COCO_ROM_DIR, fi->fname) >= (int)sizeof path) {
        j->unknown++;
        log_core1("  rom          : %s: name too long\n", fi->fname);
        return;
    }

    uint8_t d[SHA1_DIGEST_LEN];
    uint32_t len = 0;
    uint32_t t0 = time_us_32();
    FRESULT fr = hash_file(path, &len, d);
    uint32_t us = time_us_32() - t0;
    if (fr != FR_OK) {
        j->unknown++;
        log_core1("  rom          : %s: unreadable (FatFs %d)\n", fi->fname, (int)fr);
        return;
    }

    char hex[2 * SHA1_DIGEST_LEN + 1];
    for (unsigned i = 0; i < SHA1_DIGEST_LEN; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);

    rom_id_t id = romset_identify_digest(d, len);
    const char *what = "unrecognised";
    if (id == ROM_UNKNOWN) {
        j->unknown++;
    } else {
        j->found[id] = true;
        what = romset_images[id].file;
    }
    log_core1("  rom          : %s, %lu bytes, sha1 %s, %s%s, %lu us\n", fi->fname,
              (unsigned long)len, hex, id == ROM_UNKNOWN ? "" : "recognised as ", what,
              (unsigned long)us);
    if (id != ROM_UNKNOWN) maybe_load(j, id, path, fi->fname, basic, ext);
}

void card_roms(card_job_t *j, uint8_t basic[COCO_ROM_HALF], uint8_t ext[COCO_ROM_HALF]) {
    memset(j, 0, sizeof *j);
    j->basic = ROM_UNKNOWN;
    if (!sd_present()) {
        j->state = CARD_NONE;
        log_core1("  card         : no card\n");
        return;
    }

    uint32_t t0 = time_us_32();
    j->fresult = storage_mount();
    j->mount_us = time_us_32() - t0;
    if (j->fresult != 0) {
        j->state = CARD_UNUSABLE;
        log_core1("  card         : no FAT volume (FatFs %d) after %lu us\n", j->fresult,
                  (unsigned long)j->mount_us);
        return;
    }
    j->state = CARD_MOUNTED;
    log_core1("  card         : mounted in %lu us\n", (unsigned long)j->mount_us);

    DIR *dir = &s_dir;
    FRESULT fr = f_opendir(dir, COCO_ROM_DIR);
    if (fr != FR_OK) {
        log_core1("  card         : no %s (FatFs %d)\n", COCO_ROM_DIR, (int)fr);
        storage_unmount();
        return;
    }
    j->dir = true;
    FILINFO *fi = &s_info;
    while (f_readdir(dir, fi) == FR_OK && fi->fname[0]) {
        /* macOS's AppleDouble files are not the user's (hardware-notes.md
         * §7.1), and a directory is not a ROM. */
        if (fi->fattrib & AM_DIR) continue;
        if (fi->fname[0] == '.' && fi->fname[1] == '_') continue;
        one_rom(j, fi, basic, ext);
    }
    f_closedir(dir);
    storage_unmount();

    char missing[96] = "";
    size_t at = 0;
    for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
        if (j->found[i]) continue;
        int n = snprintf(missing + at, sizeof missing - at, " %s", romset_images[i].file);
        if (n < 0 || (size_t)n >= sizeof missing - at) break;
        at += (size_t)n;
    }
    log_core1("  card         : %s has %u files, %u unrecognised; missing:%s\n",
              COCO_ROM_DIR, j->files, j->unknown, at ? missing : " none");
    log_core1("  card         : loaded %s at $A000%s, %lu us\n",
              j->basic == ROM_UNKNOWN ? "no Color BASIC" : romset_images[j->basic].file,
              j->extended ? ", extbas11.rom at $8000" : "", (unsigned long)j->load_us);
}

bool card_poll(void) {
    uint32_t now = time_us_32();
    if (!s_polled) {
        s_polled = true;
        s_present = s_raw = sd_present();
        s_raw_since = now;
        return false;
    }
    bool raw = sd_present();
    if (raw != s_raw) {
        s_raw = raw;
        s_raw_since = now;
        return false;
    }
    if (raw == s_present || now - s_raw_since < CARD_SETTLE_US) return false;
    s_present = raw;
    s_changes++;
    return true;
}

bool card_present(void) {
    return s_present;
}

uint32_t card_changes(void) {
    return s_changes;
}

const char *card_state_str(card_state_t st) {
    switch (st) {
    case CARD_NONE:     return "none";
    case CARD_UNUSABLE: return "unusable";
    case CARD_MOUNTED:  return "mounted";
    }
    return "?";
}
