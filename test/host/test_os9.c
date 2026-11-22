/* test_os9.c — OS-9 on the CoCo's disks (design.md §12.4): Tandy's OS-9
 * Level One and NitrOS-9's Level 1, booted from Disk BASIC's DOS, a disk
 * formatted in /D1, files copied and listed.
 *
 * Needs disks the tree does not ship (D10): they are found by SHA-1 in
 * PICO_COCO_DISKS if that is set, else in roms/, as the ROMs are, and
 * tools/fetch-os9.sh fetches them. Each part skips without its disk; the
 * test skips if none is there. A skip is not a pass.
 *
 * NitrOS-9's disks are headerless DSKs of two sides, which only OS-9's
 * LSN0 says (jvc.h), and its driver chooses the side after it has given
 * the command (wd1793.c): its boot holds both. The control is the same
 * 360K disk read as one side of 80 tracks, which must not boot.
 *
 * Lower case on the MC6847T1 (mc6847.h): both systems' console drivers
 * set GM0 in the alphanumeric mode when bit 0 of the TERM descriptor's
 * PAR is set as they start, and never otherwise (their TMODE does not
 * reach it). So each boots with that bit set in its descriptor, as
 * tools/os9-lowercase.py sets it, against the disk as shipped.
 *
 * The SmartWatch (smartwatch.h): 2.01's system disk with clock_tc3 in
 * its OS9Boot, as tools/os9-clock.py makes it, boots with the host's
 * date and time, and SETIME sets the chip; the control is the same disk
 * with no SmartWatch fitted.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk_util.h"
#include "mc6847.h"
#include "mc6847_font.h"
#include "guest.h"
#include "sha1.h"
#include "test_util.h"

typedef enum { TANDY_V200, NITROS9_40D, NITROS9_80D, TANDY_V201_RTC, DISK_COUNT } disk_id_t;

static const struct {
    const char *name, *sha1;
} s_known[DISK_COUNT] = {
    { "OS-9 Level One 02.00.00, the system disk", "521740e7a00eefb4b92a6c803391df31c004fdf9" },
    { "NitrOS-9 3.3.0, Level 1, CoCo 1, 40 tracks double-sided, disk 1",
      "c417fad58ea8eca331e173922748e9903bc20fcd" },
    { "NitrOS-9 3.3.0, Level 1, CoCo 1, 80 tracks double-sided",
      "8695113e8a9e7d616e7db055af2b1f4a00989808" },
    { "OS-9 Level One 02.01.00 with clock_tc3, by tools/os9-clock.py",
      "76fd68df3fc0527d1ceb49bc6772a7a7311d7636" },
};

static char s_path[DISK_COUNT][1024];

static void find_disks(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static uint8_t buf[1u << 20];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        uint8_t dg[SHA1_DIGEST_LEN];
        sha1(buf, n, dg);
        char hex[2 * SHA1_DIGEST_LEN + 1];
        for (unsigned i = 0; i < SHA1_DIGEST_LEN; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);
        for (unsigned k = 0; k < DISK_COUNT; k++)
            if (!strcmp(hex, s_known[k].sha1)) snprintf(s_path[k], sizeof s_path[k], "%s", path);
    }
    closedir(d);
}

static guest_t g;
static host_disk_t s_d0, s_d1;
static host_disk_t *s_drives[COCO_DISK_DRIVES];

/* The SmartWatch, fitted or not, and the host's time it is given. */
static bool     s_clock_fit;
static uint32_t s_clock_host;

static void serve_fields(guest_t *gg) {
    host_disk_serve(&gg->m, s_drives);
    gg->m.clock.fitted = s_clock_fit;
    smartwatch_set_host(&gg->m.clock, s_clock_host);
}

static bool screen_has(const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(&g.m, r), s)) return true;
    return false;
}

/* The bottom-most text on the screen. */
static const char *last_line(void) {
    for (int r = 15; r >= 0; r--) {
        const char *s = guest_row(&g.m, r);
        if (s[0]) return s;
    }
    return "";
}

static bool ends_with(const char *s, const char *tail) {
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && !strcmp(s + n - k, tail);
}

