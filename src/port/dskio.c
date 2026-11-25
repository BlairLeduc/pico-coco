/* dskio.c — disk images on the card (dskio.h), from pico-oric's discio.c. */

#include "dskio.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/time.h"

#include "ff.h"

#include "keymapio.h"
#include "log.h"
#include "mfmdisk.h"
#include "rsdos.h"
#include "snapshot.h"
#include "storage.h"

static FIL s_file;

static struct {
    char       path[COCO_PATH_MAX];
    uint32_t   size;
    jvc_geom_t geom;
    bool       protect;
} s_drive[COCO_DISK_DRIVES];

static uint32_t s_got, s_put, s_lost;

/* One side of a track's sectors, in transit between the file and the
 * controller's raw track (mfmdisk.h). */
static uint8_t s_data[COCO_DISK_TRACK_DATA_MAX];

static bool serve_mounted(coco_t *m, bool mounted, uint32_t t0, uint32_t *us);

/* Neither a directory nor macOS's AppleDouble "._" shadow of a file. */
static bool is_disk(const char *fname) {
    if (fname[0] == '.') return false;
    size_t n = strlen(fname);
    return n > 4 && (strcasecmp(fname + n - 4, ".dsk") == 0 || strcasecmp(fname + n - 4, ".jvc") == 0);
}

bool dskio_list(const char *path, dirlist_t *l) {
    DIR dir;
    static FILINFO fi;
    dirlist_clear(l);
    if (f_opendir(&dir, path) != FR_OK) return false;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fname[0] == '.' || (fi.fattrib & (AM_HID | AM_SYS))) continue;
        bool folder = (fi.fattrib & AM_DIR) != 0;
        if (!folder && !is_disk(fi.fname)) continue;
        (void)dirlist_add(l, fi.fname,
                          (uint8_t)((folder ? DIRLIST_FOLDER : 0u) |
                                    ((fi.fattrib & AM_RDO) ? DIRLIST_RDONLY : 0u)),
                          (uint32_t)fi.fsize);
    }
    f_closedir(&dir);
    dirlist_sort(l);
    return true;
}

/* Put back what the drive has written, and drop its request. */
static void settle_drive(coco_t *m, unsigned drive) {
    if (!m) return;
    coco_disk_flush(m);
    const wd_req_t *r = coco_disk_request(m);
    if (r && ((r->put && r->put_drive == drive) || (r->get && r->drive == drive))) {
        uint32_t us;
        (void)serve_mounted(m, true, time_us_32(), &us);
    }
}

/* A bare name is in DSKIO_DIR, as the settings file writes it. */
static void full_path(const char *path, char out[COCO_PATH_MAX]) {
    if (path[0] == '/') snprintf(out, COCO_PATH_MAX, "%s", path);
    else snprintf(out, COCO_PATH_MAX, "%s/%s", DSKIO_DIR, path);
}

/* The header, or OS-9's LSN0 where there is none (jvc.h). */
static const char *probe(const char *path, uint32_t *size, jvc_geom_t *g, bool *protect) {
    static FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return "NOT ON THE CARD";
    if (fi.fattrib & AM_DIR) return "A FOLDER";
    UINT got = 0;
    if (f_open(&s_file, path, FA_READ) != FR_OK) return "CANNOT OPEN";
    FRESULT fr = f_read(&s_file, s_data, JVC_HEAD_LEN, &got);
    f_close(&s_file);
    if (fr != FR_OK) return "CANNOT READ";
    const char *why = jvc_parse(s_data, got, (uint32_t)fi.fsize, g);
    if (why) return why;
    *size = (uint32_t)fi.fsize;
    *protect = (fi.fattrib & AM_RDO) != 0;
    return NULL;
}

const char *dskio_probe(const char *path, jvc_geom_t *g, bool *protect) {
    char full[COCO_PATH_MAX];
    if (strlen(path) >= sizeof full - sizeof DSKIO_DIR) return "NAME TOO LONG";
    full_path(path, full);
    uint32_t size;
    jvc_geom_t geom;
    bool prot;
    const char *why = probe(full, &size, &geom, &prot);
    if (why) return why;
    if (g) *g = geom;
    if (protect) *protect = prot;
    return NULL;
}

