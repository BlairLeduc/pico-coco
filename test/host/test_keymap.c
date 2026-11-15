/* test_keymap.c — the keymap table and the held-key set (design.md §11).
 * Needs no ROM, so CI runs it; test_keyboard checks the same table
 * against the ROMs. Adapted from pico-oric's (§5).
 */

#include <stdio.h>
#include <string.h>

#include "coco.h"
#include "keymatrix.h"
#include "test_util.h"

static coco_t      m;
static keymatrix_t k;

static void fresh(void) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&m, &cfg);
    keymatrix_init(&k);
}

static bool cell_down(uint8_t row, uint8_t col) {
    return (m.keys[col] >> row) & 1u;
}

static bool shift(void) { return cell_down(CK_ROW_SHIFT, CK_COL_SHIFT); }

/* Cells down other than SHIFT. */
static unsigned cells_down(void) {
    unsigned n = 0;
    for (unsigned c = 0; c < COCO_KEY_COLS; c++)
        for (unsigned r = 0; r < COCO_KEY_ROWS; r++)
            if (cell_down((uint8_t)r, (uint8_t)c) &&
                !(r == CK_ROW_SHIFT && c == CK_COL_SHIFT))
                n++;
    return n;
}

static bool matrix_empty(void) {
    for (unsigned c = 0; c < COCO_KEY_COLS; c++)
        if (m.keys[c]) return false;
    return true;
}

static const keymap_t *entry_for(uint8_t code, bool alt) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->code == code && !!(e->flags & KM_ALT) == alt) return e;
    }
    return NULL;
}

static void press(uint8_t code)   { keymatrix_event(&k, KEY_EV_PRESSED, code); }
static void release(uint8_t code) { keymatrix_event(&k, KEY_EV_RELEASED, code); }
static void fields(int n) { for (int i = 0; i < n; i++) keymatrix_field(&k, &m); }

/* Characters with no key on the CoCo (keymap_picocalc.c). */
static bool no_coco_key(unsigned c) {
    return c == '{' || c == '}' || c == '|' || c == '`' || c == '~';
}

