/* settings.c — defaults and the settings file (settings.h, design.md §12.6).
 *
 * pico-oric's, with the CoCo's keys. */

#include "settings.h"

#include <ctype.h>
#include <string.h>

void settings_default(settings_t *s) {
    memset(s, 0, sizeof *s);
    coco_config_t cfg;
    coco_config_default(&cfg);
    s->extended  = true;        /* Extended Color BASIC over 1.3 (D6) */
    s->ram_k     = cfg.ram_k;
    s->volume    = 8u;
    s->perf      = false;
    s->status    = true;
    s->backlight = 0u;
    s->fast_tape = true;
    s->vdg_t1    = false;       /* the MC6847 of the CoCo 1 and 2 (§9.2) */
    s->artefacts = SETTINGS_ARTEFACTS_SIMPLE;   /* §9.5 */
    s->artefact_phase = SETTINGS_PHASE_BLUE;
    s->dark_bg   = true;        /* the datasheet's backgrounds (§9.5) */
    for (unsigned c = 0; c < VDG_COLOUR_COUNT; c++) s->colour[c] = SETTINGS_COLOUR_NONE;
    s->disk_sounds = true;      /* the drives heard (§10) */
    s->mpi_switch = 1u + cfg.mpi_switch;   /* slot 4 (§12.8) */
}

/* strcasecmp is POSIX, not C11. */
static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    }
    return *a == *b;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static settings_status_t on_off(const char *v, bool *out) {
    if (same_name(v, "on"))  { *out = true;  return SET_OK; }
    if (same_name(v, "off")) { *out = false; return SET_OK; }
    return SET_BAD_VALUE;
}

static settings_status_t vdg(const char *v, bool *t1) {
    if (same_name(v, "6847"))   { *t1 = false; return SET_OK; }
    if (same_name(v, "6847t1")) { *t1 = true;  return SET_OK; }
    return SET_BAD_VALUE;
}

/* pico-atom's words: the datasheet's dark green or orange, or black. */
static settings_status_t background(settings_t *s, const char *v) {
    if (same_name(v, "dark"))  { s->dark_bg = true;  return SET_OK; }
    if (same_name(v, "black")) { s->dark_bg = false; return SET_OK; }
    return SET_BAD_VALUE;
}

/* Six hex digits, RRGGBB, with $ or 0x before them if the writer likes.
 * Empty is refused: it is what `#00FF00` leaves, # starting a comment. */
static settings_status_t colour(const char *v, uint32_t *out) {
    if (!*v) return SET_BAD_VALUE;
    if (*v == '$') v++;
    else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
    uint32_t n = 0;
    unsigned i = 0;
    for (; v[i]; i++) {
        char c = v[i];
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0')
                   : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                   : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16u;
        if (d > 15u || i >= 6u) return SET_BAD_VALUE;
        n = n << 4 | d;
    }
    if (i != 6u) return SET_BAD_VALUE;
    *out = n;
    return SET_OK;
}

/* A value from a list of names, its index into *out. */
static settings_status_t one_of(const char *v, const char *const *names, unsigned n,
                                uint8_t *out) {
    for (unsigned i = 0; i < n; i++)
        if (same_name(v, names[i])) { *out = (uint8_t)i; return SET_OK; }
    return SET_BAD_VALUE;
}

/* In SETTINGS_ARTEFACTS_* and SETTINGS_PHASE_* order. */
static const char *const k_artefacts[] = { "off", "simple", "full" };
static const char *const k_phases[] = { "blue", "red", "random" };

const char *settings_artefacts_str(unsigned a) {
    return a < 3u ? k_artefacts[a] : "simple";
}

const char *settings_phase_str(unsigned p) {
    return p < 3u ? k_phases[p] : "blue";
}

static settings_status_t number(const char *v, unsigned lo, unsigned hi, unsigned *out) {
    unsigned n = 0;
    if (!*v) return SET_BAD_VALUE;
    for (; *v; v++) {
        if (!isdigit((unsigned char)*v) || n > hi) return SET_BAD_VALUE;
        n = n * 10u + (unsigned)(*v - '0');
    }
    if (n < lo || n > hi) return SET_BAD_VALUE;
    *out = n;
    return SET_OK;
}