/* Fields until the shell's prompt ends the screen with the controller
 * idle, up to max seconds; at a "TIME ?" on the way, ENTER. */
static bool prompt(int max) {
    for (int f = 0; f < max * 60; f++) {
        const char *s = last_line();
        if (ends_with(s, "OS9:") && !coco_disk_busy(&g.m)) {
            guest_fields(&g, 30);   /* the shell reads the keyboard by now */
            return true;
        }
        if (ends_with(s, "TIME ?")) guest_type(&g, "\r");
        guest_fields(&g, 1);
    }
    printf("prompt: gave up after %d s, at %.1f s of guest time, pc %04X, disk busy %d\n", max,
           (double)coco_ticks(&g.m) / 14318180.0, g.m.cpu.pc, coco_disk_busy(&g.m));
    return false;
}

static void dump(void) {
    for (int r = 0; r < 16; r++) printf("  |%s\n", guest_row(&g.m, r));
}

/* A command at the shell, and its prompt again. */
static bool shell(const char *line, int max) {
    guest_type(&g, line);
    bool ok = prompt(max);
    if (!ok) dump();
    return ok;
}

/* DOS from Disk BASIC with d0 in drive 0 and an empty disk in drive 1,
 * as the Disks page makes one. True once the shell's prompt shows. */
static bool boot(int max) {
    guest_build_disk(&g, 64);
    g.after_field = serve_fields;
    host_disk_free(&s_d1);
    host_disk_new(&s_d1);
    s_drives[0] = &s_d0;
    s_drives[1] = &s_d1;
    host_disk_insert(&g.m, 0, &s_d0);
    host_disk_insert(&g.m, 1, &s_d1);
    if (!guest_boot(&g)) return false;
    guest_type(&g, "DOS\r");
    return prompt(max);
}

static bool load(disk_id_t id) {
    host_disk_free(&s_d0);
    const char *why = host_disk_load(&s_d0, s_path[id]);
    if (why) printf("%s: %s\n", s_path[id], why);
    return why == NULL;
}

/* OS-9's module CRC (F$CRC): 24 bits, $800063, from $FFFFFF; a whole
 * module, its stored CRC included, comes to $800FE3. */
static uint32_t os9_crc(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= (uint32_t)p[i] << 16;
        for (unsigned b = 0; b < 8u; b++) {
            c <<= 1;
            if (c & 0x1000000u) c ^= 0x800063u;
        }
        c &= 0xFFFFFFu;
    }
    return c;
}

/* Bit 0 of PAR set in the image's TERM descriptor (offset $26), its CRC
 * made good: the number of descriptors changed, which should be one. */
static unsigned term_lowercase(uint8_t *img, uint32_t len) {
    unsigned n = 0;
    for (uint32_t i = 0; i + 0x30u < len; i++) {
        uint8_t *m = img + i;
        if (m[0] != 0x87u || m[1] != 0xCDu) continue;
        uint8_t par = 0;
        for (unsigned k = 0; k < 9u; k++) par ^= m[k];
        uint32_t size = (uint32_t)m[2] << 8 | m[3], name = (uint32_t)m[4] << 8 | m[5];
        if (par != 0xFFu || (m[6] & 0xF0u) != 0xF0u || size > len - i || name + 4u > size)
            continue;
        if ((m[name] & 0x5Fu) != 'T' || (m[name + 1] & 0x5Fu) != 'E' ||
            (m[name + 2] & 0x5Fu) != 'R' || (m[name + 3] & 0xDFu) != ('M' | 0x80u))
            continue;
        if (os9_crc(m, size) != 0x800FE3u) continue;
        m[0x26] |= 1u;
        uint32_t c = os9_crc(m, size - 3u) ^ 0xFFFFFFu;
        m[size - 3u] = (uint8_t)(c >> 16);
        m[size - 2u] = (uint8_t)(c >> 8);
        m[size - 1u] = (uint8_t)c;
        n++;
    }
    return n;
}

static bool gm0(void) {
    return (g.m.pia1.b.out & g.m.pia1.b.ddr & VDG_GM0) != 0;
}

