/* test_cassette.c — tape phase 2, the signal, against the ROMs' own
 * routines (design.md §12.3, §17 M12; cassette.h).
 *
 * With fast loading off the ROM writes and reads the tape itself. For
 * each save, in Color BASIC 1.1, 1.3 and Extended Color BASIC:
 *
 *  1. the ROM's own save is run with every crossing of the DAC's middle
 *     and every move of the relay noted by its tick; the recorder's
 *     image of it is, byte for byte, the image the trap writes for the
 *     same save on another machine (tape.h);
 *  2. the player's walk of that image gives the ROM's crossings back,
 *     edge for edge, in every span of the relay, the silence before
 *     each leader among them;
 *  3. a machine with the image in its deck loads it off the signal with
 *     the ROM's own reader, and has the bytes the first machine saved.
 *
 * Then the port's cues with fast loading off: CLOAD with the deck empty
 * stalls for the tape by name, and a reader past the tape's end stalls
 * for a rewind; a save cut short by the reset button keeps whole blocks
 * only; and the player holds its place while the relay is open.
 */

#include <string.h>

#include "cas.h"
#include "guest.h"
#include "test_util.h"

#define TAPE_CAP 0x10000u
static uint8_t tape_a[TAPE_CAP], tape_b[TAPE_CAP], tape_c[TAPE_CAP];
static guest_t a, b;
static const char *label;

/* ---- the ROM's save, noted ------------------------------------------------ */

#define EDGE_MAX 200000u
#define SPAN_MAX 64u
static uint32_t edge[EDGE_MAX];      /* ticks from the event before */
static unsigned n_edge, span_at[SPAN_MAX + 1u], n_span;
static bool     in_span, note_level, note_motor;
static uint64_t note_last;

static bool motor_of(const coco_t *m) {
    return (m->pia1.a.cr & 0x38u) == 0x38u;
}

static void note(coco_t *m) {
    bool motor = motor_of(m), level = (mc6821_out(&m->pia1.a) & 0xFCu) >= 0x80u;
    uint64_t t = coco_ticks(m);
    if (motor != note_motor) {
        note_motor = motor;
        in_span = motor && n_span < SPAN_MAX;
        if (in_span) span_at[n_span++] = n_edge;
        note_last = t;
    }
    if (level != note_level) {
        note_level = level;
        if (in_span && n_edge < EDGE_MAX) edge[n_edge++] = (uint32_t)(t - note_last);
        note_last = t;
    }
}

static bool wait_ok(guest_t *gg, int max) {
    for (int f = 0; f < max; f++) {
        for (int r = 15; r >= 0; r--) {
            const char *s = guest_row(&gg->m, r);
            if (!s[0]) continue;
            if (strcmp(s, "OK") == 0 && coco_tape_pending(&gg->m) == TAPE_NONE) return true;
            break;
        }
        guest_fields(gg, 1);
    }
    return false;
}

static bool screen_has(const coco_t *m, const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(m, r), s)) return true;
    return false;
}

static void boot(guest_t *gg, rom_id_t basic, bool ext, uint8_t *tape, bool fast) {
    guest_build_as(gg, 64, basic, ext);
    coco_tape_attach(&gg->m, tape, TAPE_CAP);
    coco_cassette_set_fast(&gg->m, fast);
    guest_boot(gg);
}

typedef struct {
    const char *name;
    const char *program;     /* typed first                                  */
    const char *save;        /* the command that saves                       */
    const char *load;        /* the command that loads it back, or NULL      */
    uint16_t    from, to;    /* RAM the load must give back, or the program
                              * when both are 0                              */
    bool        extended;    /* needs Extended Color BASIC                   */
} save_case_t;

#define REM120 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" \
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

