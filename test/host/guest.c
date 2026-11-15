/* guest.c — a real CoCo or Dragon, on the host (guest.h). */

#include "guest.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_ROWS 16
#define SCREEN_COLS 32

/* The Dragon's typing: whole fields held, and whole fields between keys,
 * as M3 left it (§17: the Dragon is frozen until M14). */
#define HOLD_FIELDS 4
#define GAP_FIELDS  4

static uint8_t images[ROM_IMAGE_COUNT][COCO_ROM_IMAGE_MAX];
static bool    have[ROM_IMAGE_COUNT];

static void scan_roms(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static uint8_t buf[COCO_ROM_IMAGE_MAX + 1];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        rom_id_t r = romset_identify(buf, n);
        if (r == ROM_UNKNOWN) continue;
        memcpy(images[r], buf, n);
        have[r] = true;
    }
    closedir(d);
}

bool guest_find_roms(const char **dir) {
    const char *env = getenv("PICO_COCO_ROMS");
    const char *where = env ? env : PICO_COCO_DEFAULT_ROMS;
    if (dir) *dir = where;
    scan_roms(where);
    return have[ROM_COCO_BAS13];
}

bool guest_have_rom(rom_id_t r) {
    return r >= 0 && r < ROM_IMAGE_COUNT && have[r];
}

const uint8_t *guest_rom_image(rom_id_t r) {
    return guest_have_rom(r) ? images[r] : NULL;
}

void guest_build_as(guest_t *g, coco_machine_t machine, unsigned ram_k, rom_id_t basic,
                    bool extended) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.machine = machine;
    cfg.ram_k = ram_k;
    coco_init(&g->m, &cfg);
    uint32_t size = romset_images[basic].size;
    coco_load_rom(&g->m, size == COCO_ROM_SIZE ? 0x8000u : 0xA000u, images[basic], size);
    if (extended && have[ROM_COCO_EXTBAS11])
        coco_load_rom(&g->m, 0x8000u, images[ROM_COCO_EXTBAS11],
                      romset_images[ROM_COCO_EXTBAS11].size);
    coco_power_on(&g->m);
    keymatrix_init(&g->k);
    g->ok_ticks = 0;
}

void guest_build(guest_t *g, unsigned ram_k, bool extended) {
    guest_build_as(g, COCO_MACHINE_COCO, ram_k, ROM_COCO_BAS13, extended);
}

static bool is_coco(const guest_t *g) {
    return g->m.cfg.machine == COCO_MACHINE_COCO;
}

void guest_fields(guest_t *g, int n) {
    for (int i = 0; i < n; i++) {
        if (is_coco(g)) keymatrix_field(&g->k, &g->m);
        coco_run_field(&g->m);
    }
}

bool guest_settle(guest_t *g, int max) {
    for (int f = 0; f < max && !keymatrix_idle(&g->k); f++) guest_fields(g, 1);
    return keymatrix_idle(&g->k);
}

bool guest_boot(guest_t *g) {
    uint64_t t0 = coco_ticks(&g->m);
    for (int f = 0; f < 600; f++) {
        coco_run_field(&g->m);
        if (guest_find_row(&g->m, "OK", 0) >= 0) {
            g->ok_ticks = coco_ticks(&g->m) - t0;
            guest_fields(g, 5);
            return true;
        }
    }
    return false;
}

/* ---- the keyboard ---------------------------------------------------- */

/* The Dragon's matrix by row (PA0-PA6) and column (PB0-PB7): the CoCo's
 * columns, the rows turned by two, the digits first (XRoar's dkbd.c;
 * settled for the keys typed here by Dragon 32 BASIC, §11.2, §18). */
static const char *const dragon_rows[COCO_KEY_ROWS] = {
    "01234567",
    "89:;,-./",
    "@ABCDEFG",
    "HIJKLMNO",
    "PQRSTUVW",
    "XYZ\x01\x02\x03\x04 ",
    "\n\x05\x06\0\0\0\0\x07",
};

#define SHIFT_ROW 6
#define SHIFT_COL 7

/* The shifted characters, and the key each is on. */
static const char shifted_from[] = "!\"#$%&'()*+<=>?";
static const char shifted_on[]   = "123456789:;,-./";