/* The text screen's first cell holding `want`, in the VDG's codes. */
static int find_cells(const uint8_t *want, unsigned n) {
    uint16_t base = (uint16_t)((g.m.sam.reg >> 3 & 0x7Fu) << 9);
    for (unsigned i = 0; i + n <= 512u; i++)
        if (!memcmp(&g.m.ram[(uint16_t)(base + i)], want, n)) return (int)i;
    return -1;
}

/* Cell `at` of the text screen drawn through a T1, every one of its 12
 * lines: is it that glyph, dark on green? */
static bool t1_cell_is(int at, unsigned glyph) {
    static coco_frame_t f;
    static mc6847_t v;
    static uint16_t px[COCO_PIC_W];
    coco_video_snapshot(&g.m, &f);
    mc6847_init(&v);
    mc6847_set_font(&v, font_6847);
    mc6847_set_t1(&v, font_6847t1);
    mc6847_set_mode(&v, f.mode);
    unsigned row = (unsigned)at / 32u, col = (unsigned)at % 32u;
    for (unsigned y = 0; y < 12u; y++) {
        mc6847_render_line(&v, &f.lines[(row * 12u + y) * COCO_VDG_LINE_BYTES], y, px);
        uint8_t bits = (uint8_t)~font_6847t1[glyph * 12u + y];
        for (unsigned x = 0; x < 8u; x++) {
            vdg_colour_t c = ((bits >> (7u - x)) & 1u) ? VDG_GREEN : VDG_BLACK;
            if (px[col * 8u + x] != mc6847_palette[c]) return false;
        }
    }
    return true;
}

/* Tandy's: one side of 35 tracks, as its CCDisk drives one (it has no
 * side select). DIR, then FORMAT /D1 onto the empty disk, then a file
 * copied there and listed. */
static int test_tandy(void) {
    CHECK(load(TANDY_V200) && s_d0.g.os9 && s_d0.g.sides == 1 && s_d0.g.tracks == 35,
          "Tandy: the system disk, one side of 35 by LSN0");
    CHECK(boot(60), "Tandy: DOS boots OS-9 to its shell");
    CHECK(screen_has("OS-9 LEVEL ONE VR. 02.00.00"), "Tandy: its banner");
    CHECK(shell("DIR\r", 30) && screen_has("OS9BOOT") && screen_has("CMDS"), "Tandy: DIR");
    CHECK(shell("FORMAT /D1 R \"TANDY\"\r", 120) && screen_has("NUMBER OF GOOD SECTORS: $000276"),
          "Tandy: FORMAT /D1, 630 sectors");
    CHECK(s_d1.len == 161280u && s_d1.g.sides == 1 && s_d1.g.tracks == 35,
          "Tandy: the new disk, %u bytes, %u sides", s_d1.len, s_d1.g.sides);
    CHECK(shell("COPY /D0/STARTUP /D1/STARTUP\r", 60) && shell("LIST /D1/STARTUP\r", 30) &&
          screen_has("SETIME"), "Tandy: a file copied and listed");
    return 0;
}

/* NitrOS-9's 360K disk, two sides by LSN0 alone: it boots, FREE counts
 * both sides, and FORMAT /D1 with two sides of 40 grows the empty disk
 * into a two-sided image whose header says so. */
