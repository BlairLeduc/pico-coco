/* tapeio.c — CAS files on the card, serving the tape trap (tapeio.h,
 * design.md §12.3). */

#include "keymapio.h"
#include "tapeio.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "card.h"
#include "log.h"
#include "sd.h"
#include "storage.h"
#include "snapshot.h"
#include "tape.h"

/* The deck: the whole tape (design.md §16's tape buffer). */
static uint8_t  s_buf[COCO_TAPE_IMAGE_MAX];
static char     s_path[COCO_PATH_MAX];   /* "" for none, or a new tape not yet named */
static bool     s_in;                    /* a tape in the deck                       */
static uint32_t s_len;                   /* its bytes on the card, for a power-on    */
static bool     s_chosen;
static char     s_said[COCO_TEXT_COLS + 1];

/* The blocks read when the tape was last rewound at its end, so that a
 * second pass over the whole tape gives up (tapeio.h). */
#define NO_REWIND UINT32_MAX
static uint32_t s_rewind_blocks = NO_REWIND;

/* A recording that could not be written: not tried again until it grows
 * or the card changes, so core 0 does not park every field. */
static uint32_t s_failed_len, s_failed_card;
static bool     s_failed;

static FIL     s_file;
static DIR     s_dir;
static FILINFO s_info;

volatile tapeio_stats_t g_tape_stats;

static void say(const char *fmt, const char *arg) {
    snprintf(s_said, sizeof s_said, fmt, arg);
    log_core1("  tape         : %s\n", s_said);
}

