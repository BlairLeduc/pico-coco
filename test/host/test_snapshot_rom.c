/* test_snapshot_rom.c — our own save states over the real ROMs
 * (design.md §12.5, §17 M10; EL §8.5).
 *
 * M10's criterion, executed: a state saved mid-program and restored
 * into a machine doing something else is identical to the original
 * straight after the load and 150 fields on (snap_util.h's comparison),
 * sample for sample, and shows the same screen.
 *
 *   - Extended Color BASIC in 64 KB, running a loop that prints and
 *     plays SOUND, restored into a machine listing another program.
 *   - Color BASIC 1.1 in 4 KB, refused by a 16 KB Color BASIC 1.3 as
 *     another ROM, and loaded once that machine is powered on as the
 *     state's info says, as the port does (snapio.h).
 *   - A CLOAD in flight: saved at an instruction with the trap's page
 *     hot (tape.h), and again between two blocks, each restored into a
 *     machine whose deck the test puts the same tape in at the same
 *     place, as the port does from the media; the program it loads
 *     runs the same.
 *
 * Skips without Color BASIC 1.3 and Extended Color BASIC 1.1; the 4 KB
 * case needs Color BASIC 1.1, and says so if it is missing.
 */

#include <stdio.h>
#include <string.h>

#include "guest.h"
#include "snap_util.h"
#include "test_util.h"

static guest_t g, h;
static coco_t saved, ahead;
static mem_t snap;
static int16_t sound_a[SNAP_SOUND_MAX], sound_b[SNAP_SOUND_MAX];

#define TAPE_CAP 0x10000u
static uint8_t tape_a[TAPE_CAP], tape_b[TAPE_CAP];

static void fields_sound(coco_t *m, int n, int16_t *out, size_t *got) {
    for (int i = 0; i < n; i++) {
        coco_run_field(m);
        snap_sound_take(m, out, got);
    }
}

static bool same_screen(const coco_t *a, const coco_t *b, const char *what) {
    for (int r = 0; r < 16; r++) {
        char ra[40];
        snprintf(ra, sizeof ra, "%s", guest_row(a, r));
        if (strcmp(ra, guest_row(b, r)) != 0) {
            fprintf(stderr, "    %s: row %d \"%s\" vs \"%s\"\n", what, r, ra, guest_row(b, r));
            return false;
        }
    }
    return true;
}

/* `a` saved where it stands and run on 150 fields; the state loaded into
 * `b`, which is somewhere else, and run on as far; the two compared
 * straight after the load and at the end, and their sound. With
 * `deck`, b's deck is given a's tape at a's place before the load, the
 * port's part (snapio.c). */
static int meet(coco_t *a, coco_t *b, bool deck, const char *what) {
    snap_media_t md;
    memset(&md, 0, sizeof md);
    CHECK(mem_save(&snap, a, &md) == SNAP_OK, "%s: save", what);
    coco_copy(&saved, a);
    a->pcm.dc_block = false;
    snap_sound_start(a);
    size_t na = 0, nb = 0;
    fields_sound(a, 150, sound_a, &na);
    coco_copy(&ahead, a);

    CHECK(mem_check(&snap, b, NULL, NULL) == SNAP_OK, "%s: check", what);
    /* What b made before the load is b's own, as the port drains it each
     * field. */
    snap_sound_take(b, NULL, &nb);
    nb = 0;
    if (deck) {
        memcpy(tape_b, tape_a, TAPE_CAP);
        coco_tape_insert(b, saved.tape.len);
        b->tape.pos = saved.tape.pos;
    }
    CHECK(mem_load(&snap, b) == SNAP_OK, "%s: load", what);
    uint32_t b_blocks = b->tape.blocks_in;   /* a count, not state */
    {
        char w[80];
        snprintf(w, sizeof w, "%s, straight after", what);
        CHECK(snap_same(&saved, b, w), "%s: not the saved machine straight after the load", what);
    }
    b->pcm.dc_block = false;
    fields_sound(b, 150, sound_b, &nb);
    CHECK(snap_same(&ahead, b, what), "%s: apart after 150 fields", what);
    CHECK(snap_same_sound(sound_a, na, sound_b, nb, what), "%s: the sound differs", what);
    CHECK(same_screen(&ahead, b, what), "%s: the screens differ", what);
    if (deck) CHECK(ahead.tape.blocks_in > saved.tape.blocks_in, "%s: the CLOAD was over at the save", what);
    if (deck) CHECK(b->tape.pos == ahead.tape.pos && b->tape.blocks_in - b_blocks ==
                    ahead.tape.blocks_in - saved.tape.blocks_in,
                    "%s: the deck went on differently: at %u vs %u", what, b->tape.pos, ahead.tape.pos);
    return 0;
}

/* ---- a program running, with sound ----------------------------------------- */

static int running(void) {
    guest_build(&g, 64, true);
    CHECK(guest_boot(&g), "boot");
    guest_type(&g, "10 FOR I=1 TO 5000\n");
    guest_type(&g, "20 PRINT I;\n");
    guest_type(&g, "30 SOUND (I AND 63)*3+40,1\n");
    guest_type(&g, "40 NEXT\n");
    guest_type(&g, "RUN\n");
    CHECK(guest_settle(&g, 50), "the keys did not settle");
    guest_fields(&g, 47);
    uint32_t edges = g.m.sound_edges;
    guest_fields(&g, 10);
    CHECK(g.m.sound_edges - edges > 100, "SOUND should be playing: %u edges", g.m.sound_edges - edges);

    guest_build(&h, 64, true);
    CHECK(guest_boot(&h), "boot the other");
    guest_type(&h, "10 REM SOMETHING ELSE\n");
    guest_type(&h, "LIST\n");
    guest_fields(&h, 13);
    return meet(&g.m, &h.m, false, "Extended, running");
}

