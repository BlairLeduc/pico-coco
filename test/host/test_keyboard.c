/* test_keyboard.c — the keyboard, settled by executing the ROMs
 * (design.md §11, §17 M5).
 *
 * 1. The sweep: every cell of the matrix pressed at the prompt, alone and
 *    with SHIFT, and what the ROM's POLCAT returned (§11.2).
 * 2. Every entry of the PicoCalc table typed through keymatrix into the
 *    ROM, and what it decoded; the editing keys and BREAK seen working.
 * 3. The hold and gap the ROM needs, with controls that must fail; that
 *    it does not repeat a held key; one key of type-ahead while a program
 *    runs.
 * 4. A long program typed through the replay, every line stored and run,
 *    against the scan's bare minimum.
 *
 * In Color BASIC 1.1, Color BASIC 1.3, and Extended Color BASIC 1.1 over
 * 1.3, each in 64 KB; a case whose ROM is absent is left out, and with
 * none present the test skips. Adapted from pico-oric's (§5). With
 * --print it prints the sweep as a table, for keymap_picocalc.c.
 */

#include <string.h>

#include "guest.h"
#include "test_util.h"

static guest_t g;
static coco_t  booted;
static int     input_row;   /* the row the prompt's cursor is on */
static const char *label;

typedef struct {
    rom_id_t    basic;
    bool        extended;
    const char *label;
    unsigned    need_hold;   /* fields a key must be down, as executed */
} rom_case_t;

/* ---- memory ------------------------------------------------------------ */

static uint8_t peek(uint16_t a) {
    int32_t i = coco_ram_index(&g.m, a);
    return i < 0 ? 0xFFu : g.m.ram[i];
}

static uint16_t peek16(uint16_t a) {
    return (uint16_t)(peek(a) << 8 | peek((uint16_t)(a + 1u)));
}

/* ---- running with POLCAT watched --------------------------------------- */

/* Color BASIC's POLCAT, through its vector at $A000: it returns with the
 * key in A, or 0 with Z set if none. Every key the ROM takes, at the
 * prompt or from the BREAK check while a program runs, comes back
 * through it. */
#define LOG_MAX 64
static uint8_t  decoded[LOG_MAX];
static int      n_decoded;
static uint16_t polcat, ret_pc, ret_s;
static bool     inside;

static void hook(coco_t *m) {
    mc6809_t *c = &m->cpu;
    if (!inside && c->pc == polcat) {
        inside = true;
        ret_pc = peek16(c->s);
        ret_s = (uint16_t)(c->s + 2u);
    } else if (inside && c->pc == ret_pc && c->s == ret_s) {
        inside = false;
        if (c->a && n_decoded < LOG_MAX) decoded[n_decoded++] = c->a;
    }
}

static void fresh(void) {
    coco_copy(&g.m, &booted);
    keymatrix_init(&g.k);
    g.m.step_hook = hook;
    n_decoded = 0;
    inside = false;
}

/* Straight into the matrix, no keymatrix. */
static void fields_raw(int n) {
    for (int i = 0; i < n; i++) coco_run_field(&g.m);
}

/* The input line as typed. */
static const char *input(void) {
    return guest_row(&g.m, input_row);
}

static void log_str(char *out, size_t n) {
    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < n_decoded && at + 4 < n; i++)
        at += (size_t)snprintf(out + at, n - at, " %02X", decoded[i]);
}

/* ---- 1. the sweep ------------------------------------------------------ */

/* What each cell decodes to, alone and with SHIFT, read off the three
 * ROMs by this sweep and kept as its regression (keymap_picocalc.c,
 * §11.2): 0 for nothing. They agree in every cell (2026-11-15). SHIFT+0
 * decodes nothing: it turns lower case on and off. The four empty cells
 * of row 6 decode what follows the ROM's table, and are not checked. */