int main(void) {
    /* ---- table invariants (§11.3) ------------------------------------- */
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (!(e->flags & KM_NOCELL)) {
            CHECK(e->row < COCO_KEY_ROWS && e->col < COCO_KEY_COLS,
                  "code 0x%02X: cell (%u,%u) off the matrix", e->code, e->row, e->col);
            /* SHIFT is a flag and the host's Shift, never an entry's
             * cell; the four empty cells of row 6 are no key's. */
            CHECK(e->row != CK_ROW_SHIFT || e->col <= 2,
                  "code 0x%02X bound to (%u,%u), not a key", e->code, e->row, e->col);
        }
        CHECK((e->flags & (KM_SHIFT | KM_NOSHIFT)) != (KM_SHIFT | KM_NOSHIFT),
              "code 0x%02X both adds and removes SHIFT", e->code);
        CHECK(!(e->flags & (KM_SHIFT | KM_NOSHIFT)) || !(e->flags & KM_NOCELL),
              "code 0x%02X changes SHIFT and has no cell", e->code);
        if (e->flags & KM_MENU)
            CHECK(e->row <= KM_PAGE_ABOUT, "code 0x%02X opens page %u", e->code, e->row);
        /* The MCU keeps Alt+, . Space and B for itself, and Alt+I is its
         * Insert (hardware-notes.md §6.3): a binding there never fires.
         * Letters arrive in capitals with Alt. */
        if (e->flags & KM_ALT) {
            CHECK(strchr(",. BI", e->code) == NULL, "Alt+'%c' is consumed by the MCU",
                  e->code);
            CHECK(!(e->code >= 'a' && e->code <= 'z'),
                  "Alt+'%c': Alt letters arrive in capitals", e->code);
        }
        CHECK(e->code != PICOCALC_KEY_ALT && e->code != PICOCALC_KEY_CTRL &&
                  e->code != PICOCALC_KEY_SHIFT_L && e->code != PICOCALC_KEY_SHIFT_R,
              "a modifier is bound as a key");
        for (size_t j = i + 1; j < keymap_picocalc_len; j++) {
            const keymap_t *f = &keymap_picocalc[j];
            CHECK(!(e->code == f->code && (e->flags & KM_ALT) == (f->flags & KM_ALT)),
                  "code 0x%02X bound twice", e->code);
        }
    }
    /* Every key the PicoCalc sends only as a Shift chord has an entry:
     * canonicalisation pairs its press with its release, but binds
     * nothing (hardware-notes.md §6.3, EL §7.2). */
    {
        static const uint8_t alts[] = {
            PICOCALC_KEY_INSERT, PICOCALC_KEY_BREAK, PICOCALC_KEY_HOME,
            PICOCALC_KEY_END, PICOCALC_KEY_PAGE_UP, PICOCALC_KEY_PAGE_DOWN,
            PICOCALC_KEY_F6, PICOCALC_KEY_F10,
        };
        for (unsigned i = 0; i < sizeof alts; i++)
            CHECK(entry_for(alts[i], false) != NULL, "Shift alternate 0x%02X has no entry",
                  alts[i]);
    }

    /* Every character the PicoCalc types reaches the CoCo (EL §7.2), but
     * for those the CoCo has no key for. */
    for (unsigned c = 0x20; c < 0x7F; c++) {
        bool none = no_coco_key(c);
        CHECK((entry_for((uint8_t)c, false) != NULL) != none, "'%c': %s", c,
              none ? "the CoCo has no key for it" : "no entry");
    }
    /* A PicoCalc letter types the CoCo's letter whatever its case
     * (§11.3): both on one cell, the capital without SHIFT. Which cell,
     * and what each entry types, test_keyboard settles through the ROMs. */
    for (unsigned c = 'a'; c <= 'z'; c++) {
        const keymap_t *lo = entry_for((uint8_t)c, false);
        const keymap_t *up = entry_for((uint8_t)(c - 'a' + 'A'), false);
        if (!lo || !up) continue;
        CHECK(lo->row == up->row && lo->col == up->col, "'%c' and '%c' on different cells", c,
              c - 'a' + 'A');
        CHECK(lo->flags == 0 && up->flags == KM_NOSHIFT, "'%c' / '%c': flags %02X %02X", c,
              c - 'a' + 'A', lo->flags, up->flags);
    }
    /* No two characters share a cell and SHIFT, but a letter's two cases
     * and the keys the PicoCalc has twice (Backspace and Del, and End,
     * which is SHIFT+LEFT as '_' is SHIFT+UP). */
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->flags & (KM_NOCELL | KM_ALT)) continue;
        for (size_t j = i + 1; j < keymap_picocalc_len; j++) {
            const keymap_t *f = &keymap_picocalc[j];
            if (f->flags & (KM_NOCELL | KM_ALT)) continue;
            if (e->row != f->row || e->col != f->col) continue;
            if (!!(e->flags & KM_SHIFT) != !!(f->flags & KM_SHIFT)) continue;
            bool letters = keymap_picocalc_canonical(e->code) ==
                           keymap_picocalc_canonical(f->code);
            bool keys = e->code >= 0x80u || f->code >= 0x80u || e->code < 0x20u ||
                        f->code < 0x20u;
            CHECK(letters || keys, "0x%02X and 0x%02X share (%u,%u)", e->code, f->code,
                  e->row, e->col);
        }
    }

    /* Canonical identity is idempotent, and maps both halves of a key
     * to one physical key. */
    for (unsigned c = 0; c < 256; c++) {
        uint8_t once = keymap_picocalc_canonical((uint8_t)c);
        CHECK(keymap_picocalc_canonical(once) == once, "canonical(0x%02X) not stable", c);
    }
    CHECK(keymap_picocalc_canonical('A') == 'a', "A is a");
    CHECK(keymap_picocalc_canonical('!') == '1', "! is 1");
    CHECK(keymap_picocalc_canonical(':') == ';', ": is ;");
    CHECK(keymap_picocalc_canonical(0xD2u) == PICOCALC_KEY_TAB, "Home is on Tab");

    /* ---- text as PicoCalc events: the UART's path (§11.1) ------------- */
    {
        picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
        CHECK(keymap_picocalc_text('a', ev) == 2 && ev[0].state == KEY_EV_PRESSED &&
                  ev[0].code == 'a' && ev[1].state == KEY_EV_RELEASED && ev[1].code == 'a',
              "a is a press and a release");
        CHECK(keymap_picocalc_text(PICOCALC_KEY_F1, ev) == 2 && ev[0].code == PICOCALC_KEY_F1,
              "F1 is itself");
        CHECK(keymap_picocalc_text(PICOCALC_KEY_F6, ev) == 4 &&
              ev[0].code == PICOCALC_KEY_SHIFT_L && ev[1].code == PICOCALC_KEY_F6 &&
              ev[2].code == PICOCALC_KEY_F6 && ev[3].code == PICOCALC_KEY_SHIFT_L,
              "F6 is Shift+F1's code inside Shift");
        CHECK(keymap_picocalc_text(0x8Au, ev) == 0 && keymap_picocalc_text(0x80u, ev) == 0,
              "no key sends 0x8A or 0x80");
        CHECK(keymap_picocalc_text('!', ev) == 4 && ev[0].code == PICOCALC_KEY_SHIFT_L &&
                  ev[1].code == '!' && ev[2].code == '!' && ev[2].state == KEY_EV_RELEASED &&
                  ev[3].code == PICOCALC_KEY_SHIFT_L && ev[3].state == KEY_EV_RELEASED,
              "! is inside Shift, as the PicoCalc types it");
        CHECK(keymap_picocalc_text('A', ev) == 4 && ev[0].code == PICOCALC_KEY_SHIFT_L,
              "A is inside Shift");
        CHECK(keymap_picocalc_text('\r', ev) == 2 && ev[0].code == PICOCALC_KEY_ENTER,
              "CR is Enter");
        CHECK(keymap_picocalc_text('\n', ev) == 2 && ev[0].code == PICOCALC_KEY_ENTER,
              "LF is Enter");
        CHECK(keymap_picocalc_text(0x7Fu, ev) == 2 && ev[0].code == PICOCALC_KEY_BACKSPACE,
              "DEL is Backspace");
        CHECK(keymap_picocalc_text(0x1Bu, ev) == 2 && ev[0].code == PICOCALC_KEY_ESC,
              "ESC is Esc");
        CHECK(keymap_picocalc_text(0x09u, ev) == 2 && ev[0].code == PICOCALC_KEY_TAB,
              "TAB is Tab");
        CHECK(keymap_picocalc_text(0x14u, ev) == 4 && ev[0].code == PICOCALC_KEY_CTRL &&
                  ev[1].code == 't' && ev[3].code == PICOCALC_KEY_CTRL,
              "^T is Ctrl+t");
        CHECK(keymap_picocalc_text(0x00u, ev) == 0 && keymap_picocalc_text(0x1Cu, ev) == 0,
              "bytes no key sends give no events");
        /* Every printable byte's events reach a binding in the table,
         * but those the CoCo has no key for. */
        for (unsigned c = 0x20; c < 0x7F; c++) {
            if (no_coco_key(c)) continue;
            unsigned n = keymap_picocalc_text((uint8_t)c, ev);
            CHECK(n >= 2 && entry_for(ev[n == 4 ? 1 : 0].code, false),
                  "'%c' sends a code with no binding", c);
        }
    }

    /* ---- the held set: pacing (§11.1) --------------------------------- */
    {
        /* Press and release in one poll: the key is down for exactly
         * COCO_KEY_MIN_FIELDS fields. A is (0,1). */
        fresh();
        press('a');
        release('a');
        unsigned down = 0;
        for (int f = 0; f < 20; f++) {
            fields(1);
            if (cell_down(0, 1)) down++;
        }
        CHECK(down == COCO_KEY_MIN_FIELDS, "held %u fields, want %u", down,
              (unsigned)COCO_KEY_MIN_FIELDS);

        /* Two keys in one poll are serialised with the gap between. B is
         * (0,2). */
        fresh();
        press('a');
        release('a');
        press('b');
        release('b');
        int a_last = -1, b_first = -1;
        for (int f = 0; f < 40; f++) {
            fields(1);
            CHECK(!(cell_down(0, 1) && cell_down(0, 2)), "a and b overlap at field %d", f);
            if (cell_down(0, 1)) a_last = f;
            if (cell_down(0, 2) && b_first < 0) b_first = f;
        }
        CHECK(b_first - a_last - 1 == (int)COCO_KEY_GAP_FIELDS,
              "gap of %d fields, want %u", b_first - a_last - 1,
              (unsigned)COCO_KEY_GAP_FIELDS);

        /* A key held by a human is held here: auto-repeat presses do not
         * release it, and it goes up only when released. J is (1,2). */
        fresh();
        press('j');
        for (int f = 0; f < 30; f++) {
            if (f % 6 == 0) press('j');
            fields(1);
            CHECK(cell_down(1, 2), "j let go at field %d while held", f);
        }
        release('j');
        fields(1);
        CHECK(!cell_down(1, 2), "j should be up after its release");
        CHECK(k.n == 0, "nothing should be held");

        /* Keys that overlapped at the keyboard overlap here: a second
         * press while the first is down is not held back by the gap. */
        fresh();
        press('a');
        press('b');
        fields(1);
        CHECK(cell_down(0, 1) && cell_down(0, 2), "a and b held together");
    }

    /* ---- the host's Shift and the guest's SHIFT (§11.3) --------------- */
    {
        /* Either Shift alone is the CoCo's SHIFT, which games read on its
         * own, and nothing else. */
        static const uint8_t shifts[] = { PICOCALC_KEY_SHIFT_L, PICOCALC_KEY_SHIFT_R };
        for (unsigned i = 0; i < 2; i++) {
            fresh();
            press(shifts[i]);
            fields(1);
            CHECK(shift() && cells_down() == 0, "0x%02X alone is SHIFT alone", shifts[i]);
            release(shifts[i]);
            fields(COCO_KEY_MIN_FIELDS);
            CHECK(matrix_empty(), "and lets it go");
        }

        /* A tap of Shift inside one poll is held as long as a key. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_SHIFT_L);
        unsigned down = 0;
        for (int f = 0; f < 20; f++) {
            fields(1);
            if (shift()) down++;
        }
        CHECK(down == COCO_KEY_MIN_FIELDS, "a tap of Shift held %u fields, want %u", down,
              (unsigned)COCO_KEY_MIN_FIELDS);
        CHECK(keymatrix_idle(&k) && matrix_empty() && !k.shift, "Shift left down after a tap");

        /* Its held events while down do not restart its count. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        fields(COCO_KEY_MIN_FIELDS);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_SHIFT_L);
        fields(1);
        CHECK(!shift(), "Shift held its minimum already, and goes up at once");

        /* Both Shifts: letting one go keeps SHIFT. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_SHIFT_R);
        release(PICOCALC_KEY_SHIFT_L);
        fields(COCO_KEY_MIN_FIELDS + 1);
        CHECK(shift(), "the right Shift still holds SHIFT");

        /* Shift and a key whose binding keeps SHIFT ('"' is SHIFT+2, at
         * (4,2)): SHIFT a field ahead, then both (EL §7.1). */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press('"');
        fields(1);
        CHECK(shift() && !cell_down(4, 2), "Shift+\": SHIFT first, alone");
        fields(1);
        CHECK(shift() && cell_down(4, 2), "Shift+\": then both");

        /* Shift and a key whose binding takes SHIFT away ('@' at (0,0)):
         * SHIFT, then a field with nothing, then '@' alone, so that no
         * scan sees '@' with SHIFT, which is $13. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press('@');
        fields(1);
        CHECK(shift() && cells_down() == 0, "Shift+@: the host's SHIFT first");
        fields(1);
        CHECK(!shift() && cells_down() == 0, "Shift+@: then SHIFT up, the key waiting");
        fields(1);
        CHECK(!shift() && cell_down(0, 0), "Shift+@: then @ without SHIFT");
        down = 0;
        release('@');
        release(PICOCALC_KEY_SHIFT_L);
        for (int f = 0; f < 20; f++) {
            if (cell_down(0, 0)) {
                down++;
                CHECK(!shift(), "@ with SHIFT at field %d", f);
            }
            fields(1);
        }
        CHECK(down == COCO_KEY_MIN_FIELDS, "@ held %u fields, want %u", down,
              (unsigned)COCO_KEY_MIN_FIELDS);
        CHECK(matrix_empty() && keymatrix_idle(&k), "Shift+@ left something down");

        /* A capital takes SHIFT away: the CoCo's letter, whatever the
         * PicoCalc's case. */
        fresh();
        press(PICOCALC_KEY_SHIFT_R);
        press('Q');
        fields(3);
        CHECK(cell_down(2, 1) && !shift(), "Shift+Q is Q without SHIFT");

        /* A KM_SHIFT entry with no Shift held, as a release of Shift
         * before the poll leaves it, or a key the PicoCalc sends unshifted
         * ('=' is SHIFT+-, at (5,5)): SHIFT a field ahead of its cell,
         * and the cell still held its minimum. */
        fresh();
        press('=');
        release('=');
        CHECK(entry_for('=', false)->flags & KM_SHIFT, "= asserts SHIFT");
        fields(1);
        CHECK(shift() && !cell_down(5, 5), "=: SHIFT first, alone");
        down = 0;
        for (int f = 0; f < 20; f++) {
            if (cell_down(5, 5)) {
                down++;
                CHECK(shift(), "= without SHIFT at field %d", f);
            }
            fields(1);
        }
        CHECK(down == COCO_KEY_MIN_FIELDS, "= held %u fields, want %u", down,
              (unsigned)COCO_KEY_MIN_FIELDS);
        CHECK(matrix_empty() && keymatrix_idle(&k), "= left something down");

        /* A key with no SHIFT of its own takes the host's as it finds it:
         * no lead either way. */
        fresh();
        press('5');
        fields(1);
        CHECK(cell_down(4, 5) && !shift(), "5 is 5 at once");
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        fields(1);
        press(PICOCALC_KEY_LEFT);   /* as Shift with an arrow never arrives, but Left first */
        fields(1);
        CHECK(cell_down(3, 5) && shift(), "an arrow with the host's Shift held keeps SHIFT");

        /* The latest key down decides SHIFT: '"' held, then ':' pressed
         * over it, is ':' without SHIFT; let ':' go and '"' has it back. */
        fresh();
        press('"');
        fields(3);
        CHECK(shift() && cell_down(4, 2), "\" held: SHIFT and 2");
        press(':');
        fields(1);
        CHECK(!shift() && !cell_down(5, 2), ": over it: SHIFT up first");
        fields(1);
        CHECK(!shift() && cell_down(5, 2) && cell_down(4, 2), "then : with 2 still down");
        release(':');
        fields(COCO_KEY_MIN_FIELDS);
        CHECK(shift() && cell_down(4, 2) && !cell_down(5, 2), ": let go: SHIFT and 2 again");

        /* The order is kept as keys go up: '"', ':' and '+' held, the
         * first let go, and '+', the latest, still has SHIFT down. */
        fresh();
        press('"');
        fields(3);
        press(':');
        fields(3);
        press('+');
        fields(3);
        CHECK(shift() && cell_down(5, 3), "\" : + held: the latest, +, has SHIFT");
        release('"');
        fields(1);
        CHECK(shift() && cell_down(5, 3) && cell_down(5, 2) && !cell_down(4, 2),
              "\" let go: + still the latest, SHIFT still down");

        /* Releasing Shift first retranslates the release: press '"',
         * release '\'' (hardware-notes.md §6.2). It still lets go. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press('"');
        release(PICOCALC_KEY_SHIFT_L);
        fields(2);
        CHECK(cell_down(4, 2) && shift(), "\" is SHIFT+2");
        release('\'');
        fields(10);
        CHECK(k.n == 0 && matrix_empty(), "a release under the other translation lets go");

        /* A letter held first, then Shift: SHIFT joins it, as on the
         * CoCo's keyboard; the binding fixed at press has no SHIFT of its
         * own, and the MCU's repeat as the capital is absorbed. */
        fresh();
        press('z');
        fields(1);
        CHECK(cell_down(3, 2) && !shift(), "z is Z");
        press(PICOCALC_KEY_SHIFT_R);
        press('Z');   /* the MCU's repeat, absorbed */
        fields(1);
        CHECK(cell_down(3, 2) && shift(), "z held as Shift goes down: SHIFT joins it");

        /* Ctrl reaches no cell, and a key pressed with it types nothing:
         * the CoCo 1 and 2 have no CTRL. */
        fresh();
        press(PICOCALC_KEY_CTRL);
        fields(1);
        CHECK(matrix_empty(), "Ctrl alone reached the matrix");
        press('c');
        fields(2);
        CHECK(matrix_empty() && k.n == 0, "Ctrl+c reached the matrix");
        release('c');
        release(PICOCALC_KEY_CTRL);
        fields(2);
        press('c');
        fields(1);
        CHECK(cell_down(0, 3), "c after Ctrl is C");
        /* A function key with Ctrl is still the menu's. */
        fresh();
        press(PICOCALC_KEY_CTRL);
        press(PICOCALC_KEY_F1);
        fields(1);
        CHECK(k.menu_request && k.menu_page == KM_PAGE_TAPE, "Ctrl+F1 opens the Tapes page");
    }

    /* ---- the CoCo's own keys, the Alt layer and the requests ---------- */
    {
        /* The arrows are the CoCo's, plain keys on row 3 (§11.2). */
        static const struct { uint8_t code, col; } arrows[] = {
            { PICOCALC_KEY_UP, 3 }, { PICOCALC_KEY_DOWN, 4 },
            { PICOCALC_KEY_LEFT, 5 }, { PICOCALC_KEY_RIGHT, 6 },
        };
        for (unsigned i = 0; i < 4; i++) {
            fresh();
            press(arrows[i].code);
            fields(1);
            CHECK(cell_down(3, arrows[i].col) && !shift() && cells_down() == 1,
                  "arrow 0x%02X is (3,%u)", arrows[i].code, arrows[i].col);
            /* With Alt, SHIFT and the arrow: a field apart. */
            fresh();
            press(PICOCALC_KEY_ALT);
            press(arrows[i].code);
            fields(1);
            CHECK(shift() && cells_down() == 0, "Alt+arrow 0x%02X: SHIFT first",
                  arrows[i].code);
            fields(1);
            CHECK(shift() && cell_down(3, arrows[i].col) && cells_down() == 1,
                  "Alt+arrow 0x%02X is SHIFT+(3,%u)", arrows[i].code, arrows[i].col);
        }
        static const struct { uint8_t code, row, col; } own[] = {
            { PICOCALC_KEY_ENTER, 6, 0 }, { PICOCALC_KEY_TAB, 6, 1 },
            { PICOCALC_KEY_ESC, 6, 2 },   { PICOCALC_KEY_BACKSPACE, 3, 5 },
            { PICOCALC_KEY_DEL, 3, 5 },   { ' ', 3, 7 },
        };
        for (unsigned i = 0; i < sizeof own / sizeof own[0]; i++) {
            fresh();
            press(own[i].code);
            fields(1);
            CHECK(cell_down(own[i].row, own[i].col) && !shift() && cells_down() == 1,
                  "0x%02X is (%u,%u)", own[i].code, own[i].row, own[i].col);
        }

        /* Alt+0 and Alt+@ are SHIFT with the key: lower case, and the
         * listing's pause. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press('0');
        fields(2);
        CHECK(shift() && cell_down(4, 0) && cells_down() == 1, "Alt+0 is SHIFT+0");
        fresh();
        press(PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_SHIFT_L);
        press('@');
        fields(3);
        CHECK(shift() && cell_down(0, 0) && cells_down() == 1, "Alt+@ is SHIFT+@");

        /* The binding is fixed at press: Alt+K asks for the reset button,
         * and its release as 'k' after Alt is up presses nothing. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press('K');
        fields(1);
        CHECK(k.reset_request && matrix_empty(), "Alt+K asks for the reset button");
        release(PICOCALC_KEY_ALT);
        fields(1);
        release('k');
        fields(10);
        CHECK(k.n == 0 && matrix_empty(), "K's release under the other translation");

        /* Insert is Alt+I as well as Shift+Enter (hardware-notes.md
         * §6.3). Let Alt go first and I's release arrives as 'i': it must
         * still close the press, or the next Enter is taken for a repeat. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        release(PICOCALC_KEY_ALT);
        release('i');
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k), "Alt+I left %u press(es) open", k.n_open);
        press(PICOCALC_KEY_ENTER);
        fields(1);
        CHECK(cell_down(6, 0), "Enter after Alt+I is not taken for a repeat");

        /* The sequence pico-ace's device sent: Alt let go while I was
         * down, then the MCU's auto-repeat retranslated as presses of
         * 'i', then 'i' released. */
        fresh();
        keymatrix_event(&k, KEY_EV_PRESSED, PICOCALC_KEY_ALT);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_ALT);
        release(PICOCALC_KEY_ALT);
        for (int i = 0; i < 4; i++) press('i');
        release('i');
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k) && matrix_empty(),
              "the captured Alt+I left %u press(es) open", k.n_open);
        CHECK(k.q_len == 0, "the repeats were not absorbed");

        /* Shift+Enter is Insert too: SHIFT+ENTER, and its release under
         * the other translation, Enter, closes it. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_INSERT);
        fields(2);
        CHECK(cell_down(6, 0) && shift(), "Shift+Enter is SHIFT+ENTER");
        release(PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_ENTER);
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k) && matrix_empty(),
              "Shift+Enter left %u press(es) open", k.n_open);

        /* Alt+I is Insert, and reaches nothing: the Alt layer has no I. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        fields(2);
        CHECK(matrix_empty() && k.n == 0, "Alt+I reached the matrix");

        /* The keys the PicoCalc sends only with Shift are their base
         * key's cell with SHIFT: End is SHIFT+LEFT, which erases the line. */
        static const struct { uint8_t code, row, col; } shifted[] = {
            { PICOCALC_KEY_INSERT,    6, 0 },
            { PICOCALC_KEY_BREAK,     6, 2 },
            { PICOCALC_KEY_HOME,      6, 1 },
            { PICOCALC_KEY_END,       3, 5 },
            { PICOCALC_KEY_PAGE_UP,   3, 3 },
            { PICOCALC_KEY_PAGE_DOWN, 3, 4 },
        };
        for (unsigned i = 0; i < sizeof shifted / sizeof shifted[0]; i++) {
            fresh();
            press(PICOCALC_KEY_SHIFT_R);
            press(shifted[i].code);
            fields(2);
            CHECK(cell_down(shifted[i].row, shifted[i].col) && shift() && cells_down() == 1,
                  "Shift+0x%02X is SHIFT+(%u,%u)", shifted[i].code, shifted[i].row,
                  shifted[i].col);
        }

        /* An Alt chord with no binding reaches nothing. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press('Q');
        fields(1);
        CHECK(k.n == 0 && matrix_empty(), "Alt+Q should do nothing");

        /* Alt+M, Alt+H, Alt+P and Alt+K ask, and touch nothing in the
         * machine (§14). */
        static const struct { uint8_t code; bool *flag; const char *what; } asks[] = {
            { 'M', &k.menu_request,  "menu"  },
            { 'H', &k.menu_request,  "help page" },
            { 'P', &k.pause_request, "pause" },
            { 'K', &k.reset_request, "reset button" },
        };
        for (unsigned i = 0; i < 4; i++) {
            fresh();
            press(PICOCALC_KEY_ALT);
            press(asks[i].code);
            fields(1);
            CHECK(*asks[i].flag, "Alt+%c requests the %s", asks[i].code, asks[i].what);
            CHECK(k.menu_request + k.pause_request + k.reset_request + k.shot_request == 1,
                  "Alt+%c requests only the %s", asks[i].code, asks[i].what);
            CHECK(matrix_empty(), "Alt+%c reached the matrix", asks[i].code);
        }
        fresh();
        press(PICOCALC_KEY_ALT);
        press('M');
        fields(1);
        CHECK(k.menu_page == KM_PAGE_MAIN, "Alt+M opens the main page");
        fresh();
        press(PICOCALC_KEY_ALT);
        press('H');
        fields(1);
        CHECK(k.menu_page == KM_PAGE_HELP, "Alt+H opens the help page");

        /* F1-F5 open §14's pages, with or without Alt held; F6, Shift+F1,
         * is a screenshot; F10 is About. */
        static const uint8_t pages[5] = {
            KM_PAGE_TAPE, KM_PAGE_DISC, KM_PAGE_SNAPSHOT, KM_PAGE_SETUP, KM_PAGE_MACHINE,
        };
        for (unsigned alt = 0; alt < 2; alt++) {
            for (unsigned f = 0; f < 5; f++) {
                fresh();
                if (alt) press(PICOCALC_KEY_ALT);
                press((uint8_t)(PICOCALC_KEY_F1 + f));
                fields(1);
                CHECK(k.menu_request && k.menu_page == pages[f], "%sF%u: page %u, want %u",
                      alt ? "Alt+" : "", f + 1, k.menu_page, pages[f]);
                CHECK(matrix_empty(), "F%u reached the matrix", f + 1);
            }
        }
        for (unsigned alt = 0; alt < 2; alt++) {
            fresh();
            if (alt) press(PICOCALC_KEY_ALT);
            press(PICOCALC_KEY_SHIFT_L);
            press(PICOCALC_KEY_F6);
            fields(2);
            CHECK(k.shot_request, "%sF6 requests a screenshot", alt ? "Alt+" : "");
            CHECK(!k.menu_request && !k.pause_request && !k.reset_request,
                  "%sF6 requests only a screenshot", alt ? "Alt+" : "");
            /* The host's Shift is the CoCo's SHIFT, held as it is. */
            CHECK(cells_down() == 0, "%sF6 reached the matrix", alt ? "Alt+" : "");
        }

        /* F6 is Shift+F1, so a Shift let go first turns its release into
         * F1's (hardware-notes.md §6.2). That release must let go of F6,
         * or the next F6 is taken for its auto-repeat and asks nothing. */
        CHECK(keymap_picocalc_canonical(PICOCALC_KEY_F6) == PICOCALC_KEY_F1 &&
              keymap_picocalc_canonical(PICOCALC_KEY_F10) == PICOCALC_KEY_F1 + 4u,
              "F6 and F10 are F1's and F5's keys");
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F6);
        fields(COCO_KEY_MIN_FIELDS + 1);
        CHECK(k.shot_request, "F6 requests a screenshot");
        k.shot_request = false;
        release(PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_F1);
        fields(COCO_KEY_GAP_FIELDS + 2);
        CHECK(k.n == 0, "F1's release left %u key(s) held", k.n);
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F6);
        fields(2);
        CHECK(k.shot_request, "a second F6 requests another");
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F10);
        fields(2);
        CHECK(k.menu_request && k.menu_page == KM_PAGE_ABOUT, "F10 opens About");

        /* Caps Lock is an event the MCU sends and nothing binds
         * (hardware-notes.md §6.2): accepted, and nothing held. */
        fresh();
        press(PICOCALC_KEY_CAPS);
        release(PICOCALC_KEY_CAPS);
        fields(10);
        CHECK(keymatrix_idle(&k) && k.n_open == 0 && matrix_empty(), "Caps Lock left a key");
    }

    /* ---- the queue is bounded, and never loses a release -------------- */
    {
        /* Auto-repeat of a key already down is absorbed at once. */
        fresh();
        for (unsigned i = 0; i < COCO_KEY_EVENT_QUEUE + 5u; i++) press('x');
        CHECK(k.q_len == 1 && k.dropped == 0, "repeats: q_len %u, dropped %u", k.q_len,
              (unsigned)k.dropped);
        release('x');

        /* A burst far faster than the replay: presses are refused once
         * the queue is short of room, but every key that went down comes
         * back up, so nothing is left held. */
        fresh();
        static const char burst[] = "the quick brown fox jumps over the lazy dog 0123456789";
        for (int rep = 0; rep < 3; rep++) {
            for (const char *c = burst; *c; c++) {
                press((uint8_t)*c);
                release((uint8_t)*c);
            }
        }
        CHECK(k.dropped > 0, "the burst should have overflowed");
        CHECK(k.q_len + k.n_open <= COCO_KEY_EVENT_QUEUE, "a release has no room");
        for (int f = 0; f < 2000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(keymatrix_idle(&k) && k.n_open == 0, "stuck keys: %u held, %u open", k.n,
              k.n_open);
        CHECK(matrix_empty(), "the matrix is left with keys down");

        /* The same, with Shift, Ctrl and Alt in the burst: a modifier's
         * release is kept like any other. */
        fresh();
        for (int i = 0; i < 100; i++) {
            press(PICOCALC_KEY_ALT);
            press(PICOCALC_KEY_SHIFT_L);
            press(PICOCALC_KEY_CTRL);
            press('P');
            release('P');
            release(PICOCALC_KEY_CTRL);
            release(PICOCALC_KEY_SHIFT_L);
            release(PICOCALC_KEY_ALT);
        }
        for (int f = 0; f < 4000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(!k.alt && !k.shift && !k.ctrl && k.n == 0 && k.n_open == 0 && matrix_empty(),
              "a modifier left down after a burst");

        /* Mixed SHIFT bindings in a burst leave SHIFT up. */
        fresh();
        static const char mixed[] = "A\"@:=b*Q^_[";
        for (int rep = 0; rep < 10; rep++)
            for (const char *c = mixed; *c; c++) {
                press((uint8_t)*c);
                release((uint8_t)*c);
            }
        for (int f = 0; f < 4000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(keymatrix_idle(&k) && matrix_empty() && !k.shift_shown,
              "SHIFT left down after a mixed burst");
    }

    TEST_DONE();
}