static settings_status_t path(const char *v, char out[COCO_PATH_MAX]) {
    size_t n = strlen(v);
    if (n >= COCO_PATH_MAX) return SET_TOO_LONG;
    memcpy(out, v, n + 1);
    return SET_OK;
}

/* The machine's rows by the names settings.h gives them. */
static const unsigned k_ram_k[] = { 4u, 16u, 32u, 64u };
static const char *const k_rams[] = { "4", "16", "32", "64" };

const char *settings_basic_str(bool extended) {
    return extended ? "extended" : "color";
}

const char *settings_ram_str(unsigned ram_k) {
    for (unsigned r = 0; r < sizeof k_ram_k / sizeof k_ram_k[0]; r++)
        if (k_ram_k[r] == ram_k) return k_rams[r];
    return "64";
}

static settings_status_t basic(settings_t *s, const char *v) {
    if (same_name(v, "extended")) { s->extended = true;  return SET_OK; }
    if (same_name(v, "color"))    { s->extended = false; return SET_OK; }
    return SET_BAD_VALUE;
}

static settings_status_t ram(settings_t *s, const char *v) {
    for (unsigned r = 0; r < sizeof k_rams / sizeof k_rams[0]; r++) {
        if (strcmp(v, k_rams[r]) == 0) { s->ram_k = k_ram_k[r]; return SET_OK; }
    }
    return SET_BAD_VALUE;
}

static settings_status_t layout(settings_t *s, const char *v) {
    if (same_name(v, "standard")) { s->layout[0] = 0; return SET_OK; }
    size_t n = strlen(v);
    if (n > COCO_KEYMAP_NAME_LEN) return SET_TOO_LONG;
    for (size_t i = 0; i <= n; i++) s->layout[i] = (char)toupper((unsigned char)v[i]);
    return SET_OK;
}

/* Every setting the file may give, in settings.h's order. Only the
 * cartridge, the boot tape and the boot disk may be left empty, meaning
 * none. */
enum { K_BASIC, K_RAM, K_CART, K_VOLUME, K_PERF, K_STATUS, K_BACKLIGHT, K_LAYOUT, K_FAST_TAPE,
       K_VDG, K_SMARTWATCH, K_DISK_SOUNDS, K_ARTIFACTS, K_ARTIFACT_PHASE, K_BACKGROUND, K_BOOT_TAPE,
       K_BOOT_DISK, K_BOOT_DISK1, K_BOOT_DISK2, K_BOOT_DISK3,
       K_SLOT1, K_SLOT2, K_SLOT3, K_SLOT4, K_MPI_SWITCH,
       K_COLOUR, K_COUNT = K_COLOUR + VDG_COLOUR_COUNT };
_Static_assert(K_SLOT4 - K_SLOT1 + 1 == COCO_MPI_SLOTS, "a key for each Multi-Pak slot");
_Static_assert(K_BOOT_DISK3 - K_BOOT_DISK + 1 == COCO_DISK_DRIVES, "a key for each drive");

/* The keys a pass over the file has seen, one bit each. */
typedef uint64_t keys_t;
#define KEY(k) ((keys_t)1u << (k))
_Static_assert(K_COUNT <= 64, "the keys seen fit a keys_t");

/* The artefacts' keys are spelt as the CoCo's own manuals and its
 * users spell the word. */
static const char *const k_names[K_COUNT] = {
    "basic", "ram", "cart", "volume", "perf", "status", "backlight", "layout", "fast_tape",
    "vdg", "smartwatch", "disk_sounds", "artifacts", "artifact_phase", "background", "boot_tape",
    "boot_disk", "boot_disk1", "boot_disk2", "boot_disk3",
    "slot1", "slot2", "slot3", "slot4", "mpi_switch",
    /* In vdg_colour_t's order. */
    "color_green", "color_yellow", "color_blue", "color_red", "color_buff", "color_cyan",
    "color_magenta", "color_orange", "color_black", "color_dark_green", "color_dark_orange",
    "color_text_orange", "color_artifact_blue", "color_artifact_red",
};
_Static_assert(VDG_COLOUR_COUNT == 14, "a key for each VDG colour");