const char *tapeio_said(void) {
    static char out[COCO_TEXT_COLS + 1];
    memcpy(out, s_said, sizeof out);
    s_said[0] = 0;
    return out;
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static bool is_cas(const char *name) {
    size_t n = strlen(name);
    return n > 4 && name[n - 4] == '.' && toupper((unsigned char)name[n - 3]) == 'C' &&
           toupper((unsigned char)name[n - 2]) == 'A' && toupper((unsigned char)name[n - 1]) == 'S';
}

/* ---- the machine's side ------------------------------------------------ */

/* coco_init empties the core's deck; the tape goes back in, rewound. A
 * power-on with a recording not written out is refused (menu.c). */
void tapeio_attach(coco_t *m) {
    coco_tape_attach(m, s_buf, sizeof s_buf);
    if (s_in) coco_tape_insert(m, s_len);
}

static bool ends_with_eof(const coco_t *m) {
    static const uint8_t eof[5] = { CAS_SYNC, CAS_BLOCK_EOF, 0x00, CAS_BLOCK_EOF, CAS_LEADER_BYTE };
    const coco_tape_t *t = &m->tape;
    return t->len >= 5u && memcmp(&s_buf[t->len - 5u], eof, 5) == 0;
}

bool tapeio_unsaved(const coco_t *m) {
    uint32_t from, to;
    return coco_tape_unsaved(m, &from, &to);
}

bool tapeio_wanted(const coco_t *m) {
    if (coco_tape_pending(m) != TAPE_NONE) return true;
    if (!tapeio_unsaved(m) || !ends_with_eof(m)) return false;
    return !(s_failed && s_failed_len == m->tape.len && s_failed_card == card_changes());
}

static uint8_t peek(const coco_t *m, uint16_t a) {
    int32_t i = coco_ram_index(m, a);
    return i < 0 ? 0xFFu : m->ram[i];
}

/* ---- the card ---------------------------------------------------------- */

/* A file into the deck's buffer: its length, or -1 with *why set. A file
 * cut off between its .new and the rename is found as the .new. */
static int32_t read_file(const char *path, uint8_t *dst, uint32_t cap, const char **why) {
    FRESULT fr = f_open(&s_file, path, FA_READ);
    if (fr == FR_NO_FILE) {
        char alt[COCO_PATH_MAX + 4];
        if (snprintf(alt, sizeof alt, "%s.new", path) >= (int)sizeof alt) {
            *why = "NAME TOO LONG";
            return -1;
        }
        fr = f_open(&s_file, alt, FA_READ);
    }
    if (fr != FR_OK) {
        *why = fr == FR_NO_FILE ? "NO SUCH FILE" : "CANNOT OPEN";
        return -1;
    }
    FSIZE_t size = f_size(&s_file);
    if (size > cap) {
        f_close(&s_file);
        *why = "TOO LONG FOR THE DECK";
        return -1;
    }
    UINT got = 0;
    fr = f_read(&s_file, dst, (UINT)size, &got);
    f_close(&s_file);
    if (fr != FR_OK || got != size) {
        *why = "CANNOT READ";
        return -1;
    }
    return (int32_t)got;
}

/* The whole buffer to `path`, through path.new and a rename (EL §8.6). */
static bool write_file(const char *path, const uint8_t *src, uint32_t len) {
    char tmp[COCO_PATH_MAX + 4];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    (void)f_mkdir("/coco");
    (void)f_mkdir(TAPEIO_DIR);
    FRESULT fr = f_open(&s_file, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        UINT put = 0;
        fr = len ? f_write(&s_file, src, len, &put) : FR_OK;
        if (fr == FR_OK && put != len) fr = FR_DENIED;
        FRESULT fc = f_close(&s_file);
        if (fr == FR_OK) fr = fc;
    }
    if (fr == FR_OK) {
        (void)f_unlink(path);
        fr = f_rename(tmp, path);
    }
    if (fr != FR_OK) log_core1("  tape         : %s not written: FatFs %d\n", path, (int)fr);
    return fr == FR_OK;
}

/* The first file's name and type from the head of a file. */
static void first_file(const char *path, char name[CAS_NAME_LEN + 1], uint8_t *type) {
    static uint8_t head[1024];
    name[0] = 0;
    *type = 0;
    if (f_open(&s_file, path, FA_READ) != FR_OK) return;
    UINT got = 0;
    (void)f_read(&s_file, head, sizeof head, &got);
    f_close(&s_file);
    cas_block_t b;
    uint32_t at = 0;
    while (cas_next_block(head, got, at, &b)) {
        if (b.sum_ok && b.type == CAS_BLOCK_NAME && b.len >= CAS_NAMEFILE_LEN) {
            *type = head[b.data + CAS_NF_TYPE];
            break;
        }
        at = b.at + 1u;
    }
    (void)cas_first_name(head, got, name);
}

unsigned tapeio_list(tapeio_entry_t *out, unsigned max) {
    unsigned n = 0;
    if (f_opendir(&s_dir, TAPEIO_DIR) != FR_OK) return 0;
    while (n < max && f_readdir(&s_dir, &s_info) == FR_OK && s_info.fname[0]) {
        if (s_info.fattrib & AM_DIR) continue;
        if (s_info.fname[0] == '.' && s_info.fname[1] == '_') continue;
        if (!is_cas(s_info.fname)) continue;
        tapeio_entry_t *e = &out[n];
        if (snprintf(e->path, sizeof e->path, "%s/%s", TAPEIO_DIR, s_info.fname) >=
            (int)sizeof e->path)
            continue;
        e->size = (uint32_t)s_info.fsize;
        n++;
    }
    f_closedir(&s_dir);
    for (unsigned i = 0; i < n; i++) first_file(out[i].path, out[i].name, &out[i].type);
    return n;
}

/* ---- the deck ---------------------------------------------------------- */

static const char *load(coco_t *m, const char *path) {
    const char *why = NULL;
    int32_t n = read_file(path, s_buf, sizeof s_buf, &why);
    if (n < 0) return why;
    snprintf(s_path, sizeof s_path, "%s", path);
    s_in = true;
    s_len = (uint32_t)n;
    coco_tape_insert(m, (uint32_t)n);
    s_rewind_blocks = NO_REWIND;
    s_failed = false;
    keymapio_file_loaded(path);   /* a layout may name it (design.md §11.3) */
    log_core1("  tape         : %s in the deck, %ld bytes\n", path, (long)n);
    return NULL;
}

const char *tapeio_boot(const char *path) {
    char full[COCO_PATH_MAX + 16];
    if (path[0] == '/') snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "%s/%s", TAPEIO_DIR, path);
    const char *why = NULL;
    int32_t n = read_file(full, s_buf, sizeof s_buf, &why);
    if (n < 0) return why;
    if (snprintf(s_path, sizeof s_path, "%s", full) >= (int)sizeof s_path) return "NAME TOO LONG";
    s_in = true;
    s_len = (uint32_t)n;
    s_chosen = true;
    keymapio_file_loaded(full);
    log_core1("  tape         : %s in the deck at boot, %ld bytes\n", full, (long)n);
    return NULL;
}

const char *tapeio_insert(coco_t *m, const char *path) {
    if (tapeio_unsaved(m)) return "RECORDING NOT SAVED";
    s_chosen = false;
    if (!path || !path[0]) {
        coco_tape_eject(m);
        s_in = false;
        s_len = 0;
        s_path[0] = 0;
        log_core1("  tape         : ejected\n");
        return NULL;
    }
    const char *err = load(m, path);
    if (!err) s_chosen = true;
    return err;
}