static bool find_key(char ch, int *row, int *col) {
    for (int r = 0; r < COCO_KEY_ROWS; r++)
        for (int c = 0; c < COCO_KEY_COLS; c++)
            if (dragon_rows[r][c] == ch && ch != 0) {
                *row = r;
                *col = c;
                return true;
            }
    return false;
}

static void press(guest_t *g, int row, int col, bool shift) {
    if (shift) coco_key_set(&g->m, SHIFT_ROW, SHIFT_COL, true);
    coco_key_set(&g->m, row, col, true);
    guest_fields(g, HOLD_FIELDS);
    coco_key_set(&g->m, row, col, false);
    if (shift) coco_key_set(&g->m, SHIFT_ROW, SHIFT_COL, false);
    guest_fields(g, GAP_FIELDS);
}

static void dragon_type(guest_t *g, const char *s) {
    for (; *s; s++) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        int row, col;
        const char *sh = strchr(shifted_from, ch);
        if (sh && find_key(shifted_on[sh - shifted_from], &row, &col)) press(g, row, col, true);
        else if (find_key(ch, &row, &col)) press(g, row, col, false);
    }
}

void guest_type(guest_t *g, const char *s) {
    if (!is_coco(g)) {
        dragon_type(g, s);
    } else {
        for (; *s; s++) {
            uint8_t ch = (uint8_t)*s;
            if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 'a');
            picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
            unsigned n = keymap_picocalc_text(ch, ev);
            if (n == 0) fprintf(stderr, "guest_type: no PicoCalc key for 0x%02X\n", ch);
            for (unsigned i = 0; i < n; i++) keymatrix_event(&g->k, ev[i].state, ev[i].code);
            if (!guest_settle(g, 100))
                fprintf(stderr, "guest_type: 0x%02X still held after 100 fields\n", ch);
        }
    }
    guest_fields(g, 10);
}

void guest_press(guest_t *g, uint8_t code, bool shift, bool alt) {
    if (alt)   keymatrix_event(&g->k, KEY_EV_PRESSED, PICOCALC_KEY_ALT);
    if (shift) keymatrix_event(&g->k, KEY_EV_PRESSED, PICOCALC_KEY_SHIFT_L);
    keymatrix_event(&g->k, KEY_EV_PRESSED, code);
    keymatrix_event(&g->k, KEY_EV_RELEASED, code);
    if (shift) keymatrix_event(&g->k, KEY_EV_RELEASED, PICOCALC_KEY_SHIFT_L);
    if (alt)   keymatrix_event(&g->k, KEY_EV_RELEASED, PICOCALC_KEY_ALT);
    if (!guest_settle(g, 100)) fprintf(stderr, "guest_press: 0x%02X still held\n", code);
}

/* ---- the screen ------------------------------------------------------ */

/* A text cell: bit 7 a semigraphics block; otherwise six bits of the
 * VDG's character set, which runs @ A-Z [ \ ] up-arrow left-arrow, then
 * space to ?, with bit 6 clear for inverse (§3.4, §9.2). */
static char cell_char(uint8_t b) {
    if (b & 0x80u) return ' ';
    uint8_t c = b & 0x3Fu;
    return (char)(c < 0x20u ? c + 0x40u : c);
}

const char *guest_row(const coco_t *m, int row) {
    static char buf[SCREEN_COLS + 1];
    uint16_t base = (uint16_t)(sam6883_display_base(&m->sam) + row * SCREEN_COLS);
    int n = 0;
    for (int c = 0; c < SCREEN_COLS; c++) {
        int32_t i = coco_ram_index(m, (uint16_t)(base + c));
        buf[n++] = i < 0 ? ' ' : cell_char(m->ram[i]);
    }
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = 0;
    return buf;
}

int guest_find_row(const coco_t *m, const char *s, int from) {
    for (int r = from; r < SCREEN_ROWS; r++)
        if (strncmp(guest_row(m, r), s, strlen(s)) == 0) return r;
    return -1;
}

void guest_dump(const coco_t *m, FILE *f) {
    for (int r = 0; r < SCREEN_ROWS; r++) fprintf(f, "  |%-32s|\n", guest_row(m, r));
}