/* ---- 4 KB Color BASIC 1.1, into the machine its info names --------------- */

static int four_k(void) {
    if (!guest_have_rom(ROM_COCO_BAS11)) {
        printf("  note: no Color BASIC 1.1 (bas11.rom): the 4 KB case is not run\n");
        return 0;
    }
    guest_build_as(&g, COCO_MACHINE_COCO, 4, ROM_COCO_BAS11, false);
    CHECK(guest_boot(&g), "boot 4K");
    guest_type(&g, "10 A=A+1:PRINT A;:GOTO 10\n");
    guest_type(&g, "RUN\n");
    CHECK(guest_settle(&g, 50), "the keys did not settle");
    guest_fields(&g, 31);

    guest_build_as(&h, COCO_MACHINE_COCO, 16, ROM_COCO_BAS13, false);
    CHECK(guest_boot(&h), "boot 16K");
    coco_copy(&saved, &h.m);
    CHECK(mem_save(&snap, &g.m, NULL) == SNAP_OK, "save 4K");
    snap_info_t info;
    CHECK(mem_check(&snap, &h.m, &info, NULL) == SNAP_OTHER_ROM, "a 4K 1.1 state into 16K 1.3");
    CHECK(mem_load(&snap, &h.m) == SNAP_OTHER_ROM, "loaded into 16K 1.3");
    CHECK(snap_same(&saved, &h.m, "refused"), "the refusing machine changed");
    CHECK(info.ram_k == 4u && info.rom_present[1] && info.rom[1] == ROM_COCO_BAS11 &&
          !info.rom_present[0] && !info.cart && info.machine == COCO_MACHINE_COCO,
          "the info: %u K, ROM %d", info.ram_k, (int)info.rom[1]);

    /* Powered on as the info says, as the port does. */
    guest_build_as(&h, info.machine, info.ram_k, info.rom[1], info.rom_present[0]);
    guest_fields(&h, 5);
    return meet(&g.m, &h.m, false, "4K Color BASIC 1.1");
}

/* ---- a CLOAD in flight ----------------------------------------------------- */

/* Step through fields, an instruction at a time up to each FS fall, until
 * pred holds at a boundary, with ENTER held for the first fields. */
static bool step_until(coco_t *m, bool (*pred)(const coco_t *), int max_fields, int enter_fields) {
    for (int f = 0; f < max_fields; f++) {
        coco_key_set(m, 6, 0, f < enter_fields);
        uint64_t fall = m->field_start + (uint64_t)m->fs_fall_line * m->line_ticks;
        while (coco_ticks(m) + 64u < fall) {
            coco_run(m, 1);
            if (pred(m)) {
                coco_key_set(m, 6, 0, false);
                return true;
            }
        }
        coco_run_field(m);
    }
    return false;
}

static bool trap_hot(const coco_t *m) { return m->tape.hot != 0 && m->tape.blocks_in >= 1; }
static bool between_blocks(const coco_t *m) { return m->tape.blocks_in == 3 && m->tape.hot == 0; }

/* A program long enough for several data blocks, saved to tape_a, NEW,
 * and CLOAD typed without its ENTER. */
static int cload_ready(guest_t *gg) {
    guest_build(gg, 64, true);
    coco_tape_attach(&gg->m, tape_a, TAPE_CAP);
    CHECK(guest_boot(gg), "boot for the tape");
    for (int line = 1; line <= 40; line++) {
        char text[64];
        snprintf(text, sizeof text, "%d PRINT \"LINE %d OF THE TAPE PROGRAM\";%d\n", line * 10, line, line * 3);
        guest_type(gg, text);
    }
    guest_type(gg, "CSAVE \"LONG\"\n");
    guest_fields(gg, 30);
    CHECK(gg->m.tape.len > 1200u && gg->m.tape.blocks_out >= 6, "CSAVE: %u bytes, %u blocks",
          gg->m.tape.len, gg->m.tape.blocks_out);
    coco_tape_rewind(&gg->m);
    gg->m.tape.saved = gg->m.tape.len;
    guest_type(gg, "NEW\n");
    guest_type(gg, "CLOAD \"LONG\"");
    CHECK(guest_settle(gg, 50), "the keys did not settle");
    return 0;
}

static int cload(bool (*pred)(const coco_t *), const char *what) {
    if (cload_ready(&g)) return 1;
    CHECK(step_until(&g.m, pred, 120, 4), "%s: no such boundary in the CLOAD", what);
    CHECK(coco_tape_pending(&g.m) == TAPE_NONE, "%s: a request waits", what);

    guest_build(&h, 64, true);
    coco_tape_attach(&h.m, tape_b, TAPE_CAP);
    CHECK(guest_boot(&h), "boot the other");
    guest_type(&h, "PRINT 6*7\n");
    if (meet(&g.m, &h.m, true, what)) return 1;

    /* The program it loaded, run in the restored machine. */
    keymatrix_init(&h.k);
    guest_type(&h, "RUN\n");
    guest_fields(&h, 60);
    CHECK(guest_find_row(&h.m, "LINE 40 OF THE TAPE PROGRAM", 0) >= 0, "%s: the program did not run", what);
    if (test_failures) guest_dump(&h.m, stderr);
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_BAS13) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skipped: Color BASIC 1.3 and Extended Color BASIC 1.1 not found in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (running()) return 1;
    if (four_k()) return 1;
    if (cload(trap_hot, "CLOAD, the trap's page hot")) return 1;
    if (cload(between_blocks, "CLOAD, between blocks")) return 1;
    TEST_DONE();
}