const char *dskio_insert(coco_t *m, unsigned drive, const char *path) {
    if (drive >= COCO_DISK_DRIVES) return "NO SUCH DRIVE";
    settle_drive(m, drive);
    s_drive[drive].path[0] = 0;
    if (m) coco_disk_eject(m, drive);
    if (!path || !path[0]) return NULL;
    if (strlen(path) >= COCO_PATH_MAX - sizeof DSKIO_DIR) return "NAME TOO LONG";

    char full[COCO_PATH_MAX];
    full_path(path, full);
    /* A disk is in one drive at a time: two would each hold a track of
     * the same file. */
    for (unsigned d = 0; d < COCO_DISK_DRIVES; d++)
        if (d != drive && strcasecmp(s_drive[d].path, full) == 0) return "IN ANOTHER DRIVE";
    const char *why = probe(full, &s_drive[drive].size, &s_drive[drive].geom, &s_drive[drive].protect);
    if (why) return why;
    strcpy(s_drive[drive].path, full);
    if (m) coco_disk_insert(m, drive, s_drive[drive].protect);
    const jvc_geom_t *g = &s_drive[drive].geom;
    log_core1("  disk         : drive %u: %s, %u track%s, %u side%s of %u x %u bytes%s%s\n", drive, full,
              g->tracks, g->tracks == 1 ? "" : "s", g->sides, g->sides > 1 ? "s" : "", g->lay.count,
              mfm_size(g->lay.n), g->os9 ? ", OS-9's LSN0" : "",
              s_drive[drive].protect ? ", write-protected" : "");
    keymapio_file_loaded(full);   /* a layout may name it (§11.3) */
    return NULL;
}

void dskio_flush(coco_t *m) {
    coco_disk_flush(m);
    if (coco_disk_request(m)) {
        uint32_t us;
        (void)serve_mounted(m, true, time_us_32(), &us);
    }
}

const char *dskio_inserted(unsigned drive) {
    return drive < COCO_DISK_DRIVES ? s_drive[drive].path : "";
}

bool dskio_protected(unsigned drive) {
    return drive < COCO_DISK_DRIVES && s_drive[drive].protect;
}

void dskio_attach(coco_t *m) {
    if (!coco_disk_fdc(m)) return;
    for (unsigned d = 0; d < COCO_DISK_DRIVES; d++)
        if (s_drive[d].path[0]) coco_disk_insert(m, d, s_drive[d].protect);
}

void dskio_media(snap_media_t *md) {
    for (unsigned d = 0; d < COCO_DISK_DRIVES && d < COCO_SNAP_DRIVES; d++)
        memcpy(md->disk[d], s_drive[d].path, sizeof md->disk[d]);
}

void dskio_next_name(const char *dir, char out[COCO_MEDIA_NAME_MAX + 1]) {
    char path[COCO_PATH_MAX];
    static FILINFO fi;
    out[0] = 0;
    for (unsigned n = 1; n <= 99u; n++) {
        char name[16];
        snprintf(name, sizeof name, "DISK%02u.dsk", n);
        if (!dirlist_join(path, sizeof path, dir, name)) return;
        FRESULT fr = f_stat(path, &fi);
        if (fr == FR_OK) continue;
        if (fr == FR_NO_FILE) snprintf(out, COCO_MEDIA_NAME_MAX + 1, "DISK%02u", n);
        return;
    }
}

const char *dskio_new(coco_t *m, unsigned drive, const char *dir, const char *name) {
    if (drive >= COCO_DISK_DRIVES) return "NO SUCH DRIVE";
    if (!dirlist_within(dir, DSKIO_DIR)) return "NOT IN " DSKIO_DIR;
    char path[COCO_PATH_MAX], file[COCO_MEDIA_NAME_MAX + 8];
    static FILINFO fi;
    (void)f_mkdir("/coco");
    (void)f_mkdir(DSKIO_DIR);
    snprintf(file, sizeof file, "%.*s.dsk", (int)COCO_MEDIA_NAME_MAX, name);
    if (!dirlist_join(path, sizeof path, dir, file)) return "NAME TOO LONG";
    FRESULT fr = f_stat(path, &fi);
    if (fr == FR_OK) return "ALREADY THERE";
    if (fr != FR_NO_FILE) return "CARD ERROR";
    /* Empty, so nothing to write through a .new: the file is whole the
     * moment it exists. */
    if (f_open(&s_file, path, FA_WRITE | FA_CREATE_NEW) != FR_OK) return "CANNOT CREATE";
    if (f_close(&s_file) != FR_OK) {
        (void)f_unlink(path);
        return "CARD ERROR";
    }
    log_core1("  disk         : %s made, empty, for DSKINI\n", path);
    return dskio_insert(m, drive, path);
}