static const uint8_t sweep_want[COCO_KEY_ROWS][COCO_KEY_COLS][2] = {
    { {0x40,0x13},{0x41,0x41},{0x42,0x42},{0x43,0x43},{0x44,0x44},{0x45,0x45},{0x46,0x46},{0x47,0x47} },
    { {0x48,0x48},{0x49,0x49},{0x4A,0x4A},{0x4B,0x4B},{0x4C,0x4C},{0x4D,0x4D},{0x4E,0x4E},{0x4F,0x4F} },
    { {0x50,0x50},{0x51,0x51},{0x52,0x52},{0x53,0x53},{0x54,0x54},{0x55,0x55},{0x56,0x56},{0x57,0x57} },
    { {0x58,0x58},{0x59,0x59},{0x5A,0x5A},{0x5E,0x5F},{0x0A,0x5B},{0x08,0x15},{0x09,0x5D},{0x20,0x20} },
    { {0x30,0x00},{0x31,0x21},{0x32,0x22},{0x33,0x23},{0x34,0x24},{0x35,0x25},{0x36,0x26},{0x37,0x27} },
    { {0x38,0x28},{0x39,0x29},{0x3A,0x2A},{0x3B,0x2B},{0x2C,0x3C},{0x2D,0x3D},{0x2E,0x3E},{0x2F,0x3F} },
    { {0x0D,0x0D},{0x0C,0x5C},{0x03,0x03},{0,0},{0,0},{0,0},{0,0},{0,0} },
};

static bool unchecked(int r, int c) {
    return r == CK_ROW_SHIFT && c >= 3;
}

static int run_sweep(bool print) {
    for (int r = 0; r < COCO_KEY_ROWS; r++) {
        if (print) printf("    { ");
        for (int c = 0; c < COCO_KEY_COLS; c++) {
            uint8_t got[2];
            for (int sh = 0; sh < 2; sh++) {
                fresh();
                if (sh) coco_key_set(&g.m, CK_ROW_SHIFT, CK_COL_SHIFT, true);
                fields_raw(2);
                coco_key_set(&g.m, r, c, true);
                fields_raw(4);
                coco_key_set(&g.m, r, c, false);
                fields_raw(2);
                coco_key_set(&g.m, CK_ROW_SHIFT, CK_COL_SHIFT, false);
                fields_raw(4);
                got[sh] = n_decoded == 1 ? decoded[0] : 0;
                if (print || unchecked(r, c)) continue;
                CHECK(n_decoded <= 1, "%s: (%d,%d)%s decoded %d keys", label, r, c,
                      sh ? " with SHIFT" : "", n_decoded);
                CHECK(got[sh] == sweep_want[r][c][sh], "%s: (%d,%d)%s decoded $%02X, want $%02X",
                      label, r, c, sh ? " with SHIFT" : "", got[sh], sweep_want[r][c][sh]);
            }
            if (print) printf("{0x%02X,0x%02X}%s", got[0], got[1], c < 7 ? "," : "");
        }
        if (print) printf(" },\n");
    }
    return 0;
}

/* ---- 2. every entry, through keymatrix --------------------------------- */

/* What the ROM decodes for each entry with a cell (keymap_picocalc.c):
 * a printable character is itself, a letter in capitals; the keys are the
 * sweep's codes. 0 where it decodes nothing. */
static uint8_t entry_want(const keymap_t *e) {
    if (e->flags & KM_ALT) {
        switch (e->code) {
        case PICOCALC_KEY_LEFT:  return 0x15;
        case PICOCALC_KEY_UP:    return 0x5F;
        case PICOCALC_KEY_DOWN:  return 0x5B;
        case PICOCALC_KEY_RIGHT: return 0x5D;
        case '0':                return 0x00;   /* lower case, on and off */
        case '@':                return 0x13;
        default:                 return 0xFF;
        }
    }
    switch (e->code) {
    case PICOCALC_KEY_ENTER:     return 0x0D;
    case PICOCALC_KEY_INSERT:    return 0x0D;
    case PICOCALC_KEY_BACKSPACE: return 0x08;
    case PICOCALC_KEY_DEL:       return 0x08;
    case PICOCALC_KEY_LEFT:      return 0x08;
    case PICOCALC_KEY_END:       return 0x15;
    case PICOCALC_KEY_ESC:       return 0x03;
    case PICOCALC_KEY_BREAK:     return 0x03;
    case PICOCALC_KEY_TAB:       return 0x0C;
    case PICOCALC_KEY_HOME:      return 0x5C;
    case PICOCALC_KEY_UP:        return 0x5E;
    case PICOCALC_KEY_PAGE_UP:   return 0x5F;
    case PICOCALC_KEY_DOWN:      return 0x0A;
    case PICOCALC_KEY_PAGE_DOWN: return 0x5B;
    case PICOCALC_KEY_RIGHT:     return 0x09;
    default: break;
    }
    if (e->code >= 'a' && e->code <= 'z') return (uint8_t)(e->code - 'a' + 'A');
    if (e->code >= 0x20u && e->code < 0x7Fu) return e->code;
    return 0xFF;
}