const char *tapeio_inserted(void) {
    return s_in ? s_path : "";
}

bool tapeio_chosen(void) {
    return s_in && s_chosen && s_path[0];
}

void tapeio_media(const coco_t *m, snap_media_t *md) {
    snprintf(md->tape, sizeof md->tape, "%s", tapeio_inserted());
    md->tape_pos = m->tape.pos;
    md->tape_rewind = s_rewind_blocks;
    md->tape_chosen = s_chosen;
}

const char *tapeio_restore(coco_t *m, const snap_media_t *md) {
    s_chosen = false;
    if (!md->tape[0]) {
        coco_tape_eject(m);
        s_in = false;
        s_len = 0;
        s_path[0] = 0;
        return NULL;
    }
    const char *why = load(m, md->tape);
    if (why) {
        coco_tape_eject(m);
        s_in = false;
        s_len = 0;
        s_path[0] = 0;
        return why;
    }
    m->tape.pos = md->tape_pos <= m->tape.len ? md->tape_pos : m->tape.len;
    s_rewind_blocks = md->tape_rewind;
    s_chosen = md->tape_chosen;
    log_core1("  tape         : at %lu of %lu, as the state left it\n", (unsigned long)m->tape.pos,
              (unsigned long)m->tape.len);
    return NULL;
}

void tapeio_rewind(coco_t *m) {
    coco_tape_rewind(m);
    s_rewind_blocks = NO_REWIND;
}

static bool exists(const char *path) {
    return f_stat(path, &s_info) == FR_OK;
}

void tapeio_next_name(char out[COCO_MEDIA_NAME_MAX + 1]) {
    char path[COCO_PATH_MAX];
    for (unsigned i = 1; i < 100; i++) {
        snprintf(out, COCO_MEDIA_NAME_MAX + 1, "TAPE%02u", i);
        snprintf(path, sizeof path, "%s/%s.CAS", TAPEIO_DIR, out);
        if (!exists(path)) return;
    }
    out[0] = 0;
}