static const save_case_t cases[] = {
    { "a short program", "10 PRINT\"HELLO\"\n", "CSAVE\"T\"\n", "CLOAD\"T\"\n", 0, 0, false },
    { "a program of several blocks",
      "10 REM " REM120 "\n20 REM " REM120 "\n30 REM " REM120 "\n40 REM " REM120 "\n50 REM " REM120 "\n",
      "CSAVE\"T\"\n", "CLOAD\"T\"\n", 0, 0, false },
    { "an ASCII save", "10 PRINT\"HELLO\"\n20 GOTO 10\n", "CSAVE\"T\",A\n", "CLOAD\"T\"\n", 0, 0, false },
    { "a data file",
      "10 OPEN\"O\",#-1,\"D\":FORI=1TO40:PRINT#-1,\"ABCDEFGHIJ\":NEXT:CLOSE#-1\n", "RUN\n", NULL,
      0, 0, false },
    { "machine code, 712 bytes", "FORI=12288TO12999:POKEI,I AND 255:NEXT\n",
      "CSAVEM\"T\",12288,12999,12288\n", "CLOADM\"T\"\n", 12288, 12999, true },
    { "machine code, 255 bytes", "FORI=12288TO12542:POKEI,I AND 255:NEXT\n",
      "CSAVEM\"T\",12288,12542,12288\n", "CLOADM\"T\"\n", 12288, 12542, true },
};

static uint16_t peek16(const coco_t *m, uint16_t at) {
    return (uint16_t)(m->ram[at] << 8 | m->ram[at + 1u]);
}

static int one_save(rom_id_t basic, bool ext, const save_case_t *sc) {
    /* 1. The ROM's own save, noted, and the recorder's image of it. */
    boot(&a, basic, ext, tape_a, false);
    guest_type(&a, sc->program);
    /* A line with no number is run as it is typed. */
    bool direct = sc->program[0] > '9';
    CHECK(!direct || wait_ok(&a, 3000), "%s, %s: the program did not come back", label, sc->name);
    n_edge = n_span = 0;
    in_span = false;
    note_motor = motor_of(&a.m);
    note_level = (mc6821_out(&a.m.pia1.a) & 0xFCu) >= 0x80u;
    a.m.step_hook = note;
    guest_type(&a, sc->save);
    bool back = wait_ok(&a, 6000);
    a.m.step_hook = NULL;
    span_at[n_span] = n_edge;
    CHECK(back, "%s, %s: the save did not come back", label, sc->name);
    const coco_tape_t *ta = &a.m.tape;
    CHECK(n_span >= 2 && n_edge > 2000u && n_edge < EDGE_MAX, "%s, %s: %u spans, %u edges", label,
          sc->name, n_span, n_edge);
    CHECK(a.m.cas.rec_errors == 0 && !ta->overflow && ta->inserted && ta->len > 150u,
          "%s, %s: the recorder kept %u bytes, %u errors", label, sc->name, ta->len,
          a.m.cas.rec_errors);

    /* The same save by the trap, on another machine. */
    boot(&b, basic, ext, tape_b, true);
    guest_type(&b, sc->program);
    CHECK(!direct || wait_ok(&b, 3000), "%s, %s: the program did not come back, fast", label,
          sc->name);
    guest_type(&b, sc->save);
    CHECK(wait_ok(&b, 3000), "%s, %s: the trap's save did not come back", label, sc->name);
    const coco_tape_t *tb = &b.m.tape;
    bool same = ta->len == tb->len && memcmp(tape_a, tape_b, ta->len) == 0;
    if (!same) {
        uint32_t i = 0;
        while (i < ta->len && i < tb->len && tape_a[i] == tape_b[i]) i++;
        CHECK(false, "%s, %s: the recorder's image is %u bytes and the trap's %u, the first "
              "difference at %u", label, sc->name, ta->len, tb->len, i);
        return 1;
    }

    /* 2. The player's walk of the image against the ROM's edges. */
    memcpy(tape_c, tape_a, ta->len);
    boot(&b, basic, ext, tape_c, false);
    coco_tape_insert(&b.m, ta->len);
    unsigned bad = 0, span = 0, at = 0, walked = 0;
    uint32_t ticks;
    while (cassette_walk(&b.m, &ticks)) {
        walked++;
        /* A silence begins a span; the relay opened in place of the low
         * half before it. */
        if (ticks == CAS_SILENCE * CAS_CYCLE_TICKS && walked > 1u) {
            if (at != span_at[span + 1u]) bad++;
            span++;
            at = span_at[span < n_span ? span : n_span];
        }
        if (span >= n_span || at >= span_at[span + 1u] || edge[at] != ticks) {
            if (!bad)
                fprintf(stderr, "    %s, %s: span %u edge %u: the player %u ticks, the ROM %u\n",
                        label, sc->name, span, at - span_at[span < n_span ? span : 0],
                        ticks, span < n_span && at < n_edge ? edge[at] : 0u);
            bad++;
        }
        at++;
    }
    CHECK(bad == 0 && span + 1u == n_span && at == n_edge,
          "%s, %s: %u edge(s) differ; the player gave %u in %u spans, the ROM %u in %u", label,
          sc->name, bad, walked, span + 1u, n_edge, n_span);

    /* 3. Loaded back off the signal by the ROM's own reader. */
    if (sc->load) {
        boot(&b, basic, ext, tape_c, false);
        coco_tape_insert(&b.m, ta->len);
        guest_type(&b, sc->load);
        uint32_t f0 = b.m.fields;
        CHECK(wait_ok(&b, 6000) && !screen_has(&b.m, "ERROR"), "%s, %s: the load did not come back",
              label, sc->name);
        if (test_failures) guest_dump(&b.m, stderr);
        uint16_t from = sc->from, to = sc->to;
        if (!from) {
            from = peek16(&a.m, 0x19);
            to = (uint16_t)(peek16(&a.m, 0x1B) - 1u);
            CHECK(peek16(&b.m, 0x19) == from && peek16(&b.m, 0x1B) == to + 1u,
                  "%s, %s: the program's ends", label, sc->name);
        }
        CHECK(to > from && memcmp(&a.m.ram[from], &b.m.ram[from], (size_t)(to - from) + 1u) == 0,
              "%s, %s: $%04X-$%04X is not what was saved", label, sc->name, from, to);
        CHECK(b.m.tape.blocks_in == 0 && b.m.cas.edges > 2000u,
              "%s, %s: read by the trap, not off the signal", label, sc->name);
        printf("  %s, %s: %u bytes, %u edges in %u spans; loaded off the signal in %.1f s\n",
               label, sc->name, ta->len, n_edge, n_span, (b.m.fields - f0) / 59.92);
    } else {
        printf("  %s, %s: %u bytes, %u edges in %u spans\n", label, sc->name, ta->len, n_edge,
               n_span);
    }
    return 0;
}

