/* test_soak.c — the soak program, tools/soak.bas, run on a 64 KB CoCo with
 * Extended Color BASIC before the board runs it for 30 minutes
 * (design.md §15.5, §17 M11), from pico-oric's.
 *
 * Typed as tools/soak.sh types it, a line at a time and RUN; then, as the
 * soak does, H and later J typed while it runs. What soak-check.py reads
 * off the board must be here first: the line "C <count> K <key>" with the
 * count rising and both keys read (72, 74), PMODE 4 shown, and the sound
 * bus moving every pass. A control types a program with a mistake in it,
 * which stops at an error: the count must not rise, and the bus must not
 * move as the program's does.
 *
 *   test_soak --write DIR    also writes the program as DIR/SOAK.CAS,
 *                            saved by CSAVE"SOAK" through the tape trap,
 *                            for the release build's soak, which has no
 *                            UART to type it (§15.5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"
#include "mc6847.h"
#include "test_util.h"

static guest_t g;
static const char *s_write_dir;
/* Fields watched, and those showing the text page: soak.sh's screen dumps
 * land at random, and soak-check.py needs half of them to show the count. */
static long s_fields, s_text_fields;
/* soak-check.py needs the bus to move in every heartbeat's five seconds:
 * the 300-field windows in which it did not, after the first pass. */
static long s_windows, s_quiet_windows;
static uint32_t s_window_edges;

#define TAPE_CAP 0x4000u
static uint8_t s_tape[TAPE_CAP];

/* The count and key on "C n K k", on any row; false if none shows. */
static bool status(long *n, long *k) {
    for (int r = 0; r < 16; r++) {
        const char *s = guest_row(&g.m, r);
        if (s[0] == 'C' && sscanf(s, "C %ld K %ld", n, k) == 2) return true;
    }
    return false;
}

/* Run up to `fields`, watching the status line; the last count and key
 * seen, and whether PMODE 4 showed. With `key`, it is typed every five
 * seconds, as soak.sh types: INKEY$ sees a key only while it is held,
 * so one typed while a pass draws is not read. */
static void watch(int fields, const char *key, long *n, long *k, bool *pmode4) {
    for (int f = 0; f < fields; f++) {
        if (key && f % 300 == 0) guest_type(&g, key);
        guest_fields(&g, 1);
        long a, b;
        if (status(&a, &b)) {
            *n = a;
            *k = b;
        }
        uint8_t mode = coco_vdg_mode(&g.m);
        if ((mode & VDG_AG) && (mode & VDG_GM_MASK) == VDG_GM_MASK) *pmode4 = true;
        s_fields++;
        if (!(mode & VDG_AG) && status(&a, &b)) s_text_fields++;
        if (s_fields % 300 == 0) {
            if (*n > 0) {
                s_windows++;
                if (g.m.sound_edges == s_window_edges) s_quiet_windows++;
            }
            s_window_edges = g.m.sound_edges;
        }
    }
}

static void type_program(const char *path, const char *mistake) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: cannot open\n", path);
        exit(1);
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (mistake && !strncmp(line, "210 ", 4)) snprintf(line, sizeof line, "%s", mistake);
        guest_type(&g, line);
        guest_type(&g, "\n");
        /* soak.sh waits a second after each line. */
        guest_fields(&g, 60);
    }
    fclose(f);
}

/* CSAVE"SOAK" through the trap into a tape of its own, written out as
 * DIR/SOAK.CAS. */
static bool save_cas(const char *dir) {
    coco_tape_attach(&g.m, s_tape, TAPE_CAP);
    coco_tape_insert(&g.m, 0);
    guest_type(&g, "CSAVE\"SOAK\"\n");
    guest_fields(&g, 120);
    uint32_t len = g.m.tape.len;
    if (!len || g.m.tape.overflow) return false;
    char path[512];
    snprintf(path, sizeof path, "%s/SOAK.CAS", dir);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(s_tape, 1, len, f) == len;
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) printf("  %s: %u bytes\n", path, (unsigned)len);
    coco_tape_eject(&g.m);
    return ok;
}

/* One run: the count after 600 fields, and after 1,800 more, H typed in
 * the first 900 and J in the second; the sound bus's changes over the run. */
static bool run(const char *mistake, long *n0, long *n1, long *kh, long *kj, bool *pmode4,
                uint32_t *edges) {
    guest_build(&g, 64, true);
    if (!guest_boot(&g)) return false;
    type_program(PICO_COCO_SOURCE_DIR "/tools/soak.bas", mistake);
    if (s_write_dir && !mistake) CHECK(save_cas(s_write_dir), "SOAK.CAS not written");
    uint32_t e0 = g.m.sound_edges;
    guest_type(&g, "RUN\n");
    long k = -1;
    *n0 = *n1 = -1;
    *kh = *kj = -1;
    *pmode4 = false;
    watch(600, NULL, n0, &k, pmode4);
    watch(900, "H", n1, kh, pmode4);
    watch(900, "J", n1, kj, pmode4);
    *edges = g.m.sound_edges - e0;
    return true;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--write") == 0) s_write_dir = argv[2];
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: Color BASIC 1.3 and Extended 1.1 not in %s\n", dir);
        return TEST_SKIP_CODE;
    }

    long n0, n1, kh, kj;
    bool pmode4;
    uint32_t edges;
    CHECK(run(NULL, &n0, &n1, &kh, &kj, &pmode4, &edges), "the CoCo did not boot");
    printf("  soak.bas: count %ld then %ld, keys %ld %ld, PMODE 4 %s, %u bus changes, "
           "the count shown in %ld of %ld fields\n", n0, n1, kh, kj, pmode4 ? "yes" : "no",
           (unsigned)edges, s_text_fields, s_fields);
    if (n1 <= n0 || kh != 'H' || kj != 'J') guest_dump(&g.m, stderr);
    CHECK(n0 > 0, "no status line after 600 fields");
    CHECK(n1 > n0 + 2, "the count did not rise (%ld to %ld)", n0, n1);
    CHECK(kh == 'H', "H typed, the program read %ld", kh);
    CHECK(kj == 'J', "J typed, the program read %ld", kj);
    CHECK(pmode4, "PMODE 4 never shown");
    CHECK(s_windows >= 4 && s_quiet_windows == 0, "the bus was still in %ld of %ld five-second "
          "windows", s_quiet_windows, s_windows);
    CHECK(s_text_fields * 10 > s_fields * 6, "the count shown in %ld of %ld fields: soak.sh's "
          "dumps would miss it", s_text_fields, s_fields);
    uint32_t run_edges = edges;
    long passes = n1 - n0;

    /* The control: line 210 misspelt, so the program stops at its first
     * GOSUB and the count never shows. */
    CHECK(run("210 PRONT@0,\"C\";N;\"K\";K;", &n0, &n1, &kh, &kj, &pmode4, &edges),
          "the CoCo did not boot");
    printf("  control: stopped at an error, count %ld, %u bus changes\n", n1, (unsigned)edges);
    CHECK(n1 <= 0, "a program stopped at an error showed a count (%ld)", n1);
    CHECK(run_edges > edges + 100u * (uint32_t)passes,
          "the program's %u bus changes are not the control's %u and a sound a pass for %ld "
          "passes", (unsigned)run_edges, (unsigned)edges, passes);

    TEST_DONE();
}
