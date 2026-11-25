/* test_cart.c — ROM cartridges with the real ROMs (design.md §12.2).
 *
 *   - A cartridge of the test's own, three instructions at $C000 that
 *     leave a mark in RAM, is started by BASIC through the CART line on
 *     PIA1 CB1; with the line not on the Q clock (the control) BASIC
 *     boots to OK instead and the mark is never left.
 *   - An 8 KB image repeats through the 16 KB window; taken out, the
 *     window reads as an empty slot.
 *   - Through a Multi-Pak: the CART line from the CTS slot alone, and
 *     Disk BASIC from slot 4 and from slot 3 (mpi_carts).
 *   - The Orchestra-90 CC (§12.10), its ROM orch90.rom beside the
 *     others (skipped without it): known by its SHA-1 and fitted with
 *     its DACs, it starts to its title, and S, ENTER, P, ENTER plays the
 *     William Tell Overture through them, heard beside the multiplexer;
 *     the ROM with one byte changed is a plain cartridge, whose port
 *     takes nothing at $FF7A. Planted, each failed (2026-11-24): the
 *     right DAC dropped from the mix, the bus not updated after a DAC's
 *     write (test_audio), and a slot of another kind taking the DACs'
 *     writes (here).
 *   - Every .ccc in the media directory (PICO_COCO_MEDIA, else media/;
 *     gitignored, none committed) starts: the PC is in the cartridge, or
 *     in RAM it has copied itself to, and BASIC's OK is not on the screen.
 */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bus.h"
#include "disk_util.h"
#include "guest.h"
#include "test_util.h"

static guest_t g;
static host_disk_t s_disk;
static host_disk_t *s_drives[COCO_DISK_DRIVES];

static void serve(guest_t *gg) {
    host_disk_serve(&gg->m, s_drives);
}

/* LDA #$5A; STA $0600; BRA * */
static const uint8_t mark_cart[] = { 0x86, 0x5A, 0xB7, 0x06, 0x00, 0x20, 0xFE };

static void build_with_cart(const uint8_t *img, size_t len, bool autostart) {
    guest_build(&g, 64, true);
    coco_load_rom(&g.m, 0xC000u, img, len);
    coco_cart_autostart(&g.m, autostart);
    coco_power_on(&g.m);   /* the ROMs loaded, then power on (§13.3) */
}

static bool on_screen(const char *s) {
    return guest_find_row(&g.m, s, 0) >= 0;
}

static int own_cart(void) {
    static uint8_t img[0x2000];
    memset(img, 0x12, sizeof img);
    memcpy(img, mark_cart, sizeof mark_cart);

    build_with_cart(img, sizeof img, true);
    int started = -1;
    for (int f = 0; f < 300 && started < 0; f++) {
        guest_fields(&g, 1);
        if (bus_read(&g.m, 0x0600) == 0x5A) started = f;
    }
    CHECK(started >= 0, "BASIC did not start the cartridge in 300 fields");
    CHECK(g.m.cpu.pc >= 0xC005u && g.m.cpu.pc <= 0xC006u, "PC %04X, not the cartridge's BRA *", g.m.cpu.pc);
    printf("own cartridge: started by field %d\n", started);

    /* The control: no Q clock on the line. */
    build_with_cart(img, sizeof img, false);
    CHECK(guest_boot(&g), "with no CART line BASIC did not boot");
    guest_fields(&g, 60);
    CHECK(bus_read(&g.m, 0x0600) != 0x5A, "the cartridge started with no CART line");

    /* The window: 8 KB repeated; then taken out. */
    CHECK(bus_read(&g.m, 0xE000) == 0x86 && bus_read(&g.m, 0xE002) == 0xB7, "8 KB not repeated at $E000");
    CHECK(bus_read(&g.m, 0xFE00) == 0x12, "8 KB not repeated to $FEFF");
    coco_cart_remove(&g.m);
    coco_power_on(&g.m);
    CHECK(!g.m.port.slot[0].rom && g.m.page[0xC0].read == NULL, "the slot still has a cartridge");
    CHECK(guest_boot(&g), "BASIC did not boot with the cartridge out");
    CHECK(bus_read(&g.m, 0x0600) != 0x5A, "the mark after the cartridge was taken out");
    return 0;
}

/* Through a Multi-Pak (§12.8): the mark cartridge in slot 1 is started
 * with the switch at slot 1, and not with it at slot 2, an empty slot,
 * where BASIC boots to OK: CART reaches FIRQ from the CTS slot alone.
 * Then Disk BASIC from slot 4 beside it, and from slot 3 with the
 * SmartWatch in slot 4, as clock_bb wants them (§12.7). */