const char *tapeio_new(coco_t *m, const char *name) {
    if (tapeio_unsaved(m)) return "RECORDING NOT SAVED";
    char path[COCO_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s.CAS", TAPEIO_DIR, name);
    if (exists(path)) return "THAT NAME IS TAKEN";
    if (!write_file(path, s_buf, 0)) return "CANNOT WRITE";
    snprintf(s_path, sizeof s_path, "%s", path);
    s_in = true;
    s_len = 0;
    s_chosen = true;
    coco_tape_insert(m, 0);
    s_rewind_blocks = NO_REWIND;
    log_core1("  tape         : new tape %s in the deck\n", path);
    return NULL;
}

/* ---- the requests ------------------------------------------------------ */

/* The file the guest named: NAME.CAS, or the first tape whose first file
 * is NAME. */
static bool find(coco_t *m, const uint8_t *want, unsigned len) {
    char name[CAS_NAME_LEN + 1];
    memcpy(name, want, len);
    name[len] = 0;
    for (int i = (int)len - 1; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    if (!name[0]) return false;
    char path[COCO_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s.CAS", TAPEIO_DIR, name);
    if (exists(path) && !load(m, path)) return true;

    static tapeio_entry_t list[COCO_TAPE_LIST_MAX];
    unsigned n = tapeio_list(list, COCO_TAPE_LIST_MAX);
    for (unsigned i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0 && !load(m, list[i].path)) return true;
    return false;
}

static void give_up(coco_t *m, const char *fmt, const char *arg) {
    say(fmt, arg);
    g_tape_stats.gave_up++;
    coco_tape_give_up(m);
}

/* How many blocks the whole tape holds. */
static uint32_t blocks_on_tape(const coco_t *m) {
    uint32_t n = 0, at = 0;
    cas_block_t b;
    while (cas_next_block(s_buf, m->tape.len, at, &b)) {
        n++;
        at = b.next;
    }
    return n;
}

static void serve_end(coco_t *m) {
    coco_tape_t *t = &m->tape;
    /* The name CLOAD is looking for, if it is looking for one. */
    uint8_t want[CAS_NAME_LEN];
    unsigned len = peek(m, 0x1D1u);
    if (len > CAS_NAME_LEN) len = CAS_NAME_LEN;
    for (unsigned i = 0; i < CAS_NAME_LEN; i++) want[i] = peek(m, (uint16_t)(0x1D2u + i));
    if (len && !cas_has_name(s_buf, t->len, want, len)) {
        char name[CAS_NAME_LEN + 1];
        memcpy(name, want, len);
        name[len] = 0;
        give_up(m, "TAPE: NO %s ON THIS TAPE", name);
        return;
    }
    if (s_rewind_blocks != NO_REWIND && t->blocks_in - s_rewind_blocks >= blocks_on_tape(m)) {
        give_up(m, "TAPE: AT ITS END%s", "");
        return;
    }
    s_rewind_blocks = t->blocks_in;
    coco_tape_rewind(m);
    g_tape_stats.rewinds++;
    log_core1("  tape         : at its end; rewound\n");
    coco_tape_resume(m);
}

/* The recording out: to the tape's file, or, for a tape begun with the
 * deck empty, to NAME.CAS for its first file, appended to what is there. */
static void write_out(coco_t *m) {
    coco_tape_t *t = &m->tape;
    char path[COCO_PATH_MAX];
    if (s_path[0]) {
        snprintf(path, sizeof path, "%s", s_path);
    } else {
        char name[CAS_NAME_LEN + 1];
        if (!cas_first_name(s_buf, t->len, name) || !name[0]) snprintf(name, sizeof name, "TAPE");
        for (char *p = name; *p; p++)
            if (strchr("/\\:*?\"<>|", *p)) *p = '_';
        snprintf(path, sizeof path, "%s/%s.CAS", TAPEIO_DIR, name);
        if (exists(path)) {
            uint32_t size = (uint32_t)s_info.fsize, rec = t->len;
            if (size + rec > sizeof s_buf) {
                s_failed = true;
                s_failed_len = t->len;
                s_failed_card = card_changes();
                say("TAPE: %.12s TOO LONG TO ADD TO", base(path));
                return;
            }
            memmove(&s_buf[size], s_buf, rec);
            const char *why;
            if (read_file(path, s_buf, size, &why) != (int32_t)size) {
                memmove(s_buf, &s_buf[size], rec);
                s_failed = true;
                s_failed_len = t->len;
                s_failed_card = card_changes();
                say("TAPE: %.14s CANNOT BE READ", base(path));
                return;
            }
            t->len = size + rec;
        }
    }
    uint32_t len = t->len;
    if (!write_file(path, s_buf, len)) {
        s_failed = true;
        s_failed_len = len;
        s_failed_card = card_changes();
        g_tape_stats.errors++;
        say("TAPE: NOT SAVED, %s", "CARD FAILED");
        return;
    }
    s_failed = false;
    s_in = true;
    s_len = len;
    if (!s_path[0]) {
        snprintf(s_path, sizeof s_path, "%s", path);
        coco_tape_insert(m, len);   /* the deck holds the file now, at its start */
    } else {
        coco_tape_saved(m);
    }
    g_tape_stats.saves++;
    say("TAPE: SAVED TO %.18s", base(path));
}

void tapeio_serve(coco_t *m) {
    uint32_t t0 = time_us_32();
    bool card = sd_present() && storage_mount() == 0;
    tape_op_t op = coco_tape_pending(m);
    if (op == TAPE_FIND) {
        g_tape_stats.finds++;
        const coco_tape_t *t = &m->tape;
        char name[CAS_NAME_LEN + 1];
        memcpy(name, t->want, t->want_len);
        name[t->want_len] = 0;
        if (!card) give_up(m, "TAPE: NO CARD%s", "");
        else if (!t->want_len) give_up(m, "TAPE: THE DECK IS EMPTY%s", "");
        else if (!find(m, t->want, t->want_len)) give_up(m, "TAPE: NO FILE %s", name);
        else {
            s_chosen = false;
            say("TAPE: %.26s", base(s_path));
            coco_tape_resume(m);
        }
    } else if (op == TAPE_END) {
        serve_end(m);
    } else if (tapeio_unsaved(m)) {
        if (card) write_out(m);
        else {
            s_failed = true;
            s_failed_len = m->tape.len;
            s_failed_card = card_changes();
            say("TAPE: NO CARD TO SAVE TO%s", "");
        }
    }
    if (card) storage_unmount();
    uint32_t us = time_us_32() - t0;
    g_tape_stats.last_us = us;
    if (us > g_tape_stats.max_us) g_tape_stats.max_us = us;
}