/* The events the PicoCalc sends for an entry: inside Alt for the Alt
 * layer, and inside Shift where the code is a Shift chord on the PicoCalc
 * (keymap_picocalc_canonical). */
static void type_entry(const keymap_t *e) {
    bool alt = (e->flags & KM_ALT) != 0;
    bool shift = keymap_picocalc_canonical(e->code) != e->code ||
                 (e->code >= 'A' && e->code <= 'Z');
    guest_press(&g, e->code, shift, alt);
}

static int every_entry(void) {
    unsigned typed = 0;
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->flags & KM_NOCELL) continue;
        uint8_t want = entry_want(e);
        CHECK(want != 0xFF, "%s: entry 0x%02X has no expected decode", label, e->code);
        fresh();
        uint8_t cas = peek(0x011A);
        type_entry(e);
        guest_fields(&g, 4);
        typed++;
        char got[LOG_MAX * 3 + 1];
        log_str(got, sizeof got);
        if (want) {
            CHECK(n_decoded == 1 && decoded[0] == want,
                  "%s: %s0x%02X ('%c') decoded [%s ], want $%02X", label,
                  (e->flags & KM_ALT) ? "Alt+" : "", e->code,
                  e->code >= 0x20 && e->code < 0x7F ? e->code : '?', got, want);
        } else {
            /* Alt+0, SHIFT+0: CASFLG at $011A flips, and nothing typed. */
            CHECK(n_decoded == 0 && peek(0x011A) != cas,
                  "%s: Alt+0 decoded [%s ] and left CASFLG $%02X from $%02X", label, got,
                  peek(0x011A), cas);
        }
    }
    printf("  %s: %u entries typed through keymatrix, each decoded as the table says\n", label,
           typed);
    return 0;
}

/* The keys that edit the line, clear the screen and stop a program, seen
 * on the screen. */
static int editing_keys(void) {
    /* Backspace rubs out; End (SHIFT+LEFT) and Alt+Left erase the line. */
    fresh();
    guest_type(&g, "abcd");
    guest_press(&g, PICOCALC_KEY_BACKSPACE, false, false);
    guest_fields(&g, 5);
    CHECK(strcmp(input(), "ABC") == 0, "%s: Backspace left \"%s\"", label, input());
    guest_press(&g, PICOCALC_KEY_END, true, false);
    guest_fields(&g, 5);
    CHECK(strcmp(input(), "") == 0, "%s: End (SHIFT+LEFT) left \"%s\"", label, input());
    guest_type(&g, "xyz");
    guest_press(&g, PICOCALC_KEY_LEFT, false, true);
    guest_fields(&g, 5);
    CHECK(strcmp(input(), "") == 0, "%s: Alt+Left left \"%s\"", label, input());

    /* Tab is CLEAR: the screen cleared, the cursor at the top. */
    fresh();
    guest_press(&g, PICOCALC_KEY_TAB, false, false);
    guest_fields(&g, 5);
    CHECK(guest_find_row(&g.m, "OK", 0) < 0, "%s: CLEAR left OK on the screen", label);

    /* Esc is BREAK: a running program stops with BREAK IN 10. */
    fresh();
    guest_type(&g, "10 GOTO 10\nrun\n");
    guest_fields(&g, 20);
    CHECK(guest_find_row(&g.m, "BREAK", 0) < 0, "%s: BREAK before Esc", label);
    guest_press(&g, PICOCALC_KEY_ESC, false, false);
    guest_fields(&g, 10);
    CHECK(guest_find_row(&g.m, "BREAK IN 10", 0) >= 0, "%s: Esc did not break the program",
          label);

    /* Alt+0 is SHIFT+0: lower case on, then letters show inverse, which
     * guest_row reads as the plain letter, and back off again. */
    fresh();
    guest_press(&g, '0', false, true);
    uint8_t lower = peek(0x011A);
    guest_press(&g, '0', false, true);
    CHECK(lower == 0x00 && peek(0x011A) == 0xFF, "%s: Alt+0 twice gave CASFLG $%02X then $%02X",
          label, lower, peek(0x011A));
    return 0;
}