uint32_t dskio_tracks(bool written) {
    return written ? s_put : s_got;
}

uint32_t dskio_lost(void) {
    return s_lost;
}

/* Side h of track c of the drive's image into the controller's track. */
static bool get_track(unsigned drive, unsigned h, unsigned c, uint8_t *trk) {
    if (drive >= COCO_DISK_DRIVES || !s_drive[drive].path[0]) return false;
    const jvc_geom_t *g = &s_drive[drive].geom;
    if (c >= g->tracks || h >= g->sides) {
        memset(trk, MFM_UNFORMATTED, COCO_DISK_TRACK_LEN);
        return true;
    }
    uint32_t at = jvc_offset(g, c, h), n = jvc_side_bytes(g);
    uint32_t have = s_drive[drive].size > at ? s_drive[drive].size - at : 0u;
    if (have > n) have = n;
    if (f_open(&s_file, s_drive[drive].path, FA_READ) != FR_OK) return false;
    UINT done = 0;
    bool ok = f_lseek(&s_file, at) == FR_OK && f_read(&s_file, s_data, have, &done) == FR_OK &&
              done == have;
    ok = f_close(&s_file) == FR_OK && ok;
    if (ok) mfm_build_track(trk, g->lay, (uint8_t)c, (uint8_t)h, s_data, have);
    return ok;
}

/* Zeros from the file's end to `to`, a piece at a time. */
static bool grow(uint32_t from, uint32_t to) {
    static const uint8_t zero[512];
    if (f_lseek(&s_file, from) != FR_OK) return false;
    while (from < to) {
        UINT n = to - from < sizeof zero ? (UINT)(to - from) : (UINT)sizeof zero, put = 0;
        if (f_write(&s_file, zero, n, &put) != FR_OK || put != n) return false;
        from += n;
    }
    return true;
}

/* A one-sided image made two-sided, its second side written (jvc.h):
 * from the last track to the first, each track's side moved to its new
 * place, which is never before its old one, and zeros after it for the
 * second side; then the header. The file open read-write. */
static bool widen(unsigned drive) {
    jvc_geom_t *g = &s_drive[drive].geom, w = jvc_two_sided(g);
    uint32_t n = jvc_side_bytes(g), size = s_drive[drive].size;
    uint32_t t0 = time_us_32();
    bool ok = true;
    for (unsigned c = g->tracks; ok && c-- > 0;) {
        uint32_t at = jvc_offset(g, c, 0), have = size > at ? size - at : 0u;
        if (have > n) have = n;
        UINT done = 0;
        ok = f_lseek(&s_file, at) == FR_OK && f_read(&s_file, s_data, have, &done) == FR_OK &&
             done == have;
        memset(s_data + have, 0, n - have);
        ok = ok && f_lseek(&s_file, jvc_offset(&w, c, 0)) == FR_OK &&
             f_write(&s_file, s_data, n, &done) == FR_OK && done == n;
        memset(s_data, 0, n);
        ok = ok && f_write(&s_file, s_data, n, &done) == FR_OK && done == n;
    }
    if (ok && w.header) {
        uint8_t hdr[JVC_HEADER_MAX];
        UINT done = 0;
        jvc_header(hdr, &w);
        ok = f_lseek(&s_file, 0) == FR_OK && f_write(&s_file, hdr, w.header, &done) == FR_OK &&
             done == w.header;
    }
    if (!ok) return false;
    *g = w;
    s_drive[drive].size = (uint32_t)f_size(&s_file);
    (void)jvc_resize(g, s_drive[drive].size);
    log_core1("  disk         : drive %u: now two-sided, %u tracks, %lu us\n", drive, g->tracks,
              (unsigned long)(time_us_32() - t0));
    return true;
}