/* ---- the port's cues, and the deck's manners ------------------------------- */

static bool run_to_request(guest_t *gg, tape_op_t op, int max) {
    for (int f = 0; f < max; f++) {
        if (coco_tape_pending(&gg->m) == op) return true;
        guest_fields(gg, 1);
    }
    return coco_tape_pending(&gg->m) == op;
}

static int cues(rom_id_t basic) {
    /* A tape to play: a short program, saved by the trap. */
    boot(&a, basic, false, tape_a, true);
    guest_type(&a, "10 PRINT\"HELLO\"\nCSAVE\"T\"\n");
    CHECK(wait_ok(&a, 600), "%s: no tape to play", label);
    uint32_t len = a.m.tape.len;

    /* CLOAD with the deck empty: the trap's request, by name; the tape
     * put in, the ROM reads the signal. */
    boot(&b, basic, false, tape_c, false);
    guest_type(&b, "CLOAD\"T\"\n");
    CHECK(run_to_request(&b, TAPE_FIND, 200) && b.m.tape.want_len == 1 && b.m.tape.want[0] == 'T',
          "%s: no request for the tape by name with the deck empty", label);
    memcpy(tape_c, tape_a, len);
    coco_tape_insert(&b.m, len);
    coco_tape_resume(&b.m);
    CHECK(wait_ok(&b, 3000) && b.m.tape.blocks_in == 0, "%s: the tape put in did not load", label);
    guest_type(&b, "LIST\n");
    CHECK(screen_has(&b.m, "10 PRINT\"HELLO\""), "%s: the program off the signal", label);

    /* A name the tape does not have: the reader runs off the end, and
     * stalls for the port, which gives up. The control: with the relay
     * open nothing is asked. */
    boot(&b, basic, false, tape_c, false);
    coco_tape_insert(&b.m, len);
    guest_fields(&b, 600);
    CHECK(coco_tape_pending(&b.m) == TAPE_NONE && b.m.tape.pos == 0 && !b.m.cas.playing,
          "%s: the deck moved with the relay open", label);
    guest_type(&b, "CLOAD\"NOPE\"\n");
    CHECK(run_to_request(&b, TAPE_END, 1200), "%s: no request at the tape's end", label);
    CHECK(b.m.tape.pos == len && b.m.cas.ended, "%s: the head at %u of %u", label, b.m.tape.pos,
          len);
    coco_tape_give_up(&b.m);
    CHECK(wait_ok(&b, 300), "%s: giving up did not reach the prompt", label);

    /* The relay opened part-way holds the player's place: the edges
     * played before and after add up to the walk's. */
    boot(&b, basic, false, tape_c, false);
    coco_tape_insert(&b.m, len);
    unsigned total = 0;
    uint32_t ticks;
    while (cassette_walk(&b.m, &ticks)) total++;
    boot(&b, basic, false, tape_c, false);
    coco_tape_insert(&b.m, len);
    guest_type(&b, "MOTOR ON\n");
    guest_fields(&b, 50);
    (void)coco_slow_read(&b.m, 0xFF20u);
    guest_type(&b, "MOTOR OFF\n");
    uint32_t held = b.m.cas.edges, pos = b.m.tape.pos;
    guest_fields(&b, 300);
    CHECK(held > 0 && b.m.cas.edges == held && b.m.tape.pos == pos && !b.m.cas.playing,
          "%s: the player ran on with the relay open", label);
    guest_type(&b, "MOTOR ON\n");
    guest_fields(&b, 600);
    (void)coco_slow_read(&b.m, 0xFF20u);
    CHECK(b.m.cas.ended && b.m.cas.edges == total, "%s: %u edges played across a stop, %u walked",
          label, b.m.cas.edges, total);

    /* A save onto a tape that is in the deck goes on at its end, and
     * the relay's spans the ROM wrote in do not move the player. */
    boot(&b, basic, false, tape_c, false);
    coco_tape_insert(&b.m, len);
    guest_type(&b, "10 PRINT\"BYE\"\nCSAVE\"U\"\n");
    CHECK(wait_ok(&b, 1200), "%s: the save onto the tape did not come back", label);
    CHECK(b.m.tape.len > len + 150u && memcmp(tape_c, tape_a, len) == 0 && b.m.tape.pos <= 1u &&
              b.m.cas.edges == 0,   /* the first byte is in the player's hand */
          "%s: after a save the tape is %u bytes, the head at %u, %u edges played", label,
          b.m.tape.len, b.m.tape.pos, b.m.cas.edges);

    /* A save cut short by the reset button: whole blocks only. */
    boot(&b, basic, false, tape_c, false);
    guest_type(&b, "10 REM " REM120 "\n20 REM " REM120 "\n30 REM " REM120 "\n");
    guest_type(&b, "CSAVE\"T\"\n");
    guest_fields(&b, 190);   /* the name block is down, the data part-way */
    coco_reset(&b.m);
    CHECK(wait_ok(&b, 300), "%s: no prompt after the reset", label);
    const coco_tape_t *t = &b.m.tape;
    cas_block_t blk;
    unsigned blocks = 0;
    uint32_t at = 0;
    while (cas_next_block(tape_c, t->len, at, &blk)) {
        CHECK(blk.sum_ok, "%s: a kept block's checksum", label);
        blocks++;
        at = blk.next;
    }
    CHECK(blocks >= 1 && blocks < 4 && b.m.cas.rec_errors == 1 &&
              (t->len == at || t->len == at + 1u),
          "%s: a save cut short kept %u blocks in %u bytes, %u past the last, %u errors", label,
          blocks, t->len, t->len - at, b.m.cas.rec_errors);
    printf("  %s: the cues, the relay and a save cut short\n", label);
    return 0;
}

int main(void) {
    const char *dir;
    guest_find_roms(&dir);
    static const struct { rom_id_t basic; bool ext; const char *label; } roms[] = {
        { ROM_COCO_BAS11, false, "Color BASIC 1.1" },
        { ROM_COCO_BAS13, false, "Color BASIC 1.3" },
        { ROM_COCO_BAS13, true,  "Extended BASIC 1.1" },
    };
    int ran = 0;
    for (size_t r = 0; r < sizeof roms / sizeof roms[0]; r++) {
        label = roms[r].label;
        if (!guest_have_rom(roms[r].basic) || (roms[r].ext && !guest_have_rom(ROM_COCO_EXTBAS11))) {
            printf("%s: no ROM, left out\n", label);
            continue;
        }
        ran++;
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
            if (!cases[i].extended || roms[r].ext) one_save(roms[r].basic, roms[r].ext, &cases[i]);
        if (!roms[r].ext) cues(roms[r].basic);
    }
    if (!ran) {
        printf("skipped: no Color BASIC ROM in %s (roms/README.md)\n", dir);
        return TEST_SKIP_CODE;
    }
    TEST_DONE();
}