static settings_status_t apply(settings_t *s, unsigned k, const char *v) {
    bool disk = k >= K_BOOT_DISK && k <= K_BOOT_DISK3;
    bool slot = k >= K_SLOT1 && k <= K_SLOT4;
    if (k >= K_COLOUR) return colour(v, &s->colour[k - K_COLOUR]);
    if (!*v && k != K_CART && k != K_BOOT_TAPE && !disk && !slot) return SET_SYNTAX;
    if (disk) return path(v, s->boot_disk[k - K_BOOT_DISK]);
    if (slot) return path(v, s->slot[k - K_SLOT1]);
    if (k == K_MPI_SWITCH) return number(v, 1u, COCO_MPI_SLOTS, &s->mpi_switch);
    switch (k) {
    case K_BASIC:     return basic(s, v);
    case K_RAM:       return ram(s, v);
    case K_CART:      return path(v, s->cart);
    case K_VOLUME:    return number(v, 0u, 8u, &s->volume);
    case K_PERF:      return on_off(v, &s->perf);
    case K_STATUS:    return on_off(v, &s->status);
    case K_BACKLIGHT: return number(v, 1u, 15u, &s->backlight);
    case K_LAYOUT:    return layout(s, v);
    case K_FAST_TAPE: return on_off(v, &s->fast_tape);
    case K_VDG:       return vdg(v, &s->vdg_t1);
    case K_SMARTWATCH: {
        /* Retired (design.md §12.7): the SmartWatch is a cartridge in a
         * Multi-Pak slot. Its old values are taken and change nothing, so
         * a file that has one still loads. */
        bool ignored;
        return on_off(v, &ignored);
    }
    case K_DISK_SOUNDS: return on_off(v, &s->disk_sounds);
    case K_ARTIFACTS: return one_of(v, k_artefacts, 3u, &s->artefacts);
    case K_ARTIFACT_PHASE: return one_of(v, k_phases, 3u, &s->artefact_phase);
    case K_BACKGROUND: return background(s, v);
    case K_BOOT_TAPE: return path(v, s->boot_tape);
    }
    return SET_UNKNOWN;
}

/* A `#` at the start of a line or after a space starts a comment, so a
 * value may be annotated and a path may still hold a `#` (EL §8.7). */
static void strip_comment(char *line) {
    for (char *p = line; *p; p++) {
        if (*p == '#' && (p == line || isspace((unsigned char)p[-1]))) {
            *p = 0;
            return;
        }
    }
}

/* One line, NUL-terminated and writable. *key is the setting it named,
 * or K_COUNT if it named none. */
static settings_status_t parse_line(settings_t *s, char *line, keys_t *seen, unsigned *key) {
    *key = K_COUNT;
    strip_comment(line);
    char *t = trim(line);
    if (!*t) return SET_OK;

    char *eq = strchr(t, '=');
    if (!eq) return SET_SYNTAX;
    *eq = 0;
    char *name = trim(t), *val = trim(eq + 1);
    if (!*name) return SET_SYNTAX;

    for (unsigned k = 0; k < K_COUNT; k++) {
        if (!same_name(name, k_names[k])) continue;
        *key = k;
        if (*seen & KEY(k)) return SET_DUPLICATE;
        settings_status_t st = apply(s, k, val);
        if (st == SET_OK) *seen |= KEY(k);
        return st;
    }
    return SET_UNKNOWN;
}

/* A line of `n` bytes as the parser sees it: too long, not text, or
 * parsed over *s. */
static settings_status_t classify(settings_t *s, const char *text, size_t n, keys_t *seen,
                                  unsigned *key) {
    char buf[COCO_SETTINGS_LINE_MAX + 1];
    *key = K_COUNT;
    if (n > COCO_SETTINGS_LINE_MAX) return SET_TOO_LONG;
    memcpy(buf, text, n);
    buf[n] = 0;
    /* A NUL inside the line is not text. */
    if (strlen(buf) != n) return SET_SYNTAX;
    return parse_line(s, buf, seen, key);
}