static int mpi_carts(void) {
    if (!guest_have_rom(ROM_COCO_EXTBAS11) || !guest_have_rom(ROM_COCO_DISK11)) {
        printf("mpi: no Extended or Disk BASIC, skipped\n");
        return 0;
    }
    static uint8_t img[0x2000];
    memset(img, 0x12, sizeof img);
    memcpy(img, mark_cart, sizeof mark_cart);
    const uint8_t *imgs[4] = { img, NULL, NULL, NULL };
    const size_t lens[4] = { sizeof img, 0, 0, 0 };
    const cart_kind_t roms[4] = { CART_ROM, CART_NONE, CART_NONE, CART_NONE };

    guest_build_mpi(&g, 64, roms, 0, imgs, lens);
    int started = -1;
    for (int f = 0; f < 300 && started < 0; f++) {
        guest_fields(&g, 1);
        if (bus_read(&g.m, 0x0600) == 0x5A) started = f;
    }
    CHECK(started >= 0, "the Multi-Pak's slot 1 not started with the switch at 1");
    printf("mpi: slot 1's cartridge started by field %d\n", started);

    guest_build_mpi(&g, 64, roms, 1, imgs, lens);
    CHECK(guest_boot(&g), "BASIC did not boot with the switch at an empty slot");
    guest_fields(&g, 60);
    CHECK(bus_read(&g.m, 0x0600) != 0x5A, "slot 1 started with the switch at slot 2");

    const cart_kind_t disk4[4] = { CART_ROM, CART_NONE, CART_NONE, CART_RSDOS };
    guest_build_mpi(&g, 64, disk4, 3, imgs, lens);
    CHECK(guest_boot(&g) && on_screen("DISK EXTENDED COLOR BASIC"),
          "Disk BASIC from slot 4 did not boot");
    CHECK(bus_read(&g.m, 0x0600) != 0x5A, "slot 1 started with the switch at slot 4");

    const cart_kind_t disk3[4] = { CART_NONE, CART_NONE, CART_RSDOS, CART_SMARTWATCH };
    guest_build_mpi(&g, 64, disk3, 2, NULL, NULL);
    CHECK(guest_boot(&g) && on_screen("DISK EXTENDED COLOR BASIC"),
          "Disk BASIC from slot 3 did not boot");
    /* The controller through slot 3: a new disk formatted and asked
     * how much is free, 68 granules on a 35-track disk. */
    host_disk_new(&s_disk);
    s_drives[0] = &s_disk;
    g.after_field = serve;
    host_disk_insert(&g.m, 0, &s_disk);
    guest_type(&g, "DSKINI0\r");
    for (int f = 0; f < 60 * 60 && guest_find_row(&g.m, "OK", 6) < 0; f++) guest_fields(&g, 1);
    guest_type(&g, "PRINT FREE(0)\r");
    guest_fields(&g, 120);
    CHECK(guest_find_row(&g.m, " 68", 0) >= 0, "FREE(0) after DSKINI0 through slot 3 not 68");
    g.after_field = NULL;
    host_disk_free(&s_disk);
    return 0;
}

static uint8_t orch_img[COCO_CART_SIZE];
static size_t  orch_len;

static void build_orch(void) {
    guest_build(&g, 64, true);
    coco_load_rom(&g.m, 0xC000u, orch_img, orch_len);
    coco_power_on(&g.m);
}

/* Whether a field's samples, drained after it, ever vary. */
static bool field_heard(void) {
    static int16_t buf[4096];
    bool moved = false;
    size_t n;
    while ((n = coco_audio_drain(&g.m, buf, 4096)) > 0)
        for (size_t i = 1; i < n; i++) moved |= buf[i] != buf[0];
    return moved;
}

