/* guest.c — a real CoCo, on the host (guest.h). */

#include "guest.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_ROWS 16
#define SCREEN_COLS 32
#define ROM_MAX     0x4000u

/* Typing: whole fields held, and whole fields between keys. Generous until
 * M5 settles each ROM's hold and gap (§11.1, §18). */
#define HOLD_FIELDS 4
#define GAP_FIELDS  4

static uint8_t images[ROM_IMAGE_COUNT][ROM_MAX];
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
        static uint8_t buf[ROM_MAX + 1];
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

void guest_build(guest_t *g, unsigned ram_k, bool extended) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = ram_k;
    coco_init(&g->m, &cfg);
    coco_load_rom(&g->m, 0xA000u, images[ROM_COCO_BAS13], romset_images[ROM_COCO_BAS13].size);
    if (extended && have[ROM_COCO_EXTBAS11])
        coco_load_rom(&g->m, 0x8000u, images[ROM_COCO_EXTBAS11],
                      romset_images[ROM_COCO_EXTBAS11].size);
    coco_power_on(&g->m);
    g->ok_ticks = 0;
}

void guest_fields(guest_t *g, int n) {
    for (int i = 0; i < n; i++) coco_run_field(&g->m);
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

/* The CoCo's matrix by row (PA0-PA6) and column (PB0-PB7), as XRoar has it
 * (§11.2, §18): settled for the keys typed here by the ROM printing them. */
static const char *const coco_rows[COCO_KEY_ROWS] = {
    "@ABCDEFG",
    "HIJKLMNO",
    "PQRSTUVW",
    "XYZ\x01\x02\x03\x04 ",   /* up, down, left, right, space */
    "01234567",
    "89:;,-./",
    "\n\x05\x06\0\0\0\0\x07",   /* ENTER, CLEAR, BREAK, SHIFT at column 7 */
};

#define SHIFT_ROW 6
#define SHIFT_COL 7

/* The shifted characters, and the key each is on. */
static const char shifted_from[] = "!\"#$%&'()*+<=>?";
static const char shifted_on[]   = "123456789:;,-./";

static bool find_key(char ch, int *row, int *col) {
    for (int r = 0; r < COCO_KEY_ROWS; r++)
        for (int c = 0; c < COCO_KEY_COLS; c++)
            if (coco_rows[r][c] == ch && !(ch == 0)) {
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

void guest_type(guest_t *g, const char *s) {
    for (; *s; s++) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        int row, col;
        const char *sh = strchr(shifted_from, ch);
        if (sh && find_key(shifted_on[sh - shifted_from], &row, &col)) press(g, row, col, true);
        else if (find_key(ch, &row, &col)) press(g, row, col, false);
    }
    guest_fields(g, 10);
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