/* ---- 3. the hold and gap the ROM needs ---------------------------------- */

/* "AABBAB112" straight into the matrix, each key held `hold` fields with
 * `gap` up between, from 240 starting phases a thousand ticks apart, the
 * whole field; true if the ROM read it back every time. The phases where
 * a ROM loses a key held a field too few are a few thousand ticks wide:
 * 48 phases missed Extended BASIC's. */
static bool types_at(unsigned hold, unsigned gap) {
    static const uint8_t keys[][2] = {
        { 0, 1 }, { 0, 1 }, { 0, 2 }, { 0, 2 }, { 0, 1 }, { 0, 2 }, { 4, 1 }, { 4, 1 }, { 4, 2 },
    };
    for (unsigned phase = 0; phase < 240; phase++) {
        fresh();
        g.m.step_hook = NULL;
        coco_run(&g.m, phase * 1000u);
        for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            coco_key_set(&g.m, keys[i][0], keys[i][1], true);
            fields_raw((int)hold);
            coco_key_set(&g.m, keys[i][0], keys[i][1], false);
            fields_raw((int)gap);
        }
        fields_raw(10);
        if (strcmp(input(), "AABBAB112") != 0) return false;
    }
    return true;
}

/* How many times one key held `hold` fields is typed. */
static size_t typed_by_hold(unsigned hold) {
    fresh();
    g.m.step_hook = NULL;
    coco_key_set(&g.m, 0, 1, true);
    fields_raw((int)hold);
    coco_key_set(&g.m, 0, 1, false);
    fields_raw(10);
    return strlen(input());
}

static int hold_and_gap(const rom_case_t *rc) {
    int hold = -1, gap = -1;
    for (int h = 1; h <= 8 && hold < 0; h++)
        if (types_at((unsigned)h, 4)) hold = h;
    for (int gp = 0; gp <= 4 && hold > 0 && gap < 0; gp++)
        if (types_at((unsigned)hold, (unsigned)gp)) gap = gp;
    printf("  %s: the ROM needs a key held %d fields and %d up; the replay uses %u and %u\n",
           label, hold, gap, (unsigned)COCO_KEY_MIN_FIELDS, (unsigned)COCO_KEY_GAP_FIELDS);

    /* Settled 2026-11-15 (config.h). The controls must fail. */
    CHECK(hold == (int)rc->need_hold && gap == 1, "%s: the ROM needs %d held and %d up, "
          "expected %u and 1", label, hold, gap, rc->need_hold);
    CHECK(!types_at(rc->need_hold - 1u, 4), "%s: control: %u fields held should lose keys",
          label, rc->need_hold - 1u);
    CHECK(!types_at(rc->need_hold, 0), "%s: control: no field up should lose keys", label);
    CHECK(types_at(COCO_KEY_MIN_FIELDS, COCO_KEY_GAP_FIELDS), "%s: the replay's own timing "
          "fails", label);
    CHECK(COCO_KEY_MIN_FIELDS >= rc->need_hold && COCO_KEY_GAP_FIELDS >= 1u,
          "%s: the replay is faster than the ROM", label);

    /* No repeat: a key held five seconds types once. */
    size_t r300 = typed_by_hold(300);
    CHECK(r300 == 1, "%s: a key held 300 fields typed %zu times", label, r300);

    /* Through keymatrix, a line arriving in one poll, as a fast typist's
     * would: the replay's own rate, in fields per key. */
    fresh();
    const char *line = "print 2+2\n";
    for (const char *c = line; *c; c++) {
        picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text((uint8_t)*c, ev);
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
    }
    uint32_t f0 = g.m.fields;
    CHECK(guest_settle(&g, 400), "%s: the line was not replayed in 400 fields", label);
    unsigned keys = (unsigned)strlen(line);
    unsigned took = (unsigned)(g.m.fields - f0);
    printf("  %s: a line of %u keys in one poll replays in %u fields, %.1f a key\n", label,
           keys, took, (double)took / keys);
    guest_fields(&g, 10);
    CHECK(strcmp(guest_row(&g.m, input_row + 1), " 4") == 0, "%s: PRINT 2+2 printed \"%s\"",
          label, guest_row(&g.m, input_row + 1));
    return 0;
}