static int orch90(const char *dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/orch90.rom", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("orchestra-90: skip, no %s\n", path);
        return 0;
    }
    orch_len = fread(orch_img, 1, sizeof orch_img, f);
    fclose(f);

    build_orch();
    CHECK(g.m.port.slot[0].kind == CART_ORCH90, "%s is not known as the Orchestra-90", path);
    guest_fields(&g, 200);
    CHECK(on_screen("ORCHESTRA-90/CC (TM)"), "the Orchestra-90's title is not on the screen");
    guest_type(&g, " ");
    guest_settle(&g, 600);
    guest_fields(&g, 120);
    CHECK(on_screen("/ WILLIAM TELL OVERTURE"), "no William Tell Overture after a key");
    guest_type(&g, "S\r");
    guest_settle(&g, 600);
    guest_fields(&g, 300);
    guest_type(&g, "P\r");
    guest_settle(&g, 600);
    /* The player moves the DACs, and the multiplexer is not on the
     * cartridge: what is heard is beside it. */
    unsigned writes = 0, heard = 0;
    uint8_t last_l = g.m.port.slot[0].u.orch.left;
    (void)field_heard();
    for (int i = 0; i < 300; i++) {
        guest_fields(&g, 1);
        heard += field_heard();
        writes += g.m.port.slot[0].u.orch.left != last_l;
        last_l = g.m.port.slot[0].u.orch.left;
    }
    CHECK(writes > 50u, "the left DAC moved in only %u of 300 fields of playing", writes);
    CHECK(heard > 50u, "the Orchestra-90 played and was heard in only %u of 300 fields", heard);
    printf("orchestra-90: the overture moved the left DAC in %u of 300 fields, heard in %u\n",
           writes, heard);

    /* The control: one byte changed is a ROM, not an Orchestra-90, and
     * its port takes nothing at $FF7A. */
    orch_img[orch_len - 1] ^= 0x01u;
    build_orch();
    orch_img[orch_len - 1] ^= 0x01u;
    CHECK(g.m.port.slot[0].kind == CART_ROM, "a changed image was still known as the Orchestra-90");
    bus_write(&g.m, 0xFF7A, 0x80);
    CHECK(g.m.port.slot[0].u.orch.left == 0 && cart_beside(&g.m) == 0, "a ROM cartridge took a DAC's write");

    /* Nor does a SmartWatch beside an Orchestra-90 in a Multi-Pak. */
    cart_kind_t kinds[COCO_MPI_SLOTS] = { CART_NONE, CART_NONE, CART_NONE, CART_SMARTWATCH };
    guest_build_mpi(&g, 64, kinds, 0, NULL, NULL);
    coco_load_slot_rom(&g.m, 0, orch_img, orch_len);
    coco_power_on(&g.m);
    smartwatch_t before = g.m.port.slot[3].u.clock;
    bus_write(&g.m, 0xFF7A, 0x5A);
    bus_write(&g.m, 0xFF7B, 0xA5);
    CHECK(g.m.port.slot[0].kind == CART_ORCH90 && g.m.port.slot[0].u.orch.left == 0x5A &&
          g.m.port.slot[0].u.orch.right == 0xA5, "the Orchestra-90 in slot 1 missed its DACs' writes");
    CHECK(memcmp(&before, &g.m.port.slot[3].u.clock, sizeof before) == 0,
          "the SmartWatch in slot 4 took the Orchestra-90's writes");
    return 0;
}

static int media(void) {
    const char *env = getenv("PICO_COCO_MEDIA");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/carts", env ? env : PICO_COCO_SOURCE_DIR "/media");
    DIR *d = opendir(dir);
    if (!d) {
        printf("media: no %s, skipped\n", dir);
        return 0;
    }
    struct dirent *e;
    unsigned tried = 0;
    while ((e = readdir(d)) != NULL) {
        size_t nl = strlen(e->d_name);
        if (nl < 4 || (strcasecmp(e->d_name + nl - 4, ".ccc") != 0 && strcasecmp(e->d_name + nl - 4, ".rom") != 0))
            continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static uint8_t img[0x4001];
        size_t n = fread(img, 1, sizeof img, f);
        fclose(f);
        if (n == 0 || n > 0x4000) {
            printf("media: %s: %zu bytes, not a cartridge\n", e->d_name, n);
            continue;
        }
        build_with_cart(img, n, true);
        unsigned in_cart = 0, in_ram = 0, in_basic = 0;
        for (int f2 = 0; f2 < 300; f2++) {
            guest_fields(&g, 1);
            uint16_t pc = g.m.cpu.pc;
            if (pc >= 0xC000u && pc < 0xFF00u) in_cart++;
            else if (pc < 0x8000u) in_ram++;
            else in_basic++;
        }
        bool ok_shown = on_screen("OK");
        printf("media: %-50s %5zu bytes: fields ending in the cartridge %3u, RAM %3u, BASIC %3u%s\n",
               e->d_name, n, in_cart, in_ram, in_basic, ok_shown ? ", OK on screen" : "");
        CHECK(in_cart + in_ram > 0 && !ok_shown, "%s did not start", e->d_name);
        tried++;
    }
    closedir(d);
    printf("media: %u cartridges\n", tried);
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: Color BASIC 1.3 and Extended 1.1 not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (own_cart()) return 1;
    if (mpi_carts()) return 1;
    if (orch90(dir)) return 1;
    if (media()) return 1;
    TEST_DONE();
}
