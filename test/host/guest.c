/* guest.c — a real CoCo, on the host (guest.h). */

#include "guest.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "textpage.h"

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

void guest_build_as(guest_t *g, unsigned ram_k, rom_id_t basic, bool extended) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = ram_k;
    coco_init(&g->m, &cfg);
    coco_load_rom(&g->m, 0xA000u, images[basic], romset_images[basic].size);
    if (extended && have[ROM_COCO_EXTBAS11])
        coco_load_rom(&g->m, 0x8000u, images[ROM_COCO_EXTBAS11],
                      romset_images[ROM_COCO_EXTBAS11].size);
    coco_power_on(&g->m);
    keymatrix_init(&g->k);
    g->ok_ticks = 0;
    g->after_field = NULL;
}

void guest_build_disk(guest_t *g, unsigned ram_k) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = ram_k;
    cfg.cart = CART_RSDOS;
    coco_init(&g->m, &cfg);
    coco_load_rom(&g->m, 0xA000u, images[ROM_COCO_BAS13], romset_images[ROM_COCO_BAS13].size);
    coco_load_rom(&g->m, 0x8000u, images[ROM_COCO_EXTBAS11], romset_images[ROM_COCO_EXTBAS11].size);
    /* RS-DOS's CART line is not on Q (§12.2): coco_load_rom leaves it off. */
    coco_load_rom(&g->m, 0xC000u, images[ROM_COCO_DISK11], romset_images[ROM_COCO_DISK11].size);
    coco_power_on(&g->m);
    keymatrix_init(&g->k);
    g->ok_ticks = 0;
    g->after_field = NULL;
}

void guest_build_mpi(guest_t *g, unsigned ram_k, const cart_kind_t kinds[COCO_MPI_SLOTS],
                     unsigned sw, const uint8_t *const imgs[COCO_MPI_SLOTS],
                     const size_t lens[COCO_MPI_SLOTS]) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = ram_k;
    cfg.cart = CART_MPI;
    cfg.mpi_switch = (uint8_t)sw;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) cfg.slot[s] = kinds[s];
    coco_init(&g->m, &cfg);
    coco_load_rom(&g->m, 0xA000u, images[ROM_COCO_BAS13], romset_images[ROM_COCO_BAS13].size);
    coco_load_rom(&g->m, 0x8000u, images[ROM_COCO_EXTBAS11], romset_images[ROM_COCO_EXTBAS11].size);
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        if (kinds[s] == CART_RSDOS)
            coco_load_slot_rom(&g->m, s, images[ROM_COCO_DISK11], romset_images[ROM_COCO_DISK11].size);
        else if (imgs && imgs[s])
            coco_load_slot_rom(&g->m, s, imgs[s], lens[s]);
    }
    coco_power_on(&g->m);
    keymatrix_init(&g->k);
    g->ok_ticks = 0;
    g->after_field = NULL;
}

void guest_build(guest_t *g, unsigned ram_k, bool extended) {
    guest_build_as(g, ram_k, ROM_COCO_BAS13, extended);
}

void guest_fields(guest_t *g, int n) {
    for (int i = 0; i < n; i++) {
        keymatrix_field(&g->k, &g->m);
        coco_run_field(&g->m);
        if (g->after_field) g->after_field(g);
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
        if (g->after_field) g->after_field(g);
        if (guest_find_row(&g->m, "OK", 0) >= 0) {
            g->ok_ticks = coco_ticks(&g->m) - t0;
            guest_fields(g, 5);
            return true;
        }
    }
    return false;
}

/* ---- the keyboard ---------------------------------------------------- */

void guest_type(guest_t *g, const char *s) {
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

/* The core's reading of the screen (textpage.h), which the firmware uses
 * for the boot's "OK" and the UART's screen dump, so the two agree. */
const char *guest_row(const coco_t *m, int row) {
    static char buf[TEXT_COLS + 1];
    textpage_screen_row(m, row, buf);
    return buf;
}

int guest_find_row(const coco_t *m, const char *s, int from) {
    return textpage_screen_find(m, s, from);
}

void guest_dump(const coco_t *m, FILE *f) {
    for (int r = 0; r < TEXT_ROWS; r++) fprintf(f, "  |%-32s|\n", guest_row(m, r));
}
