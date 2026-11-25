/* snapio.c — our save states on the card (snapio.h). From pico-oric's,
 * with the CoCo's media: the deck, the cartridge and the drives. */

#include "snapio.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "card.h"
#include "dskio.h"
#include "log.h"
#include "tapeio.h"

static FIL s_file;
/* The state's media, from the check to the load; and what the menu
 * should say about them. */
static snap_media_t s_media;
static char s_said[COCO_TEXT_COLS + 1];
/* The file snapio_check passed, for snapio_load. */
static char s_from[40];

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static void path(char *out, size_t cap, unsigned slot, const char *ext) {
    snprintf(out, cap, SNAPIO_DIR "/SLOT%u.%s", slot + 1u, ext);
}

static bool fwrite_cb(void *ctx, const uint8_t *src, size_t n) {
    UINT w = 0;
    return f_write((FIL *)ctx, src, (UINT)n, &w) == FR_OK && w == n;
}

static bool fread_cb(void *ctx, uint8_t *dst, size_t n) {
    UINT r = 0;
    return f_read((FIL *)ctx, dst, (UINT)n, &r) == FR_OK && r == n;
}

snap_status_t snapio_save(const coco_t *m, unsigned slot, uint32_t *us) {
    if (coco_tape_pending(m) != TAPE_NONE) { *us = 0; return SNAP_BUSY; }
    uint32_t t0 = time_us_32();
    char tmp[40], dst[40];
    path(tmp, sizeof tmp, slot, "NEW");
    path(dst, sizeof dst, slot, "SAV");
    (void)f_mkdir("/coco");
    (void)f_mkdir(SNAPIO_DIR);

    /* A save cut off between the unlink and the rename left only the .new,
     * which snapio_check takes as the slot: make it the slot before it is
     * overwritten, as tapeio does. */
    static FILINFO fi;   /* 270 bytes with long names: not on the stack */
    if (f_stat(dst, &fi) == FR_NO_FILE && f_stat(tmp, &fi) == FR_OK) (void)f_rename(tmp, dst);

    /* The deck and the cartridge as they are, so a load puts them back. */
    memset(&s_media, 0, sizeof s_media);
    tapeio_media(m, &s_media);
    /* Each slot's name as the settings write it (carts.h): the port's
     * one cartridge, or a Multi-Pak's four (design.md §12.8). */
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++)
        if (m->port.slot[s].kind != CART_NONE)
            snprintf(s_media.cart[s], sizeof s_media.cart[s], "%s", g_boot.port.slot[s].name);
    if (g_machine.rsdos) dskio_media(&s_media);

    snap_status_t st = SNAP_IO;
    if (f_open(&s_file, tmp, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        st = snapshot_save(m, &s_media, fwrite_cb, &s_file);
        FRESULT fc = f_close(&s_file);
        if (st == SNAP_OK && fc != FR_OK) st = SNAP_IO;
        if (st != SNAP_OK) {
            (void)f_unlink(tmp);
        } else {
            /* The publish. Between the unlink and the rename only
             * slotN.new exists, and snapio_check takes it. */
            (void)f_unlink(dst);
            st = f_rename(tmp, dst) == FR_OK ? SNAP_OK : SNAP_IO;
        }
    }
    *us = time_us_32() - t0;
    return st;
}

/* Two passes over one file: check, then load (snapshot.h). */
static snap_status_t check_file(const coco_t *m, const char *p, snap_info_t *info) {
    if (f_open(&s_file, p, FA_READ) != FR_OK) return SNAP_IO;
    snap_status_t st = snapshot_check(m, fread_cb, &s_file, info, &s_media);
    f_close(&s_file);
    return st;
}

snap_status_t snapio_check(const coco_t *m, unsigned slot, snap_info_t *info, bool *recovered) {
    char main_path[40], tmp[40];
    path(main_path, sizeof main_path, slot, "SAV");
    path(tmp, sizeof tmp, slot, "NEW");
    *recovered = false;
    s_said[0] = 0;
    s_from[0] = 0;
    snap_status_t st = check_file(m, main_path, info);
    const char *from = main_path;
    if (st == SNAP_IO || st == SNAP_CORRUPT || st == SNAP_NOT_SNAPSHOT) {
        /* Missing or damaged: an interrupted publish leaves a whole .new.
         * A state that is whole but for another machine is not damage,
         * and is reported as it is. */
        snap_info_t alt = *info;
        snap_status_t t = check_file(m, tmp, &alt);
        if (t == SNAP_OK || snapshot_other_machine(t)) {
            *recovered = true;
            *info = alt;
            st = t;
            from = tmp;
        }
    }
    if (st == SNAP_OK || snapshot_other_machine(st)) snprintf(s_from, sizeof s_from, "%s", from);
    return st;
}

/* Whether the boot's job loaded the image (core0_power_on takes it from
 * g_boot): Color BASIC 1.1 kept apart, or as the job's only BASIC. */
static bool have_rom(rom_id_t r) {
    const card_job_t *j = &g_boot.job;
    switch (r) {
    case ROM_COCO_BAS13:    return j->basic == ROM_COCO_BAS13;
    case ROM_COCO_BAS11:    return j->bas11 || j->basic == ROM_COCO_BAS11;
    case ROM_COCO_EXTBAS11: return j->extended;
    default:                return false;
    }
}