/* Keys typed while a program runs. BASIC's BREAK check keeps the key it
 * finds for INKEY$, each over the last, so of several typed during a loop
 * INKEY$ reads the last, once, and nothing reaches the prompt after. */
static int type_ahead(void) {
    fresh();
    guest_type(&g, "for i=1 to 3000:next:print \"k\";inkey$;inkey$\n");
    guest_type(&g, "xyz");
    CHECK(guest_find_row(&g.m, "OK", input_row + 1) < 0,
          "%s: the loop finished before the keys were typed", label);
    int row = -1;
    for (int f = 0; f < 2000 && row < 0; f++) {
        guest_fields(&g, 1);
        row = guest_find_row(&g.m, "OK", input_row + 1);
    }
    CHECK(row > 0, "%s: the loop did not finish", label);
    guest_fields(&g, 10);
    if (row <= 0) return 0;
    char got[40], prompt[40];
    snprintf(got, sizeof got, "%s", guest_row(&g.m, row - 1));
    snprintf(prompt, sizeof prompt, "%s", guest_row(&g.m, row + 1));
    printf("  %s: XYZ typed during the loop: INKEY$ twice reads \"%s\", the prompt \"%s\"\n",
           label, got + 1, prompt);
    CHECK(strcmp(got, "KZ") == 0, "%s: INKEY$ after XYZ: \"%s\", want \"KZ\"", label, got);
    CHECK(strcmp(prompt, "") == 0, "%s: the prompt kept \"%s\"", label, prompt);
    return 0;
}

/* ---- 4. a long program --------------------------------------------------- */

/* Lines of a program BASIC has stored: its chain of line links from
 * TXTTAB at $19, and the last line's number. */
static int lines_stored(unsigned *last) {
    int n = 0;
    uint16_t p = peek16(0x0019);
    *last = 0;
    while (n < 1000 && peek16(p)) {
        *last = peek16((uint16_t)(p + 2u));
        p = peek16(p);
        n++;
    }
    return n;
}

#define PROGRAM_LINES 60

static void program_line(int n, char *out, size_t len) {
    snprintf(out, len, "%d print %d*%d\n", n, n, n);
}

/* The program typed at the replay's rate, each line arriving in one poll
 * as a fast typist's would, through the scroll. */