static int test_nitros9_40(void) {
    CHECK(load(NITROS9_40D) && s_d0.g.os9 && s_d0.g.header == 0 && s_d0.g.sides == 2 &&
          s_d0.g.tracks == 40, "NitrOS-9 40: two sides of 40, by LSN0");
    CHECK(boot(60), "NitrOS-9 40: DOS boots it to its shell");
    /* Its console waits for a key when the screen fills: not here. */
    CHECK(shell("TMODE PAU=0\r", 30) && !screen_has("ERROR"), "NitrOS-9 40: TMODE PAU=0");
    CHECK(shell("FREE\r", 30) && screen_has("1,440 SECTORS"), "NitrOS-9 40: FREE, 1,440 sectors");
    /* /D1 is a 40-track double-sided drive in NitrOS-9's descriptor. */
    CHECK(shell("FORMAT /D1 R \"DS40\"\r", 180) && screen_has("NUMBER OF SURFACES: 2") &&
          screen_has("NUMBER OF GOOD SECTORS: $0005A0"), "NitrOS-9 40: FORMAT /D1, two sides");
    CHECK(s_d1.len == 2u + 2u * 40u * 4608u && s_d1.g.header == 2 && s_d1.g.sides == 2 &&
          s_d1.g.tracks == 40 && s_d1.img[0] == 18 && s_d1.img[1] == 2,
          "NitrOS-9 40: the new disk two-sided, %u bytes", s_d1.len);
    CHECK(shell("COPY /D0/STARTUP /D1/STARTUP\r", 60) && shell("LIST /D1/STARTUP\r", 30) &&
          screen_has("WELCOME TO NITROS-9"), "NitrOS-9 40: a file copied and listed");
    CHECK(shell("FREE /D1\r", 30) && screen_has("\"DS40\"") && screen_has("1,440 SECTORS"),
          "NitrOS-9 40: FREE /D1");
    jvc_geom_t again;
    CHECK(jvc_parse(s_d1.img, s_d1.len, s_d1.len, &again) == NULL && again.sides == 2 &&
          again.tracks == 40, "NitrOS-9 40: the new disk reads back two-sided");

    /* The control: the same disk read as one side of 80 tracks, which
     * the size alone says. It never reaches the shell. */
    CHECK(load(NITROS9_40D), "control: the disk again");
    s_d0.g.os9 = false;
    s_d0.g.sides = 1;
    CHECK(jvc_resize(&s_d0.g, s_d0.len) && s_d0.g.tracks == 80, "control: one side of 80");
    CHECK(!boot(40), "control: one side of 80 does not boot");
    return 0;
}

/* Lower case: NitrOS-9 as shipped leaves GM0 low, the control; with
 * PAR's bit 0 it boots with GM0 high, and the T1 shows the "irectory"
 * of DIR's heading, which NitrOS-9 writes as $01-$1A, in lower case. */
static int test_nitros9_t1(void) {
    CHECK(load(NITROS9_40D) && boot(60), "NitrOS-9 T1: as shipped, it boots");
    CHECK(!gm0(), "NitrOS-9 T1: as shipped, GM0 low: the control");
    CHECK(load(NITROS9_40D), "NitrOS-9 T1: the disk again");
    CHECK(term_lowercase(s_d0.img, s_d0.len) == 1, "NitrOS-9 T1: one TERM descriptor");
    CHECK(boot(60) && gm0(), "NitrOS-9 T1: PAR bit 0 set, it boots with GM0 high");
    CHECK(shell("TMODE PAU=0\r", 30) && shell("DIR\r", 30), "NitrOS-9 T1: DIR");
    static const uint8_t irectory[] = { 0x09, 0x12, 0x05, 0x03, 0x14, 0x0F, 0x12, 0x19 };
    int at = find_cells(irectory, sizeof irectory);
    CHECK(at >= 0, "NitrOS-9 T1: \"irectory\" on the screen as $01-$1A");
    CHECK(t1_cell_is(at, 0x09u) && t1_cell_is(at + 1, 0x12u),
          "NitrOS-9 T1: 'i' and 'r' drawn from the T1's lower case");
    CHECK(!t1_cell_is(at, 0x49u), "NitrOS-9 T1: and not as 'I'");

    /* The keyboard's case: the PicoCalc's Ctrl is CLEAR, so Ctrl+0 is
     * OS-9's CLEAR+0, which turns its upper-case lock off (2026-11-17).
     * The control: ECHO before it, in capitals. */
    static const uint8_t caps[] = { 0x5A, 0x51, 0x4A }, small[] = { 0x1A, 0x11, 0x0A };
    CHECK(shell("ECHO zqj\r", 30) && find_cells(caps, 3) >= 0 && find_cells(small, 3) < 0,
          "NitrOS-9 T1: before Ctrl+0, ZQJ in capitals");
    keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_CTRL);
    keymatrix_event(&g.k, KEY_EV_PRESSED, '0');
    keymatrix_event(&g.k, KEY_EV_RELEASED, '0');
    keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_CTRL);
    CHECK(guest_settle(&g, 100), "NitrOS-9 T1: Ctrl+0 let go");
    CHECK(shell("ECHO zqj\r", 30) && find_cells(small, 3) >= 0,
          "NitrOS-9 T1: after Ctrl+0, zqj in lower case");
    return 0;
}