const char *snapio_machine(const coco_t *m, const snap_info_t *info, machine_t *mc, snap_status_t *st) {
    static char why[COCO_TEXT_COLS + 1];
    (void)m;
    *st = SNAP_NO_ROM;
    if (!info->rom_present[1] || (info->rom[1] != ROM_COCO_BAS13 && info->rom[1] != ROM_COCO_BAS11))
        return "ITS COLOR BASIC IS UNKNOWN";
    if (info->rom_present[0] && info->rom[0] != ROM_COCO_EXTBAS11) return "ITS $8000 ROM IS UNKNOWN";
    if (!have_rom(info->rom[1]))
        return info->rom[1] == ROM_COCO_BAS13 ? "NEEDS BAS13.ROM" : "NEEDS BAS11.ROM";
    if (info->rom_present[0] && !have_rom(ROM_COCO_EXTBAS11)) return "NEEDS EXTBAS11.ROM";
    /* The port, each slot by the kind the state has and a ROM's by the
     * name the media carry; snapshot_load checks each ROM's bytes by
     * their SHA-1 once the machine is powered on. */
    static port_media_t want;
    memset(&want, 0, sizeof want);
    want.mpi = info->mpi;
    want.mpi_switch = info->mpi_switch;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const char *name = "";
        switch (info->slot[s]) {
        case CART_RSDOS:      name = CARD_RSDOS; break;
        case CART_SMARTWATCH: name = CARTS_SMARTWATCH; break;
        case CART_ROM:        name = s_media.cart[s]; break;
        default:              break;
        }
        if (info->slot[s] == CART_ROM && carts_kind(name) != CART_ROM) {
            *st = SNAP_NO_CART;
            return "ITS CARTRIDGE IS UNNAMED";
        }
        snprintf(want.slot[s].name, sizeof want.slot[s].name, "%s", name);
    }
    /* Straight into the machine's pool, which the power-on that follows
     * registers (carts.h); on a failure the running port's ROMs are read
     * in again, as they were a moment ago. */
    const char *err = carts_load(&want, &g_boot.job);
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_kind_t k = info->slot[s];
        if ((k == CART_ROM || k == CART_RSDOS) && !want.slot[s].len) {
            (void)carts_load(&g_boot.port, &g_boot.job);
            *st = SNAP_NO_CART;
            if (k == CART_RSDOS) snprintf(why, sizeof why, "NO DISK BASIC ROM");
            else snprintf(why, sizeof why, "NO %.24s", base(s_media.cart[s]));
            log_core1("  snapshot     : its slot %u, %s: %s\n", s + 1u, want.slot[s].name[0] ?
                      want.slot[s].name : s_media.cart[s], err ? err : "?");
            return why;
        }
    }
    g_boot.port = want;
    mc->ram_k = info->ram_k;
    mc->basic = info->rom[1];
    mc->extended = info->rom_present[0];
    mc->mpi = info->mpi;
    mc->cart = info->mpi || info->slot[0] != CART_NONE;
    mc->rsdos = info->rsdos;
    *st = SNAP_OK;
    return NULL;
}

/* The state's tape back in the deck at its place, after the load. A
 * tape that has gone does not stop the load: the program it loaded is
 * in memory, and the deck is left empty with a word on the status line
 * (EL §8.5). */
static void restore_media(coco_t *m) {
    const char *why = tapeio_restore(m, &s_media);
    if (why) {
        snprintf(s_said, sizeof s_said, " TAPE %.18s MISSING", base(s_media.tape));
        log_core1("  snapshot     : the deck's %s: %s; the deck is empty\n", s_media.tape, why);
    }
    /* The drives' disks, those not in them already: a drive whose disk
     * stays keeps the track the state had in hand asked for again
     * (wd_restored). A disk that has gone leaves its drive empty, as a
     * tape does. Every drive that changes is emptied before any is
     * filled: a disk is in one drive at a time (dskio_insert), so two
     * disks swapped since the state would otherwise refuse the first. */
    if (!coco_disk_fdc(m)) return;
    bool change[COCO_DISK_DRIVES] = { false };
    for (unsigned d = 0; d < COCO_DISK_DRIVES && d < COCO_SNAP_DRIVES; d++) {
        change[d] = strcmp(s_media.disk[d], dskio_inserted(d)) != 0;
        if (change[d]) (void)dskio_insert(m, d, "");
    }
    for (unsigned d = 0; d < COCO_DISK_DRIVES && d < COCO_SNAP_DRIVES; d++) {
        if (!change[d] || !s_media.disk[d][0]) continue;
        why = dskio_insert(m, d, s_media.disk[d]);
        if (why) {
            snprintf(s_said, sizeof s_said, " DISK %.18s MISSING", base(s_media.disk[d]));
            log_core1("  snapshot     : drive %u's %s: %s; the drive is empty\n", d,
                      s_media.disk[d], why);
        }
    }
}

const char *snapio_said(void) {
    return s_said;
}

snap_status_t snapio_load(coco_t *m, bool *changed) {
    *changed = false;
    if (!s_from[0]) return SNAP_IO;
    if (f_open(&s_file, s_from, FA_READ) != FR_OK) return SNAP_IO;
    snap_status_t st = snapshot_load(m, fread_cb, &s_file);
    f_close(&s_file);
    s_from[0] = 0;
    /* Every other refusal comes before anything changes (snapshot.h). */
    *changed = st == SNAP_TORN;
    if (st == SNAP_OK) restore_media(m);
    return st;
}

bool snapio_exists(unsigned slot) {
    char p[40];
    static FILINFO fi;
    path(p, sizeof p, slot, "SAV");
    if (f_stat(p, &fi) == FR_OK) return true;
    path(p, sizeof p, slot, "NEW");
    return f_stat(p, &fi) == FR_OK;
}

bool snapio_delete(unsigned slot) {
    char p[40];
    path(p, sizeof p, slot, "SAV");
    FRESULT a = f_unlink(p);
    path(p, sizeof p, slot, "NEW");
    FRESULT b = f_unlink(p);
    return a == FR_OK || b == FR_OK;
}