settings_status_t settings_parse(settings_t *s, const char *text, size_t len,
                                 unsigned *line) {
    settings_status_t first = SET_OK;
    keys_t seen = 0;
    unsigned n_line = 0;
    *line = 0;

    size_t at = 0;
    while (at < len) {
        n_line++;
        size_t end = at;
        while (end < len && text[end] != '\n') end++;

        unsigned key;
        settings_status_t st = classify(s, text + at, end - at, &seen, &key);
        if (st != SET_OK && first == SET_OK) {
            first = st;
            *line = n_line;
        }
        at = end + 1;
    }
    return first;
}

const char *settings_status_str(settings_status_t st) {
    switch (st) {
    case SET_OK:        return "ok";
    case SET_SYNTAX:    return "not KEY = VALUE";
    case SET_UNKNOWN:   return "no such setting";
    case SET_BAD_VALUE: return "no such value";
    case SET_DUPLICATE: return "given twice";
    case SET_TOO_LONG:  return "too long";
    case SET_MISMATCH:  return "would not read back";
    }
    return "?";
}

/* ---- saving the menu's settings (§14) ------------------------------------- */

/* A bare name is in `dir`; FAT's names are the same in either case. */
static bool same_path(const char *dir, const char *a, const char *b) {
    char pa[COCO_PATH_MAX + 16], pb[COCO_PATH_MAX + 16];
    const char *x[2] = { a, b };
    char *p[2] = { pa, pb };
    for (unsigned i = 0; i < 2; i++) {
        size_t n = strlen(x[i]);
        if (!n || x[i][0] == '/') {
            memcpy(p[i], x[i], n + 1);
        } else {
            size_t d = strlen(dir);
            memcpy(p[i], dir, d);
            p[i][d] = '/';
            memcpy(p[i] + d + 1, x[i], n + 1);
        }
    }
    return same_name(pa, pb);
}

/* b is the settings being saved, whose backlight of 0 says "the file's,
 * whatever it is": equal to anything, so the line is kept. */
static bool key_equal(unsigned k, const settings_t *a, const settings_t *b) {
    if (k >= K_COLOUR) return a->colour[k - K_COLOUR] == b->colour[k - K_COLOUR];
    switch (k) {
    case K_BASIC:     return a->extended == b->extended;
    case K_RAM:       return a->ram_k == b->ram_k;
    case K_CART:      return same_path(SETTINGS_CART_DIR, a->cart, b->cart);
    case K_VOLUME:    return a->volume == b->volume;
    case K_PERF:      return a->perf == b->perf;
    case K_STATUS:    return a->status == b->status;
    case K_BACKLIGHT: return b->backlight == 0 || a->backlight == b->backlight;
    case K_LAYOUT:    return strcmp(a->layout, b->layout) == 0;
    case K_FAST_TAPE: return a->fast_tape == b->fast_tape;
    case K_VDG:       return a->vdg_t1 == b->vdg_t1;
    case K_SMARTWATCH: return true;   /* retired: the line is kept as it is */
    case K_DISK_SOUNDS: return a->disk_sounds == b->disk_sounds;
    case K_ARTIFACTS: return a->artefacts == b->artefacts;
    case K_ARTIFACT_PHASE: return a->artefact_phase == b->artefact_phase;
    case K_BACKGROUND: return a->dark_bg == b->dark_bg;
    case K_BOOT_TAPE: return same_path(SETTINGS_TAPE_DIR, a->boot_tape, b->boot_tape);
    case K_BOOT_DISK: case K_BOOT_DISK1: case K_BOOT_DISK2: case K_BOOT_DISK3:
        return same_path(SETTINGS_DISK_DIR, a->boot_disk[k - K_BOOT_DISK],
                         b->boot_disk[k - K_BOOT_DISK]);
    case K_SLOT1: case K_SLOT2: case K_SLOT3: case K_SLOT4:
        return same_path(SETTINGS_CART_DIR, a->slot[k - K_SLOT1], b->slot[k - K_SLOT1]);
    case K_MPI_SWITCH: return a->mpi_switch == b->mpi_switch;
    }
    return true;
}