/* Tandy's driver tests PAR the other way round from NitrOS-9's for its
 * VDG screen (bit 1 clear, not set), but bit 0 is lower case in both. */
static int test_tandy_t1(void) {
    CHECK(load(TANDY_V200) && boot(60), "Tandy T1: as shipped, it boots");
    CHECK(!gm0(), "Tandy T1: as shipped, GM0 low: the control");
    CHECK(load(TANDY_V200), "Tandy T1: the disk again");
    CHECK(term_lowercase(s_d0.img, s_d0.len) == 1, "Tandy T1: one TERM descriptor");
    CHECK(boot(60) && gm0(), "Tandy T1: PAR bit 0 set, it boots with GM0 high");
    CHECK(shell("DIR\r", 30) && screen_has("OS9BOOT"), "Tandy T1: DIR");
    return 0;
}

/* The SmartWatch: the date OS-9 boots with is the host's, 2026 by the
 * year register's years since 1900 (smartwatch.h). 2.01's SETIME takes a
 * four-digit year and writes the chip, and DATE reads it back. The
 * control: the same disk with no SmartWatch reads nonsense. */
static int test_tandy_rtc(void) {
    s_clock_fit = false;
    s_clock_host = 848238330u;   /* 2026-11-17 13:45:30 */
    CHECK(load(TANDY_V201_RTC) && boot(60), "RTC: no SmartWatch, it boots");
    CHECK(shell("DATE T\r", 30) && !screen_has("NOVEMBER 17"), "RTC: no SmartWatch, no date");
    s_clock_fit = true;
    CHECK(load(TANDY_V201_RTC) && boot(60), "RTC: with the SmartWatch, it boots");
    CHECK(!screen_has("TIME ?"), "RTC: and asks for no time");
    CHECK(shell("DATE T\r", 30) && screen_has("NOVEMBER 17, 2026  13:4"),
          "RTC: DATE T, the host's date and time");
    CHECK(shell("SETIME 1999/12/31 23:59:00\r", 30) && g.m.clock.sets == 1u,
          "RTC: SETIME writes the SmartWatch: %u sets", g.m.clock.sets);
    CHECK(shell("DATE T\r", 30) && screen_has("DECEMBER 31, 1999  23:59:0"),
          "RTC: DATE T, the time set");
    s_clock_fit = false;
    return 0;
}

/* The 720K disk: two sides of 80. */
static int test_nitros9_80(void) {
    CHECK(load(NITROS9_80D) && s_d0.g.os9 && s_d0.g.sides == 2 && s_d0.g.tracks == 80,
          "NitrOS-9 80: two sides of 80");
    CHECK(boot(60), "NitrOS-9 80: DOS boots it to its shell");
    CHECK(shell("FREE\r", 30) && screen_has("2,880 SECTORS"), "NitrOS-9 80: FREE, 2,880 sectors");
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11) ||
        !guest_have_rom(ROM_COCO_DISK11)) {
        printf("SKIP: no Disk Extended Color BASIC in %s\n", dir);
        return 77;
    }
    const char *disks = getenv("PICO_COCO_DISKS");
    find_disks(disks ? disks : dir);
    unsigned have = 0;
    for (unsigned k = 0; k < DISK_COUNT; k++) {
        if (s_path[k][0]) have++;
        else printf("skip: no %s (sha1 %s)\n", s_known[k].name, s_known[k].sha1);
    }
    if (!have) {
        printf("SKIP: no OS-9 disk in %s: tools/fetch-os9.sh\n", disks ? disks : dir);
        return 77;
    }
    if (s_path[TANDY_V200][0] && test_tandy()) return 1;
    if (s_path[NITROS9_40D][0] && test_nitros9_40()) return 1;
    if (s_path[NITROS9_80D][0] && test_nitros9_80()) return 1;
    if (s_path[NITROS9_40D][0] && test_nitros9_t1()) return 1;
    if (s_path[TANDY_V200][0] && test_tandy_t1()) return 1;
    if (s_path[TANDY_V201_RTC][0] && test_tandy_rtc()) return 1;
    TEST_DONE();
}
