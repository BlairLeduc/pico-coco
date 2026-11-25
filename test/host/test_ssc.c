/* test_ssc.c — the Speech/Sound Cartridge in a CoCo (ssc.h, design.md
 * §12.9).
 *
 * With Color BASIC, Extended BASIC and the cartridge's two ROMs
 * (pic-7040-510.bin, sp0256-al2.bin) in the ROM directory; skipped
 * without them:
 *   - the manual's own program, typed into BASIC: the multiplexer to the
 *     cartridge, then "I CAN TALK " a byte at a time, each after BUSY
 *     reads high, and CR. The cartridge takes every byte, the SP0256
 *     speaks the firmware's allophones, the status shows it speaking
 *     and then not, and the speech is heard; the same program with the
 *     multiplexer left on the DAC (the control) speaks unheard;
 *   - $FF7D, 1 then 0, resets the cartridge mid-sentence: silence;
 *   - in a Multi-Pak beside Disk BASIC, the cartridge in slot 2 and the
 *     disk controller in slot 4 with the switch at it, as The Interbank
 *     Incident wants them: Disk BASIC starts, and the cartridge speaks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "guest.h"
#include "test_util.h"

static guest_t g;
static uint8_t pic[TMS7000_ROM_SIZE], al2[SP0256_ROM_SIZE];

static bool load(const char *dir, const char *name, uint8_t *dst, size_t len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(dst, 1, len, f);
    fclose(f);
    return got == len;
}

static void build_ssc(void) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = 64;
    cfg.cart = CART_SSC;
    coco_init(&g.m, &cfg);
    coco_load_rom(&g.m, 0xA000u, guest_rom_image(ROM_COCO_BAS13), romset_images[ROM_COCO_BAS13].size);
    coco_load_rom(&g.m, 0x8000u, guest_rom_image(ROM_COCO_EXTBAS11),
                  romset_images[ROM_COCO_EXTBAS11].size);
    coco_load_ssc_roms(&g.m, pic, al2);
    coco_power_on(&g.m);
    keymatrix_init(&g.k);
    g.ok_ticks = 0;
    g.after_field = NULL;
}

/* Fields drained, the samples' spread over them: what was heard. */
static int field_spread(void) {
    static int16_t buf[4096];
    int lo = 0, hi = 0;
    bool first = true;
    size_t n;
    while ((n = coco_audio_drain(&g.m, buf, 4096)) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (first || buf[i] < lo) lo = buf[i];
            if (first || buf[i] > hi) hi = buf[i];
            first = false;
        }
    }
    return hi - lo;
}

typedef struct {
    unsigned speaking_fields, heard_fields;
    uint32_t allophones;
} talk_t;

/* Type the manual's program, with the multiplexer's source `src`
 * (PIA0 CB2 written to $FF03: 63 sets the cartridge, 52 leaves the DAC),
 * run it, and watch `fields` fields. */
static talk_t talk(int src_ff03, int fields) {
    char line[80];
    guest_type(&g, "10 X=&HFF00:Y=&HFF7E\r");
    snprintf(line, sizeof line, "20 POKE X+1,52:POKE X+3,%d:POKE X+35,60\r", src_ff03);
    guest_type(&g, line);
    guest_type(&g, "30 A$=\"I CAN TALK \"\r");
    guest_type(&g, "40 FOR I=1 TO LEN(A$)\r");
    guest_type(&g, "50 IF (PEEK(Y) AND 128)=0 THEN 50\r");
    guest_type(&g, "60 POKE Y,ASC(MID$(A$,I,1)):NEXT I\r");
    guest_type(&g, "70 IF (PEEK(Y) AND 128)=0 THEN 70\r");
    guest_type(&g, "80 POKE Y,13\r");
    (void)field_spread();
    talk_t t = { 0, 0, 0 };
    uint32_t a0 = g.m.port.ssc.sp.allophones;
    guest_type(&g, "RUN\r");
    for (int f = 0; f < fields; f++) {
        guest_fields(&g, 1);
        if (!g.m.port.ssc.sp.sby) t.speaking_fields++;
        if (field_spread() > 1000) t.heard_fields++;
    }
    t.allophones = g.m.port.ssc.sp.allophones - a0;
    return t;
}