/* The sectors of a written track back into the image: the whole side in
 * one write when every sector is there, otherwise each one found. The
 * file grows to the track, the tracks before it filled with zeros. */
static bool put_track(unsigned drive, unsigned h, unsigned c, const uint8_t *trk) {
    if (drive >= COCO_DISK_DRIVES || !s_drive[drive].path[0] || s_drive[drive].protect) return false;
    jvc_geom_t *g = &s_drive[drive].geom;
    bool got[COCO_DISK_SECTORS_MAX];
    unsigned found = h < 2u ? mfm_extract_track(trk, g->lay, s_data, got) : 0u;
    if (found == 0) {
        s_lost++;
        log_core1("  disk         : drive %u side %u track %u: no sector the image can keep\n",
                  drive, h, c);
        return true;   /* the card did not fail: the image has no room for it */
    }
    uint32_t at = jvc_offset(g, c, h), size = mfm_size(g->lay.n), n = jvc_side_bytes(g);
    if (f_open(&s_file, s_drive[drive].path, FA_READ | FA_WRITE) != FR_OK) return false;
    bool ok = true;
    if (h >= g->sides) {
        /* widen uses s_data: the sectors found are taken again after. */
        ok = widen(drive);
        (void)mfm_extract_track(trk, g->lay, s_data, got);
        at = jvc_offset(g, c, h);
    }
    if (ok && s_drive[drive].size < at) ok = grow(s_drive[drive].size, at);
    UINT done = 0;
    if (ok && found == g->lay.count) {
        ok = f_lseek(&s_file, at) == FR_OK && f_write(&s_file, s_data, n, &done) == FR_OK && done == n;
    } else {
        for (unsigned i = 0; ok && i < g->lay.count; i++) {
            if (!got[i]) continue;
            /* A sector past the file's end grows it to the sector: the
             * ones before it within the side are zeros, as an image cut
             * short reads (jvc.h). */
            uint32_t s_at = at + i * size;
            if (s_drive[drive].size < s_at && s_at > at) ok = grow(f_size(&s_file), s_at);
            ok = ok && f_lseek(&s_file, s_at) == FR_OK &&
                 f_write(&s_file, s_data + i * size, size, &done) == FR_OK && done == size;
        }
    }
    uint32_t len = (uint32_t)f_size(&s_file);
    ok = f_close(&s_file) == FR_OK && ok;
    if (len != s_drive[drive].size) {
        s_drive[drive].size = len;
        (void)jvc_resize(g, len);
    }
    if (found < g->lay.count)
        log_core1("  disk         : drive %u side %u track %u: %u of %u sectors kept\n", drive, h, c,
                  found, g->lay.count);
    return ok;
}

bool dskio_serve(coco_t *m, uint32_t *us) {
    *us = 0;
    if (!coco_disk_request(m)) return true;
    uint32_t t0 = time_us_32();
    bool mounted = storage_mount() == 0;
    bool ok = serve_mounted(m, mounted, t0, us);
    if (mounted) storage_unmount();
    return ok;
}

/* The request, with the card mounted or not: without, it fails. */
static bool serve_mounted(coco_t *m, bool mounted, uint32_t t0, uint32_t *us) {
    wd_req_t q = *coco_disk_request(m);
    uint8_t *buf = coco_disk_fdc(m)->buf;
    bool ok = mounted;
    if (ok && q.put) {
        ok = put_track(q.put_drive, q.put_side, q.put_cyl, buf);
        if (ok) s_put++;
    }
    if (ok && q.get) {
        ok = get_track(q.drive, q.side, q.cyl, buf);
        if (ok) s_got++;
    }
    *us = time_us_32() - t0;
    char put[24] = "", get[24] = "";
    if (q.put) snprintf(put, sizeof put, "put %u.%u:%u ", q.put_drive, q.put_side, q.put_cyl);
    if (q.get) snprintf(get, sizeof get, "get %u.%u:%u ", q.drive, q.side, q.cyl);
    log_core1("  disk         : %s%s%s, %lu us\n", put, get, ok ? "ok" : "FAILED", (unsigned long)*us);
    coco_disk_served(m, ok);
    return ok;
}