/* The value a save writes for k. */
static const char *value_of(unsigned k, const settings_t *s, char num[8]) {
    if (k >= K_COLOUR) {
        uint32_t c = s->colour[k - K_COLOUR];
        if (c == SETTINGS_COLOUR_NONE) return "";   /* never written: unsayable */
        for (unsigned i = 0; i < 6u; i++) num[i] = "0123456789ABCDEF"[(c >> (20u - 4u * i)) & 15u];
        num[6] = 0;
        return num;
    }
    switch (k) {
    case K_BASIC:     return settings_basic_str(s->extended);
    case K_RAM:       return settings_ram_str(s->ram_k);
    case K_CART:      return s->cart;
    case K_VOLUME:
    case K_BACKLIGHT: {
        unsigned v = k == K_VOLUME ? s->volume : s->backlight;
        num[0] = (char)('0' + v / 10u % 10u);
        num[1] = (char)('0' + v % 10u);
        num[2] = 0;
        return v < 10u ? num + 1 : num;
    }
    case K_PERF:      return s->perf ? "on" : "off";
    case K_STATUS:    return s->status ? "on" : "off";
    case K_LAYOUT:    return s->layout[0] ? s->layout : "standard";
    case K_FAST_TAPE: return s->fast_tape ? "on" : "off";
    case K_VDG:       return s->vdg_t1 ? "6847t1" : "6847";
    case K_DISK_SOUNDS: return s->disk_sounds ? "on" : "off";
    case K_ARTIFACTS: return settings_artefacts_str(s->artefacts);
    case K_ARTIFACT_PHASE: return settings_phase_str(s->artefact_phase);
    case K_BACKGROUND: return s->dark_bg ? "dark" : "black";
    case K_BOOT_TAPE: return s->boot_tape;
    case K_BOOT_DISK: case K_BOOT_DISK1: case K_BOOT_DISK2: case K_BOOT_DISK3:
        return s->boot_disk[k - K_BOOT_DISK];
    case K_SLOT1: case K_SLOT2: case K_SLOT3: case K_SLOT4:
        return s->slot[k - K_SLOT1];
    case K_MPI_SWITCH:
        num[0] = (char)('0' + s->mpi_switch % 10u);
        num[1] = 0;
        return num;
    }
    return "";
}

typedef struct {
    char  *buf;
    size_t n;
    bool   over;
} out_t;

static void emit(out_t *o, const char *p, size_t n) {
    if (o->over || n > COCO_SETTINGS_FILE_MAX - o->n) { o->over = true; return; }
    memcpy(o->buf + o->n, p, n);
    o->n += n;
}

static void emit_str(out_t *o, const char *p) {
    emit(o, p, strlen(p));
}

/* A line for key k whose value no longer says what *s does: the value
 * replaced, and everything around it kept. The value goes where the old
 * one began, or one space after the `=` if the old one was empty, and a
 * comment keeps its column if the new value leaves room, so the file's
 * columns of names, values and comments stay lined up. A gap with a tab
 * in it is kept as it was, since its width is the editor's. */
static void emit_changed(out_t *o, const char *line, size_t n, const char *value) {
    size_t body = n && line[n - 1] == '\r' ? n - 1u : n;
    size_t cend = body;
    for (size_t i = 0; i < body; i++) {
        if (line[i] == '#' && (i == 0 || isspace((unsigned char)line[i - 1]))) { cend = i; break; }
    }
    const char *eq = memchr(line, '=', cend);
    size_t after = eq ? (size_t)(eq - line) + 1u : cend;
    size_t vs = after;
    while (vs < cend && isspace((unsigned char)line[vs])) vs++;
    size_t ve = cend;
    while (ve > vs && isspace((unsigned char)line[ve - 1])) ve--;
    if (vs == ve) vs = after < cend && (line[after] == ' ' || line[after] == '\t') ? after + 1u : after;
    emit(o, line, vs);
    emit_str(o, value);
    if (cend == body) {
        /* No comment: whatever followed the value, and the line's end. */
        emit(o, line + (ve > vs ? ve : vs), n - (ve > vs ? ve : vs));
        return;
    }
    size_t gap_from = ve > vs ? ve : vs;
    if (memchr(line + gap_from, '\t', cend - gap_from)) {
        emit(o, line + gap_from, n - gap_from);
        return;
    }
    size_t col = vs + strlen(value);
    for (size_t pad = cend > col ? cend - col : 1u; pad; pad--) emit(o, " ", 1);
    emit(o, line + cend, n - cend);
}