static int test_basic(void) {
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot with the cartridge in the port");
    CHECK(bus_read(&g.m, 0xFF7E) == 0xFF, "the idle status reads %02X, not $FF", bus_read(&g.m, 0xFF7E));
    talk_t t = talk(63, 240);
    CHECK(guest_find_row(&g.m, "OK", 6) >= 0, "the program did not end at OK");
    CHECK(g.m.port.ssc.busy == false, "BUSY still set after the program");
    CHECK(t.allophones >= 10u, "only %u allophones spoken for I CAN TALK", t.allophones);
    CHECK(t.speaking_fields > 30u && t.speaking_fields < 200u, "speaking for %u fields", t.speaking_fields);
    CHECK(g.m.port.ssc.sp.sby, "still speaking 240 fields on");
    CHECK(t.heard_fields > 30u, "heard in only %u fields", t.heard_fields);
    printf("basic: I CAN TALK, %u allophones, speaking %u fields, heard in %u\n", t.allophones,
           t.speaking_fields, t.heard_fields);

    /* The control: the multiplexer left on the DAC. */
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot");
    talk_t c = talk(52, 240);
    CHECK(c.allophones >= 10u, "the control spoke only %u allophones", c.allophones);
    CHECK(c.heard_fields == 0, "speech heard in %u fields with the multiplexer on the DAC", c.heard_fields);

    /* RESET through $FF7D mid-sentence: the speech stops. */
    build_ssc();
    CHECK(guest_boot(&g), "BASIC did not boot");
    guest_type(&g, "POKE &HFF01,52:POKE &HFF03,63:POKE &HFF23,60\r");
    const char *text = "I CAN TALK ";
    for (const char *p = text; *p; p++) {
        while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
        bus_write(&g.m, 0xFF7E, (uint8_t)*p);
        guest_fields(&g, 1);
    }
    while (!(bus_read(&g.m, 0xFF7E) & 0x80)) guest_fields(&g, 1);
    bus_write(&g.m, 0xFF7E, 13);
    unsigned wait = 0;
    while (g.m.port.ssc.sp.sby && wait < 120) { guest_fields(&g, 1); wait++; }
    CHECK(!g.m.port.ssc.sp.sby, "never started speaking");
    guest_fields(&g, 10);
    uint32_t spoken = g.m.port.ssc.allophones;
    bus_write(&g.m, 0xFF7D, 1);
    bus_write(&g.m, 0xFF7D, 0);
    /* The firmware starts with one short pause (allophone 0), then
     * stands by. */
    wait = 0;
    while (!g.m.port.ssc.sp.sby && wait < 30) { guest_fields(&g, 1); wait++; }
    CHECK(g.m.port.ssc.sp.sby, "still speaking 30 fields after $FF7D's reset");
    CHECK(g.m.port.ssc.allophones - spoken <= 2u, "%u allophones loaded after the reset",
          g.m.port.ssc.allophones - spoken);
    (void)field_spread();
    guest_fields(&g, 30);
    CHECK(field_spread() < 300, "sound after $FF7D's reset");
    CHECK(bus_read(&g.m, 0xFF7E) == 0xFF, "the status after reset reads %02X", bus_read(&g.m, 0xFF7E));
    return 0;
}

static int test_mpi(void) {
    cart_kind_t kinds[COCO_MPI_SLOTS] = { CART_NONE, CART_SSC, CART_NONE, CART_RSDOS };
    guest_build_mpi(&g, 64, kinds, 3, NULL, NULL);
    CHECK(coco_load_ssc_roms(&g.m, pic, al2), "the cartridge's ROMs did not fit beside Disk BASIC");
    coco_power_on(&g.m);
    CHECK(guest_boot(&g), "Disk BASIC did not boot beside the cartridge");
    CHECK(guest_find_row(&g.m, "DISK EXTENDED COLOR BASIC", 0) >= 0, "not Disk BASIC's banner");
    talk_t t = talk(63, 240);
    CHECK(t.allophones >= 10u && t.heard_fields > 30u,
          "in slot 2 beside Disk BASIC: %u allophones, heard in %u fields", t.allophones, t.heard_fields);
    printf("multi-pak: Disk BASIC in slot 4, the cartridge in slot 2: %u allophones, heard in %u fields\n",
           t.allophones, t.heard_fields);
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: Color BASIC 1.3 and Extended 1.1 not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (!load(dir, "pic-7040-510.bin", pic, sizeof pic) || !load(dir, "sp0256-al2.bin", al2, sizeof al2)) {
        printf("skip: pic-7040-510.bin and sp0256-al2.bin not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (test_basic()) return 1;
    if (guest_have_rom(ROM_COCO_DISK11) && test_mpi()) return 1;
    TEST_DONE();
}