static int program_at_speed(void) {
    fresh();
    g.m.step_hook = NULL;
    uint32_t f0 = g.m.fields;
    for (int n = 1; n <= PROGRAM_LINES; n++) {
        char line[32];
        program_line(n, line, sizeof line);
        for (const char *c = line; *c; c++) {
            picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
            unsigned k = keymap_picocalc_text((uint8_t)*c, ev);
            for (unsigned i = 0; i < k; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
        }
        guest_settle(&g, 600);
    }
    guest_fields(&g, 30);
    unsigned last;
    int stored = lines_stored(&last);
    printf("  %s: %d lines typed through the replay in %.1f s: %d stored, the last %u\n", label,
           PROGRAM_LINES, (g.m.fields - f0) / 59.94, stored, last);
    CHECK(stored == PROGRAM_LINES && last == PROGRAM_LINES, "%s: %d of %d lines stored", label,
          stored, PROGRAM_LINES);
    guest_type(&g, "run\n");
    guest_fields(&g, 120);
    int ok = guest_find_row(&g.m, "OK", 1);
    CHECK(ok > 2 && strcmp(guest_row(&g.m, ok - 1), " 3600") == 0 &&
              strcmp(guest_row(&g.m, ok - 2), " 3481") == 0,
          "%s: the typed program did not run whole", label);
    if (test_failures) guest_dump(&g.m, stdout);
    return 0;
}

/* The same program straight into the matrix, each key held `hold` fields
 * with `gap` up, SHIFT a field ahead of a key that needs it: how many
 * lines BASIC stored. */
static int program_at(unsigned hold, unsigned gap) {
    fresh();
    g.m.step_hook = NULL;
    for (int n = 1; n <= PROGRAM_LINES; n++) {
        char line[32];
        program_line(n, line, sizeof line);
        for (const char *c = line; *c; c++) {
            uint8_t code = *c == '\n' ? PICOCALC_KEY_ENTER : (uint8_t)*c;
            const keymap_t *e = NULL;
            for (size_t i = 0; i < keymap_picocalc_len && !e; i++)
                if (keymap_picocalc[i].code == code && !(keymap_picocalc[i].flags & KM_ALT))
                    e = &keymap_picocalc[i];
            if (e->flags & KM_SHIFT) {
                coco_key_set(&g.m, CK_ROW_SHIFT, CK_COL_SHIFT, true);
                fields_raw(1);
            }
            coco_key_set(&g.m, e->row, e->col, true);
            fields_raw((int)hold);
            memset(g.m.keys, 0, sizeof g.m.keys);
            fields_raw((int)gap);
        }
    }
    fields_raw(30);
    unsigned last;
    return lines_stored(&last);
}

/* The replay's margin over the scan: the program at the replay's rate
 * and at the ROM's bare minimum, and the control, a field held, which
 * must lose lines. A field short of the minimum is not a control: the
 * phases where 1.1 loses a key held 2 fields are few, and 60 lines can
 * miss them all. */
static int program_margin(const rom_case_t *rc) {
    int shy = program_at(1, 1);
    int bare = program_at(rc->need_hold, 1);
    int ours = program_at(COCO_KEY_MIN_FIELDS, COCO_KEY_GAP_FIELDS);
    printf("  %s: %d lines typed straight into the matrix store %d at %u held and 1 up, "
           "%d at %u and 1, %d at the replay's %u and %u\n",
           label, PROGRAM_LINES, shy, 1u, bare, rc->need_hold, ours,
           (unsigned)COCO_KEY_MIN_FIELDS, (unsigned)COCO_KEY_GAP_FIELDS);
    CHECK(ours == PROGRAM_LINES, "%s: at the replay's rate %d of %d lines were stored", label,
          ours, PROGRAM_LINES);
    CHECK(shy < PROGRAM_LINES, "%s: control: at 1 held the program should lose lines", label);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir;
    guest_find_roms(&dir);
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;

    static const rom_case_t cases[] = {
        { ROM_COCO_BAS11, false, "Color BASIC 1.1",    3 },
        { ROM_COCO_BAS13, false, "Color BASIC 1.3",    2 },
        { ROM_COCO_BAS13, true,  "Extended BASIC 1.1", 3 },
    };
    int ran = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const rom_case_t *rc = &cases[i];
        label = rc->label;
        if (!guest_have_rom(rc->basic) || (rc->extended && !guest_have_rom(ROM_COCO_EXTBAS11))) {
            printf("%s: no ROM, left out\n", label);
            continue;
        }
        ran++;
        guest_build_as(&g, 64, rc->basic, rc->extended);
        if (!guest_boot(&g)) {
            CHECK(false, "%s did not boot", label);
            continue;
        }
        coco_copy(&booted, &g.m);
        input_row = guest_find_row(&g.m, "OK", 0) + 1;
        polcat = (uint16_t)(g.m.rom[0x2000] << 8 | g.m.rom[0x2001]);
        printf("%s (POLCAT $%04X)\n", label, polcat);
        if (print) {
            run_sweep(true);
            continue;
        }
        run_sweep(false);
        every_entry();
        editing_keys();
        hold_and_gap(rc);
        type_ahead();
        program_at_speed();
        program_margin(rc);
    }
    if (!ran) {
        printf("skipped: no Color BASIC ROM in %s (roms/README.md)\n", dir);
        return TEST_SKIP_CODE;
    }
    if (print) return 0;
    TEST_DONE();
}