settings_status_t settings_rewrite(const char *text, size_t len, const settings_t *s,
                                   const char **out, size_t *out_len) {
    static char buf[COCO_SETTINGS_FILE_MAX];
    out_t o = { buf, 0, false };
    const char *eol = "\n";
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') { if (i && text[i - 1] == '\r') eol = "\r\n"; break; }
    }

    settings_t def;
    settings_default(&def);

    /* The keys some line gives a good value. A line whose value the
     * parser refused is that key's line only if no other line is: then
     * the save writes the value in force over the refused one. If another
     * line is, the refused one is made a comment, keeping its text. Either
     * way the problem the status row names goes with the save. */
    keys_t good = 0;
    {
        keys_t seen0 = 0;
        for (size_t a = 0; a < len;) {
            size_t e = a;
            while (e < len && text[e] != '\n') e++;
            settings_t tmp = def;
            unsigned k;
            if (classify(&tmp, text + a, e - a, &seen0, &k) == SET_OK && k < K_COUNT)
                good |= KEY(k);
            a = e + 1;
        }
    }

    keys_t seen = 0, present = 0;
    size_t at = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && text[end] != '\n') end++;
        size_t n = end - at;

        /* The line as the parser reads it, over the defaults: what it
         * says for its key, if it names one that applies. */
        settings_t line_says = def;
        unsigned key;
        settings_status_t st = classify(&line_says, text + at, n, &seen, &key);
        if (st == SET_DUPLICATE) return SET_DUPLICATE;
        bool applies = st == SET_OK && key < K_COUNT;
        bool bad = st == SET_BAD_VALUE && key < K_COUNT;
        /* A backlight of 0 is "leave it", which no line can say: a
         * refused one has nothing to be replaced with, and is made a
         * comment like one beside the key's own line. */
        bool unsayable = (key == K_BACKLIGHT && s->backlight == 0) ||
                         (key >= K_COLOUR && key < K_COUNT &&
                          s->colour[key - K_COLOUR] == SETTINGS_COLOUR_NONE);
        bool refused = bad && !unsayable && !(good & KEY(key)) && !(present & KEY(key));

        if (bad && !refused) {
            emit_str(&o, "# ");
            emit(&o, text + at, n);
        } else if (refused || (applies && !key_equal(key, &line_says, s))) {
            char num[8];
            emit_changed(&o, text + at, n, value_of(key, s, num));
        } else {
            emit(&o, text + at, n);
        }
        if (applies || refused) present |= KEY(key);
        if (end < len) emit(&o, "\n", 1);
        at = end + 1;
    }

    for (unsigned k = 0; k < K_COUNT; k++) {
        if ((present & KEY(k)) || key_equal(k, &def, s)) continue;
        if (o.n && buf[o.n - 1] != '\n') emit_str(&o, eol);
        char num[8];
        emit_str(&o, k_names[k]);
        emit_str(&o, " = ");
        emit_str(&o, value_of(k, s, num));
        emit_str(&o, eol);
    }
    if (o.over) return SET_TOO_LONG;

    /* A rewrite that disagrees with its own reader is a bug, caught here
     * rather than at the next power-on. */
    settings_t back;
    settings_default(&back);
    unsigned line;
    (void)settings_parse(&back, buf, o.n, &line);
    for (unsigned k = 0; k < K_COUNT; k++) {
        if (!key_equal(k, &back, s)) return SET_MISMATCH;
    }
    *out = buf;
    *out_len = o.n;
    return SET_OK;
}

void settings_card_name(const char *dir, const char *path, char out[COCO_PATH_MAX]) {
    size_t d = strlen(dir), n = strlen(path);
    const char *v = path;
    if (n > d + 1u && path[d] == '/' && !strchr(path + d + 1, '/')) {
        bool in_dir = true;
        for (size_t i = 0; i < d; i++) {
            if (toupper((unsigned char)path[i]) != toupper((unsigned char)dir[i])) in_dir = false;
        }
        if (in_dir) v = path + d + 1;
    }
    size_t vn = strlen(v);
    if (vn >= COCO_PATH_MAX) vn = COCO_PATH_MAX - 1u;
    memcpy(out, v, vn);
    out[vn] = 0;
}
